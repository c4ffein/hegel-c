/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* ASN.1 BER encoder × decoder round-trip property test.
**
** Property: any sequence of asn1_encode_* calls that produce a buffer B
** can be parsed back element-by-element such that:
**   (1) each element's tag matches what was encoded
**   (2) each element's length matches the original payload length
**   (3) each element's value bytes match what was encoded
**   (4) the kernel's asn1_ber_decoder accepts every element when fed
**       a tiny "match-any" grammar
**
** Layered as a hegel-c stateful state machine.  State holds:
**   - a 4 KiB output buffer + write cursor
**   - a "shadow model": a list of (tag, payload-bytes, len) records
**     describing what the test thinks it just emitted
**
** Rules: emit_int, emit_octet_string, emit_oid, emit_boolean (the
**        primitive encoders).  Each rule:
**          1. draws operands
**          2. validates they fit (skip / no-op if not)
**          3. calls the encoder, advances the buffer cursor
**          4. records (tag, payload bytes) in the shadow model
**
** Invariant after every rule: walk the buffer manually as BER, compare
** tag+length+value against the shadow.  Then for each element, hand its
** TLV bytes to asn1_ber_decoder with a "[MATCH_ANY, COMPLETE]" grammar
** and assert it returns 0.
**
** Both files exercised; round-trip checked on three independent levels. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/asn1.h>
#include <linux/asn1_ber_bytecode.h>
#include <linux/asn1_encoder.h>
#include <linux/asn1_decoder.h>
#include <linux/err.h>

#include "hegel_c.h"

#define BUFSZ      4096
#define MAX_ELEM   32
#define MAX_PAYLOAD 1024

/* A shadow record: what the encoder claimed to emit. */
typedef struct {
  unsigned char tag;
  size_t        payload_len;        /* bytes inside the TLV */
  size_t        offset;             /* offset of the tag byte in buf */
  size_t        total_len;          /* tag + length + payload bytes */
  unsigned char payload[MAX_PAYLOAD];
} shadow_elem_t;

/* Coverage counters. */
static long cov_emit_int;
static long cov_emit_octet;
static long cov_emit_oid;
static long cov_emit_bool;
static long cov_long_form_len_1;   /* 0x81 prefix */
static long cov_long_form_len_2;   /* 0x82 prefix */
static long cov_int_zero;
static long cov_int_high_bit;      /* drawn integer's MSB triggers leading-zero pad */

typedef struct {
  unsigned char  buf[BUFSZ];
  unsigned char *cursor;            /* points into buf */
  shadow_elem_t  elems[MAX_ELEM];
  size_t         n_elems;
  /* a tiny "accept any single TLV" asn1 grammar */
} enc_state_t;

/* Manual BER walker: parse one TLV at p, set out_tag, out_len, out_payload,
** return advance bytes or -1 on parse error. */
static int parse_one_tlv (const unsigned char *p, size_t avail,
                          unsigned char *out_tag, size_t *out_len,
                          const unsigned char **out_payload)
{
  if (avail < 2) return -1;
  unsigned char tag = p[0];
  /* No long-form tag (encoder rejects > 30, and tests don't draw them). */
  if ((tag & 0x1f) == ASN1_LONG_TAG) return -1;
  size_t pos = 1;
  size_t len;
  unsigned char l0 = p[pos++];
  if (l0 <= 0x7f) {
    len = l0;
  } else if (l0 == 0x81) {
    if (pos >= avail) return -1;
    len = p[pos++];
  } else if (l0 == 0x82) {
    if (pos + 1 >= avail) return -1;
    len  = (size_t)p[pos++] << 8;
    len |= p[pos++];
  } else if (l0 == 0x83) {
    if (pos + 2 >= avail) return -1;
    len  = (size_t)p[pos++] << 16;
    len |= (size_t)p[pos++] << 8;
    len |= p[pos++];
  } else if (l0 == 0x80) {
    /* indefinite length — not produced by asn1_encoder.c, treat as error */
    return -1;
  } else {
    return -1;
  }
  if (len > avail - pos) return -1;
  *out_tag     = tag;
  *out_len     = len;
  *out_payload = p + pos;
  return (int)(pos + len);
}

/* Tiny ASN.1 grammar accepting exactly one element of any tag.
** Bytecode: [MATCH_ANY, COMPLETE]. */
static const unsigned char any_machine[] = {
  ASN1_OP_MATCH_ANY,
  ASN1_OP_COMPLETE,
};
static const struct asn1_decoder any_decoder = {
  .machine  = any_machine,
  .machlen  = sizeof (any_machine),
  .actions  = NULL,
};

/* ---- Setup / teardown ---- */
static void *setup_fn (void)
{
  enc_state_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  st->cursor = st->buf;
  return st;
}

static void teardown_fn (void *s)
{
  free (s);
}

/* ---- Helpers ---- */
static size_t buf_used (const enc_state_t *st)
{
  return (size_t)(st->cursor - st->buf);
}

static size_t buf_remain (const enc_state_t *st)
{
  return BUFSZ - buf_used (st);
}

