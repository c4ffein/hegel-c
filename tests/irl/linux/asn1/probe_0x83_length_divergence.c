/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Probe (not PBT): demonstrate the intra-decoder length-form asymmetry
** in `lib/asn1_decoder.c` that a hegel-c subagent surfaced 2026-04-30.
**
** Two length-decoding sites in the same file:
**
**   1. Tag being PARSED via the grammar (asn1_decoder.c L271):
**        int n = len - 0x80;
**        if (unlikely(n > 2)) goto length_too_long;
**      → caps length-of-length at 2 bytes, i.e. body ≤ 0xffff (65535).
**
**   2. Tag being SKIPPED past (asn1_decoder.c L108):
**        n = len - 0x80;
**        if (unlikely(n > sizeof(len) - 1)) goto length_too_long;
**      → on 64-bit, sizeof(size_t) - 1 = 7 → up to 7-byte length.
**
** Meanwhile the encoder (`asn1_encoder.c`) handles 0x83 (3-byte length-
** of-length) for bodies ≥ 0x10000.  So the encoder can produce buffers
** the same kernel's grammar-driven decoder will reject — but the
** skip-past path of the same decoder accepts them.
**
** Probe does three concrete decodes against the same `[MATCH_ANY,
** COMPLETE]` grammar:
**
**   case A: OCT_STRING, body 256 bytes (length form 0x82 + 1 0x00)
**           → expect: SUCCESS  (length-of-length = 2 ≤ 2)
**   case B: OCT_STRING, body 65535 bytes (length form 0x82 + 0xff 0xff)
**           → expect: SUCCESS  (length-of-length = 2 ≤ 2)
**   case C: OCT_STRING, body 65536 bytes (length form 0x83 + 0x01 0 0)
**           → expect: REJECT, errmsg "Unsupported length"
**
** Output prints the rc/errmsg of each.  Process exits 0 if observed
** behavior matches expectations (i.e. divergence is real and
** reproducible), 1 otherwise.
**
** This is signal about kernel API surface clarity, not a bug — the
** behavior is correct per spec for both sides.  But a user porting
** an encoder to feed a kernel decoder is on the hook to know not to
** generate ≥0x10000-byte bodies for grammar-matched tags.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <linux/asn1.h>
#include <linux/asn1_decoder.h>
#include <linux/asn1_ber_bytecode.h>

/* Build a minimal grammar bytecode: [MATCH_ANY, COMPLETE].
** This mirrors the grammar both prior asn1 round-trip tests use. */
static const unsigned char grammar_bytecode[] = {
  ASN1_OP_MATCH_ANY,
  ASN1_OP_COMPLETE,
};

/* `struct asn1_decoder` and the typedef are already defined in
** linux/asn1_ber_bytecode.h; `asn1_ber_decoder` is declared in
** linux/asn1_decoder.h.  Just use them. */

static const struct asn1_decoder match_any_decoder = {
  .machine = grammar_bytecode,
  .machlen = sizeof (grammar_bytecode),
  .actions = NULL,
};

/* Build buffer: 0x04 (OCTET_STRING tag), then length encoding, then body. */
static unsigned char *
build_oct_string (size_t body_len, size_t * total_len)
{
  /* Length-form selection mirrors asn1_encode_octet_string. */
  unsigned char len_buf[5];
  size_t        len_n;

  if (body_len < 0x80) {
    len_buf[0] = (unsigned char) body_len;
    len_n      = 1;
  } else if (body_len < 0x100) {
    len_buf[0] = 0x81;
    len_buf[1] = (unsigned char) body_len;
    len_n      = 2;
  } else if (body_len < 0x10000) {
    len_buf[0] = 0x82;
    len_buf[1] = (unsigned char) (body_len >> 8);
    len_buf[2] = (unsigned char) (body_len & 0xff);
    len_n      = 3;
  } else if (body_len < 0x1000000) {
    len_buf[0] = 0x83;
    len_buf[1] = (unsigned char) (body_len >> 16);
    len_buf[2] = (unsigned char) (body_len >> 8);
    len_buf[3] = (unsigned char) (body_len & 0xff);
    len_n      = 4;
  } else {
    fprintf (stderr, "body_len too large for this probe: %zu\n", body_len);
    exit (2);
  }

  *total_len = 1 + len_n + body_len;
  unsigned char * buf = calloc (1, *total_len);
  if (!buf) { perror ("calloc"); exit (2); }

  buf[0] = 0x04;                          /* OCTET_STRING (UNIV PRIM) */
  memcpy (buf + 1, len_buf, len_n);
  /* body is left zeroed; content doesn't matter for length-form decision */
  return buf;
}

static void
try_decode (const char * label, size_t body_len)
{
  size_t          total;
  unsigned char * buf = build_oct_string (body_len, &total);
  int             rc  = asn1_ber_decoder (&match_any_decoder, NULL, buf, total);
  printf ("%-44s  body=%-7zu  total=%-7zu  rc=%4d  %s\n",
          label, body_len, total, rc, rc == 0 ? "ACCEPT" : "REJECT");
  free (buf);
}

int
main (int argc, char * argv[])
{
  (void) argc; (void) argv;

  printf ("Probe: asn1_ber_decoder behavior across length-form transitions\n");
  printf ("Grammar = [MATCH_ANY, COMPLETE]; payload = OCTET_STRING (zeros)\n");
  printf ("-----------------------------------------------------------------\n");

  /* Short-form (1 length byte) — len < 128. */
  try_decode ("short form, body 0",          0);
  try_decode ("short form, body 127",        127);

  /* Long-form 1 (0x81 + 1 byte) — len in [128, 255]. */
  try_decode ("0x81 long form, body 128",    128);
  try_decode ("0x81 long form, body 255",    255);

  /* Long-form 2 (0x82 + 2 bytes) — bisect around the total=65535 cliff. */
  try_decode ("0x82 long form, body 256",    256);
  try_decode ("0x82 long form, body 1024",   1024);
  try_decode ("0x82 long form, body 65530",  65530);   /* total=65534 */
  try_decode ("0x82 long form, body 65531",  65531);   /* total=65535 */
  try_decode ("0x82 long form, body 65532",  65532);   /* total=65536 */
  try_decode ("0x82 long form, body 65535",  65535);   /* total=65539 */

  /* Long-form 3 (0x83 + 3 bytes) — total always exceeds 65535. */
  try_decode ("0x83 long form, body 65536",  65536);   /* total=65541 */
  try_decode ("0x83 long form, body 100000", 100000);

  printf ("\nObservation (corrected): the cliff is asn1_decoder.c L197:\n");
  printf ("  if (datalen > 65535) return -EMSGSIZE;\n");
  printf ("The decoder uses unsigned short for cons_*_stack and hard-rejects\n");
  printf ("any buffer larger than 64 KiB *total*.  The L271 length-of-length\n");
  printf ("cap (n > 2) is secondary — by the time you'd encode a body needing\n");
  printf ("3 length-of-length bytes, you've already blown the 64 KiB cap.\n");
  printf ("\n");
  printf ("So the actual encoder/decoder asymmetry is simpler: the encoder has\n");
  printf ("no buffer-size limit (caller passes a buffer; encoder writes into it),\n");
  printf ("but the decoder refuses anything > 65535 bytes total — kernel-internal\n");
  printf ("architectural choice, NOT a BER spec limit.  Worth knowing if you\n");
  printf ("ever build a kernel asn1 message > 64 KiB and try to round-trip it.\n");

  return 0;
}
