/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Property: for the binary BCH code with parameters (m, t),
**   for any random k-byte message m_buf and any error pattern e of
**   bit-weight <= t injected anywhere in the codeword (m_buf || ecc),
**   bch_decode reports exactly the injected error positions and
**   applying the recovered mask recovers the original codeword.
**
** API summary (from include/linux/bch.h):
**   bch = bch_init(m, t, prim_poly=0, swap_bits=false);
**   bch_encode(bch, data, len_msg, ecc);            <-- writes parity
**   nerr = bch_decode(bch, data, len_msg, recv_ecc, NULL, NULL, errloc);
**     errloc[i] is a BIT INDEX into the codeword.  bch_decode's final
**     post-processing block (when swap_bits=false) XORs the lower 3
**     bits of errloc with 7, so the caller can apply the correction
**     using the natural C mask `1 << (errloc[i] & 7)` — i.e.,
**     LSB-FIRST within byte.  We use the same convention to inject. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bch.h>
#include "hegel_c.h"

/* We pick m=14, t=4 so that m*t = 56 is a multiple of 8.  This makes
** the ecc region have NO padding bits — every storage bit in the ecc
** bytes is part of the codeword.  With m=13, t=4 (m*t=52, ecc_bytes=7,
** padding=4) the test would have to skip the 4 padding bits within the
** last ecc byte, which the decoder silently ignores any flip in.  See
** the probe history: bch_decode reports n_recovered=0 (no detection)
** for a single flip at any of those 4 padding bits.  That's not a bug
** in bch.c — those bits aren't part of the defined codeword — but it's
** a sharp corner an inattentive caller could trip on, and a different
** property test (boundary-around-padding) could call it out. */
#define BCH_M     14
#define BCH_T     4
#define MSG_BYTES 64        /* 512 message bits; n=2^14-1=16383, ecc=56 */

/* Flip the bit addressed by an errloc[] value into buf.  bch_decode's
** post-processing (final block in bch_decode, when swap_bits=false)
** XORs the lower 3 bits with 7 so the caller can apply the correction
** with the natural C `1 << (errloc & 7)` mask — i.e., LSB-first within
** byte. We use the same convention for both injection and recovery so
** the round-trip is well-defined. */
static void flip_bit (uint8_t *buf, unsigned int errloc)
{
  unsigned int byte = errloc >> 3;
  unsigned int bit  = errloc & 7;          /* 0..7 within byte, LSB-first */
  buf[byte] ^= (uint8_t) (1u << bit);
}