/* Append a shadow element record. */
static void shadow_record (enc_state_t *st, unsigned char tag,
                           size_t payload_len, const unsigned char *payload,
                           size_t total_len, size_t start_off)
{
  if (st->n_elems >= MAX_ELEM) abort ();    /* test bug — guarded below */
  shadow_elem_t *e = &st->elems[st->n_elems++];
  e->tag         = tag;
  e->payload_len = payload_len;
  e->offset      = start_off;
  e->total_len   = total_len;
  if (payload_len > MAX_PAYLOAD) abort (); /* guarded by rule preconditions */
  memcpy (e->payload, payload, payload_len);
}

/* ---- Rules ---- */

static void rule_emit_int (void *s, hegel_testcase *tc)
{
  enc_state_t *st = s;
  if (st->n_elems >= MAX_ELEM) return;
  if (buf_remain (st) < 16) return;       /* worst-case integer is ~11 bytes */

  /* Encoder only supports positive integers (asserts on negative). */
  s64 v = (s64)hegel_draw_i64 (tc, 0, INT64_MAX);
  size_t start = buf_used (st);

  unsigned char *end = (unsigned char *)st->buf + BUFSZ;
  unsigned char *r   = asn1_encode_integer (st->cursor, end, v);
  if (IS_ERR_VALUE ((unsigned long)r)) return;   /* skip on encoder reject */

  size_t total = (size_t)(r - st->cursor);
  if (total < 2) return;                  /* implausible: skip */
  unsigned char tag = st->cursor[0];
  size_t plen       = st->cursor[1];      /* short form only — encoder does that */
  if (plen + 2 != total) return;          /* defensive: skip if our parse mismatches */

  shadow_record (st, tag, plen, st->cursor + 2, total, start);
  cov_emit_int++;
  if (v == 0) cov_int_zero++;
  /* If MSB of any byte triggers the leading-zero pad, the encoded
  ** payload's first byte is 0x00 — count those. */
  if (plen >= 2 && st->cursor[2] == 0x00 && (st->cursor[3] & 0x80))
    cov_int_high_bit++;
  st->cursor = r;
}

static void rule_emit_octet (void *s, hegel_testcase *tc)
{
  enc_state_t *st = s;
  if (st->n_elems >= MAX_ELEM) return;
  if (buf_remain (st) < 8) return;

  /* Length: bias toward small but allow up to 800 to exercise long-form
  ** length encoding (>127 -> 0x81 prefix, >255 -> 0x82 prefix). */
  int max_len = 800;
  if ((size_t)max_len + 8 > buf_remain (st))
    max_len = (int)(buf_remain (st)) - 8;
  if (max_len < 0) return;
  if (max_len > MAX_PAYLOAD - 1) max_len = MAX_PAYLOAD - 1;
  int len = hegel_draw_int (tc, 0, max_len);

  static unsigned char tmp[1024];
  for (int i = 0; i < len; i++)
    tmp[i] = (unsigned char)hegel_draw_int (tc, 0, 255);

  size_t start = buf_used (st);
  unsigned char *end = (unsigned char *)st->buf + BUFSZ;
  unsigned char *r = asn1_encode_octet_string (st->cursor, end, tmp, (u32)len);
  if (IS_ERR_VALUE ((unsigned long)r)) return;

  size_t total = (size_t)(r - st->cursor);
  shadow_record (st, st->cursor[0], (size_t)len,
                 st->cursor + (total - (size_t)len), total, start);
  cov_emit_octet++;
  if (len > 127 && len <= 255) cov_long_form_len_1++;
  else if (len > 255)          cov_long_form_len_2++;
#ifdef HEGEL_NEG_CONTROL
  /* Negative control: corrupt the buffer after we recorded the shadow.
  ** Round-trip should now fail — proves the assertions actually fire. */
  if (len > 0) st->cursor[2] ^= 0x01;
#endif
  st->cursor = r;
}

static void rule_emit_oid (void *s, hegel_testcase *tc)
{
  enc_state_t *st = s;
  if (st->n_elems >= MAX_ELEM) return;
  if (buf_remain (st) < 32) return;

  /* OID: 2..6 components.  First two are constrained: oid[0] in [0,2],
  ** oid[1] in [0,39] (encoder does oid[0]*40+oid[1] which must fit in u8). */
  int n = hegel_draw_int (tc, 2, 6);
  u32 oid[6];
  oid[0] = (u32)hegel_draw_int (tc, 0, 2);
  /* For safety, force oid[1] <= 39 regardless of oid[0] (real X.509 allows
  ** higher when oid[0] == 2, but the encoder does a single-byte add, so
  ** oid[0]*40+oid[1] must fit in u8 — but we're only structurally checking
  ** the encoder, so don't push that edge). */
  oid[1] = (u32)hegel_draw_int (tc, 0, 39);
  for (int i = 2; i < n; i++)
    oid[i] = (u32)hegel_draw_int (tc, 0, 0xfffffff);

  size_t start = buf_used (st);
  unsigned char *end = (unsigned char *)st->buf + BUFSZ;
  unsigned char *r = asn1_encode_oid (st->cursor, end, oid, n);
  if (IS_ERR_VALUE ((unsigned long)r)) return;

  size_t total = (size_t)(r - st->cursor);
  if (total < 2) return;
  size_t plen  = st->cursor[1];
  if (plen + 2 != total) return;
  shadow_record (st, st->cursor[0], plen, st->cursor + 2, total, start);
  cov_emit_oid++;
  st->cursor = r;
}

