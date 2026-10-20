/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Thorough PBT for the (snapshotted, buggy) Linux lib/bch.c.
**
** Properties exercised:
**   1. bch_init never invokes UB for any valid (m, t).  UBSAN is configured
**      to abort on report — abort is caught by hegel's fork-mode wrapper
**      and shrinks to a minimal (m, t).  This is the line that the
**      010d7d984650 commit fixed: `data = i << (8*b)` is UB for i>=128, b=3
**      because i is a signed int.
**   2. bch_encode is deterministic — same (data, len) -> same ECC bytes.
**   3. Round-trip: encode -> flip k <= t distinct bits -> decode -> the
**      decoder reports exactly k errors AND we can use errloc[] to
**      recover the original data.
**   4. Decode handles >t errors gracefully (returns negative or returns
**      a "found" count but the corrected codeword doesn't match original;
**      either way: no crash, no UB).
**   5. Decode of an uncorrupted codeword returns 0 errors.
**
** UBSAN env (set in main): halt_on_error=1:abort_on_error=1 — guarantees
** that any UB report becomes SIGABRT, which fork mode converts to a
** shrinkable failure.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/bch.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ------------------------------------------------------------------- */
/* (m, t) sweep helpers                                                */
/* ------------------------------------------------------------------- */

/* bch_init requires m in [5,15], t >= 1, t <= 64, and m*t < (1<<m)-1.
** To keep individual cases fast (each one allocates a mod8 table of size
** words*1024*4 bytes ≈ m*t*128 bytes), cap m at 10 here; the dedicated
** "no-UB" sweep below covers wider m. */
static int
draw_valid_mt (hegel_testcase *tc, int m_max, int *out_m, int *out_t)
{
  int m = HEGEL_DRAW_INT (5, m_max);
  /* upper bound for t so m*t < (1<<m)-1 */
  int t_hi = (((1 << m) - 1) - 1) / m;
  if (t_hi < 1)
    {
      hegel_health_fail ("no valid t for chosen m");
      return -1;
    }
  if (t_hi > 32) t_hi = 32;           /* keep tables small for round-trip */
  int t = HEGEL_DRAW_INT (1, t_hi);
  *out_m = m;
  *out_t = t;
  return 0;
}

/* ------------------------------------------------------------------- */
/* Property 1 — bch_init never invokes UB on any valid (m, t).         */
/* ------------------------------------------------------------------- */

static void
test_init_no_ub (hegel_testcase *tc)
{
  /* Sweep the full documented range of m, capped to 12 to keep allocation
  ** within ~MB and the run within ~100ms per case. */
  int m = HEGEL_DRAW_INT (5, 12);
  int t_hi = (((1 << m) - 1) - 1) / m;
  if (t_hi < 1) { hegel_health_fail ("no valid t for chosen m"); return; }
  if (t_hi > 32) t_hi = 32;
  int t = HEGEL_DRAW_INT (1, t_hi);

  char note[64];
  snprintf (note, sizeof note, "m=%d t=%d", m, t);
  hegel_note (tc, note);

  struct bch_control *bch = bch_init (m, t, 0, false);
  /* bch_init() can legitimately return NULL for OOM; we accept that. */
  if (bch == NULL) { hegel_assume (tc, 0); return; }
  /* The actual UB check is on UBSAN — if abort_on_error is set, the run
  ** dies before we get here.  Pin the success path. */
  HEGEL_ASSERT (bch->m == (unsigned)m, "bch->m mismatch");
  HEGEL_ASSERT (bch->t == (unsigned)t, "bch->t mismatch");
  bch_free (bch);
}

/* ------------------------------------------------------------------- */
/* Property 2 — bch_encode is deterministic.                            */
/* ------------------------------------------------------------------- */

static void
test_encode_deterministic (hegel_testcase *tc)
{
  int m, t;
  if (draw_valid_mt (tc, 9, &m, &t) < 0) return;

  unsigned int n = (1u << m) - 1;
  unsigned int ecc_bits = (unsigned)m * (unsigned)t;
  if (ecc_bits >= n) { hegel_assume (tc, 0); return; }
  unsigned int data_bits = n - ecc_bits;
  unsigned int data_max  = data_bits / 8;  /* whole bytes */
  if (data_max < 1) { hegel_assume (tc, 0); return; }
  if (data_max > 256) data_max = 256;

  int len = HEGEL_DRAW_INT (1, (int)data_max);
  uint8_t data[256];
  for (int i = 0; i < len; i++) data[i] = (uint8_t)HEGEL_DRAW_INT (0, 255);

  struct bch_control *bch = bch_init (m, t, 0, false);
  if (!bch) { hegel_assume (tc, 0); return; }

  unsigned int ecc_bytes = bch->ecc_bytes;
  uint8_t ecc1[64] = {0};
  uint8_t ecc2[64] = {0};
  if (ecc_bytes > sizeof ecc1) { bch_free (bch); hegel_assume (tc, 0); return; }

  bch_encode (bch, data, (unsigned)len, ecc1);
  bch_encode (bch, data, (unsigned)len, ecc2);
  HEGEL_ASSERT (memcmp (ecc1, ecc2, ecc_bytes) == 0,
                "encode not deterministic (m=%d t=%d len=%d)", m, t, len);
  bch_free (bch);
}