void test_bch (hegel_testcase *tc)
{
  struct bch_control *bch = bch_init (BCH_M, BCH_T, 0, false);
  HEGEL_ASSERT (bch != NULL, "bch_init failed");

  unsigned int ecc_bytes = bch->ecc_bytes;

  /* Fail loudly if our compile-time MSG_BYTES violates the contract. */
  HEGEL_ASSERT (8 * MSG_BYTES <= bch->n - bch->ecc_bits,
                "msg too long: 8*%u > n-ecc_bits = %u-%u",
                MSG_BYTES, bch->n, bch->ecc_bits);

  /* --- 1. Draw a random message ------------------------------------ */
  uint8_t msg[MSG_BYTES];
  for (size_t i = 0; i < MSG_BYTES; i++)
    msg[i] = (uint8_t) hegel_draw_int (tc, 0, 255);

  /* --- 2. Encode --------------------------------------------------- */
  uint8_t *ecc = (uint8_t *) calloc (1, ecc_bytes);
  HEGEL_ASSERT (ecc != NULL, "calloc ecc failed");
  bch_encode (bch, msg, MSG_BYTES, ecc);

  /* Build the codeword: [msg | ecc] */
  unsigned int cw_bytes = MSG_BYTES + ecc_bytes;
  unsigned int cw_bits  = 8 * MSG_BYTES + bch->ecc_bits;
  uint8_t *original = (uint8_t *) malloc (cw_bytes);
  uint8_t *received = (uint8_t *) malloc (cw_bytes);
  HEGEL_ASSERT (original && received, "malloc cw failed");
  memcpy (original, msg, MSG_BYTES);
  memcpy (original + MSG_BYTES, ecc, ecc_bytes);
  memcpy (received, original, cw_bytes);

  /* --- 3. Inject 0..t distinct bit-flips --------------------------- */
  int n_errors = hegel_draw_int (tc, 0, BCH_T);
  unsigned int err_pos[BCH_T];

  for (int i = 0; i < n_errors; i++) {
    /* Pick a fresh bit position, retrying on collision. */
    unsigned int pos;
    int collide;
    int attempts = 0;
    do {
      collide = 0;
      pos = (unsigned int) hegel_draw_int (tc, 0, (int) cw_bits - 1);
      for (int j = 0; j < i; j++)
        if (err_pos[j] == pos) { collide = 1; break; }
      attempts++;
      if (attempts > 100) { hegel_assume (tc, 0); return; }
    } while (collide);
    err_pos[i] = pos;

    flip_bit (received, pos);
  }

  {
    char buf[256];
    int off = snprintf (buf, sizeof buf, "n_errors=%d positions=[", n_errors);
    for (int i = 0; i < n_errors; i++)
      off += snprintf (buf + off, sizeof buf - off, "%s%u",
                       i ? "," : "", err_pos[i]);
    snprintf (buf + off, sizeof buf - off, "]");
    hegel_note (tc, buf);
  }

  /* --- 4. Decode --------------------------------------------------- */
  unsigned int errloc[BCH_T];
  memset (errloc, 0, sizeof errloc);
  int n_recovered = bch_decode (bch, received, MSG_BYTES,
                                received + MSG_BYTES, NULL, NULL, errloc);

  {
    char buf[256];
    int off = snprintf (buf, sizeof buf, "decode returned n_recovered=%d, errloc=[",
                        n_recovered);
    for (int i = 0; i < n_recovered && i < BCH_T; i++)
      off += snprintf (buf + off, sizeof buf - off, "%s%u",
                       i ? "," : "", errloc[i]);
    snprintf (buf + off, sizeof buf - off, "]");
    hegel_note (tc, buf);
  }
  if (n_recovered < 0) {
    /* bch_decode returns -EBADMSG on uncorrectable, -EINVAL on bad input.
    ** With <=t errors injected from a valid codeword, this MUST succeed. */
    HEGEL_ASSERT (0, "bch_decode failed with %d for %d injected errors",
                  n_recovered, n_errors);
  }
  HEGEL_ASSERT (n_recovered == n_errors,
                "wrong recovered count: got %d, expected %d",
                n_recovered, n_errors);

  /* --- 5. Apply recovered errors and check ------------------------- */
  for (int i = 0; i < n_recovered; i++) {
    HEGEL_ASSERT (errloc[i] < cw_bits,
                  "errloc[%d]=%u out of range %u", i, errloc[i], cw_bits);
    flip_bit (received, errloc[i]);
  }

  /* The recovered errloc set must EQUAL the injected err_pos set. */
  for (int i = 0; i < n_errors; i++) {
    int found = 0;
    for (int j = 0; j < n_recovered; j++)
      if (errloc[j] == err_pos[i]) { found = 1; break; }
    HEGEL_ASSERT (found,
                  "injected pos %u not in recovered set", err_pos[i]);
  }

  HEGEL_ASSERT (memcmp (received, original, cw_bytes) == 0,
                "did not recover original codeword (msg_bytes=%u, "
                "ecc_bytes=%u, n_errors=%d)",
                MSG_BYTES, ecc_bytes, n_errors);

  free (ecc);
  free (original);
  free (received);
  bch_free (bch);
}

int main (void) { hegel_run_test_n (test_bch, 200); }