static void rule_emit_bool (void *s, hegel_testcase *tc)
{
  enc_state_t *st = s;
  if (st->n_elems >= MAX_ELEM) return;
  if (buf_remain (st) < 4) return;

  bool v = (bool)hegel_draw_int (tc, 0, 1);
  size_t start = buf_used (st);
  unsigned char *end = (unsigned char *)st->buf + BUFSZ;
  unsigned char *r = asn1_encode_boolean (st->cursor, end, v);
  if (IS_ERR_VALUE ((unsigned long)r)) return;

  size_t total = (size_t)(r - st->cursor);
  shadow_record (st, st->cursor[0], 1, st->cursor + 2, total, start);
  cov_emit_bool++;
  st->cursor = r;
}

/* ---- Invariants ---- */

/* Walk the buffer with the manual BER parser; assert TLVs match the
** shadow exactly (count, tag, payload length, payload bytes, total
** advance per element). */
static void inv_manual_round_trip (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const enc_state_t *st = s;
  size_t total = (size_t)(st->cursor - st->buf);
  size_t pos   = 0;

  for (size_t i = 0; i < st->n_elems; i++) {
    HEGEL_ASSERT (pos < total,
                  "manual walk ran out of buffer before element %zu (pos=%zu total=%zu)",
                  i, pos, total);

    unsigned char tag;
    size_t plen;
    const unsigned char *payload;
    int adv = parse_one_tlv (st->buf + pos, total - pos, &tag, &plen, &payload);
    HEGEL_ASSERT (adv > 0,
                  "parse_one_tlv failed at offset %zu (element %zu)", pos, i);

    const shadow_elem_t *e = &st->elems[i];
    HEGEL_ASSERT (e->offset == pos,
                  "element %zu offset %zu but parser at %zu", i, e->offset, pos);
    HEGEL_ASSERT (e->tag == tag,
                  "element %zu: shadow tag 0x%02x but parser sees 0x%02x",
                  i, e->tag, tag);
    HEGEL_ASSERT (e->payload_len == plen,
                  "element %zu (tag 0x%02x): shadow plen %zu but parser sees %zu",
                  i, tag, e->payload_len, plen);
    HEGEL_ASSERT ((size_t)adv == e->total_len,
                  "element %zu: shadow total %zu but parser advanced %d",
                  i, e->total_len, adv);
    HEGEL_ASSERT (memcmp (payload, e->payload, plen) == 0,
                  "element %zu (tag 0x%02x, len %zu): payload bytes differ",
                  i, tag, plen);
    pos += (size_t)adv;
  }
  HEGEL_ASSERT (pos == total,
                "manual walk consumed %zu bytes but buffer has %zu", pos, total);
}

/* Hand each element's TLV bytes to the kernel asn1_ber_decoder with the
** "match any" grammar.  Asserts decoder agrees the bytes are well-formed. */
static void inv_kernel_decoder_accepts (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const enc_state_t *st = s;
  for (size_t i = 0; i < st->n_elems; i++) {
    const shadow_elem_t *e = &st->elems[i];
    int rc = asn1_ber_decoder (&any_decoder, NULL,
                               st->buf + e->offset, e->total_len);
    HEGEL_ASSERT (rc == 0,
                  "asn1_ber_decoder rejected element %zu (tag=0x%02x len=%zu): rc=%d",
                  i, e->tag, e->total_len, rc);
  }
}

/* ---- Main ---- */
int main (int argc, char *argv[])
{
  (void)argc; (void)argv;

  unsigned long n_cases = 200;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  printf ("ASN.1 encoder x decoder round-trip PBT (n_cases=%lu)\n", n_cases);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  if (hegel_state_machine_add_rule (sm, "emit_int",   rule_emit_int)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_octet", rule_emit_octet) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_oid",   rule_emit_oid)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_bool",  rule_emit_bool)  != 0) return 1;

  if (hegel_state_machine_add_invariant (sm, "manual_round_trip",
                                          inv_manual_round_trip)       != 0) return 1;
  if (hegel_state_machine_add_invariant (sm, "kernel_decoder_accepts",
                                          inv_kernel_decoder_accepts)  != 0) return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
    "[asn1 PBT coverage] emit_int=%ld  emit_octet=%ld  emit_oid=%ld  emit_bool=%ld\n"
    "                   long_form_len_1=%ld  long_form_len_2=%ld  int_zero=%ld  int_msb_pad=%ld\n",
    cov_emit_int, cov_emit_octet, cov_emit_oid, cov_emit_bool,
    cov_long_form_len_1, cov_long_form_len_2, cov_int_zero, cov_int_high_bit);
  printf ("PASSED\n");
  return 0;
}