/* ------------------------------------------------------------------- */
/* Property 3 — Round-trip: inject k<=t errors, decode recovers them.   */
/* ------------------------------------------------------------------- */

static void
inject_errors (uint8_t *data, unsigned int len, uint8_t *ecc,
               unsigned int ecc_bytes, int k, const int *positions)
{
  (void)ecc_bytes;
  /* Each position is a bit index in [0, 8*(len+ecc_bytes)).  Lower bits
  ** are data, upper are ecc.  This matches the layout documented in the
  ** bch_decode header: "if (errloc[n] >= 8*len), then n-th error is in
  ** ecc". */
  unsigned int data_bits = 8u * len;
  for (int i = 0; i < k; i++)
    {
      unsigned int bit = (unsigned)positions[i];
      if (bit < data_bits)
        data[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      else
        {
          /* ecc bits are stored MSB-first; the last byte's low bits are
          ** padding, which the decoder (rightly) never sees. */
          unsigned int eb = bit - data_bits;
          ecc[eb / 8] ^= (uint8_t)(0x80u >> (eb % 8));
        }
    }
}

static void
test_decode_recovers (hegel_testcase *tc)
{
  int m, t;
  if (draw_valid_mt (tc, 9, &m, &t) < 0) return;

  unsigned int n = (1u << m) - 1;
  unsigned int ecc_bits = (unsigned)m * (unsigned)t;
  if (ecc_bits >= n) { hegel_assume (tc, 0); return; }
  unsigned int data_bits = n - ecc_bits;
  unsigned int data_max  = data_bits / 8;
  if (data_max < 1) { hegel_assume (tc, 0); return; }
  if (data_max > 128) data_max = 128;

  int len = HEGEL_DRAW_INT (1, (int)data_max);
  uint8_t data[128];
  uint8_t data_orig[128];
  for (int i = 0; i < len; i++) data[i] = (uint8_t)HEGEL_DRAW_INT (0, 255);
  memcpy (data_orig, data, (size_t)len);

  struct bch_control *bch = bch_init (m, t, 0, false);
  if (!bch) { hegel_assume (tc, 0); return; }

  unsigned int ecc_bytes = bch->ecc_bytes;
  if (ecc_bytes > 64) { bch_free (bch); hegel_assume (tc, 0); return; }
  uint8_t ecc[64] = {0};
  bch_encode (bch, data, (unsigned)len, ecc);

  /* Choose 0..t distinct bit positions in [0, 8*len + bch->ecc_bits). */
  /* Real ecc bits only, not the padding of its last byte.  The code's real
  ** ecc size is the generator polynomial's degree, bch->ecc_bits, which can
  ** be below m*t when minimal polynomials repeat. */
  unsigned int total_bits = 8u * (unsigned)len + bch->ecc_bits;
  int k = HEGEL_DRAW_INT (0, t);

  int positions[64];
  /* Distinct via reservoir-style: draw, reject duplicates with assume.
  ** Hegel can shrink this away if it's the wrong path; for small k this
  ** is OK.  Keep k small enough that rejection rate is bounded. */
  for (int i = 0; i < k; i++)
    {
      int p = HEGEL_DRAW_INT (0, (int)total_bits - 1);
      for (int j = 0; j < i; j++)
        {
          if (positions[j] == p) { hegel_assume (tc, 0); goto cleanup; }
        }
      positions[i] = p;
    }

  inject_errors (data, (unsigned)len, ecc, ecc_bytes, k, positions);

  unsigned int errloc[64] = {0};
  int found = bch_decode (bch, data, (unsigned)len, ecc, NULL, NULL, errloc);

  HEGEL_ASSERT (found == k,
                "decode found %d errors, expected %d (m=%d t=%d k=%d len=%d)",
                found, k, m, t, k, len);

  /* Apply the corrections and verify recovery. */
  for (int i = 0; i < found; i++)
    {
      unsigned int loc = errloc[i];
      if (loc < 8u * (unsigned)len)
        data[loc / 8] ^= (uint8_t)(1u << (loc % 8));
      else
        {
          unsigned int eb = loc - 8u * (unsigned)len;
          if (eb / 8 < ecc_bytes)
            ecc[eb / 8] ^= (uint8_t)(1u << (eb % 8));
        }
    }
  HEGEL_ASSERT (memcmp (data, data_orig, (size_t)len) == 0,
                "data not recovered (m=%d t=%d k=%d len=%d)", m, t, k, len);

cleanup:
  bch_free (bch);
}

/* ------------------------------------------------------------------- */
/* Property 4 — Decode of clean codeword returns 0 errors.              */
/* ------------------------------------------------------------------- */

static void
test_decode_clean_zero (hegel_testcase *tc)
{
  int m, t;
  if (draw_valid_mt (tc, 9, &m, &t) < 0) return;

  unsigned int n = (1u << m) - 1;
  unsigned int ecc_bits = (unsigned)m * (unsigned)t;
  if (ecc_bits >= n) { hegel_assume (tc, 0); return; }
  unsigned int data_max = (n - ecc_bits) / 8;
  if (data_max < 1) { hegel_assume (tc, 0); return; }
  if (data_max > 128) data_max = 128;

  int len = HEGEL_DRAW_INT (1, (int)data_max);
  uint8_t data[128];
  for (int i = 0; i < len; i++) data[i] = (uint8_t)HEGEL_DRAW_INT (0, 255);

  struct bch_control *bch = bch_init (m, t, 0, false);
  if (!bch) { hegel_assume (tc, 0); return; }

  if (bch->ecc_bytes > 64) { bch_free (bch); hegel_assume (tc, 0); return; }
  uint8_t ecc[64] = {0};
  bch_encode (bch, data, (unsigned)len, ecc);

  unsigned int errloc[64];
  int found = bch_decode (bch, data, (unsigned)len, ecc, NULL, NULL, errloc);
  HEGEL_ASSERT (found == 0, "decode of clean codeword returned %d errors", found);
  bch_free (bch);
}

/* ------------------------------------------------------------------- */
/* Property 5 — Decode does not crash on >t errors.                     */
/* ------------------------------------------------------------------- */

static void
test_decode_overcorrupt_no_crash (hegel_testcase *tc)
{
  int m, t;
  if (draw_valid_mt (tc, 9, &m, &t) < 0) return;

  unsigned int n = (1u << m) - 1;
  unsigned int ecc_bits = (unsigned)m * (unsigned)t;
  if (ecc_bits >= n) { hegel_assume (tc, 0); return; }
  unsigned int data_max = (n - ecc_bits) / 8;
  if (data_max < 1) { hegel_assume (tc, 0); return; }
  if (data_max > 64) data_max = 64;

  int len = HEGEL_DRAW_INT (1, (int)data_max);
  uint8_t data[64];
  for (int i = 0; i < len; i++) data[i] = (uint8_t)HEGEL_DRAW_INT (0, 255);

  struct bch_control *bch = bch_init (m, t, 0, false);
  if (!bch) { hegel_assume (tc, 0); return; }

  if (bch->ecc_bytes > 64) { bch_free (bch); hegel_assume (tc, 0); return; }
  uint8_t ecc[64] = {0};
  bch_encode (bch, data, (unsigned)len, ecc);

  /* Flip a random number of bits with no distinctness constraint —
  ** likely to exceed t and create an uncorrectable codeword. */
  unsigned int total_bits = 8u * ((unsigned)len + bch->ecc_bytes);
  int k = HEGEL_DRAW_INT (t + 1, t + 1 + 32);
  if ((unsigned)k > total_bits) k = (int)total_bits;
  for (int i = 0; i < k; i++)
    {
      int p = HEGEL_DRAW_INT (0, (int)total_bits - 1);
      if ((unsigned)p < 8u * (unsigned)len)
        data[p / 8] ^= (uint8_t)(1u << (p % 8));
      else
        {
          unsigned int eb = (unsigned)p - 8u * (unsigned)len;
          ecc[eb / 8] ^= (uint8_t)(1u << (eb % 8));
        }
    }

  unsigned int errloc[64];
  int found = bch_decode (bch, data, (unsigned)len, ecc, NULL, NULL, errloc);
  /* Either a negative error (-EBADMSG/-EINVAL) or a found count.  No
  ** crash, no UB.  Anything in [-EINVAL, t] is acceptable. */
  (void)found;
  bch_free (bch);
}

/* ------------------------------------------------------------------- */
/* main                                                                */
/* ------------------------------------------------------------------- */

int
main (void)
{
  /* Make UBSAN abort so fork mode catches it and shrinks. */
  setenv ("UBSAN_OPTIONS", "halt_on_error=1:abort_on_error=1:print_stacktrace=1", 1);
  /* ASAN already aborts on error by default in fork mode; be explicit. */
  setenv ("ASAN_OPTIONS", "abort_on_error=1", 1);

  hegel_suite *s = hegel_suite_new ();
  hegel_suite_add (s, "init_no_ub",                 test_init_no_ub);
  hegel_suite_add (s, "encode_deterministic",       test_encode_deterministic);
  hegel_suite_add (s, "decode_recovers",            test_decode_recovers);
  hegel_suite_add (s, "decode_clean_zero",          test_decode_clean_zero);
  hegel_suite_add (s, "decode_overcorrupt_no_crash",test_decode_overcorrupt_no_crash);
  int rc = hegel_suite_run (s);
  hegel_suite_free (s);
  return rc;
}
