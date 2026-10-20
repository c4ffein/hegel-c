/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Property: lzo1x_decompress_safe(lzo1x_1_compress(x)) == x for any byte
** sequence x of length 1..MAX_INPUT.  Mixed entropy patterns (uniform random
** bytes, low-entropy {0..3}, all-zeros, periodic).  Compresses real
** lib/lzo/lzo1x_compress.c + lib/lzo/lzo1x_decompress_safe.c into userspace
** under ASAN+UBSAN — sanitizers catch OOB reads/writes even when the
** round-trip looks clean.
**
** Negative-control test_lzo_corrupt_byte: corrupt one byte of the compressed
** stream and verify decompress_safe either returns an error OR produces
** different output — it must NOT silently produce the original input (a
** false-PASS detector for the round-trip property). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sanitizer/lsan_interface.h>
#include <linux/lzo.h>
#include "hegel_c.h"

/* LZO has no hard kernel-side input cap; lzo1x_worst_compress(x) bounds the
** output buffer at x + x/16 + 64 + 5.  We cap input at 64 KiB to keep
** per-case wall time reasonable and stay clear of size_t/int corner cases on
** the worst-compress estimate. */
#define MAX_INPUT (64u * 1024u)

/* Pattern mix for input data — biases toward inputs that exercise distinct
** code paths in the compressor. */
enum input_pattern {
  PAT_UNIFORM_RANDOM = 0,   /* dense / incompressible */
  PAT_LOW_ENTROPY    = 1,   /* {0..3} — heavy match coverage, RLE region */
  PAT_ALL_ZEROS      = 2,   /* zero-run path */
  PAT_PERIODIC       = 3,   /* short period — back-references at fixed offset */
  N_PATTERNS         = 4
};

static void fill_input (hegel_testcase *tc, uint8_t *buf, size_t n,
                        enum input_pattern pat)
{
  switch (pat) {
  case PAT_UNIFORM_RANDOM:
    for (size_t i = 0; i < n; i++)
      buf[i] = (uint8_t) hegel_draw_int (tc, 0, 255);
    break;
  case PAT_LOW_ENTROPY:
    for (size_t i = 0; i < n; i++)
      buf[i] = (uint8_t) hegel_draw_int (tc, 0, 3);
    break;
  case PAT_ALL_ZEROS:
    memset (buf, 0, n);
    break;
  case PAT_PERIODIC: {
    int period = hegel_draw_int (tc, 1, 16);
    uint8_t seed[16];
    for (int i = 0; i < period; i++)
      seed[i] = (uint8_t) hegel_draw_int (tc, 0, 255);
    for (size_t i = 0; i < n; i++)
      buf[i] = seed[i % (size_t) period];
    break;
  }
  default:
    /* unreachable */
    abort ();
  }
}

void test_lzo_round_trip (hegel_testcase *tc)
{
  size_t in_len = (size_t) hegel_draw_usize (tc, 1, MAX_INPUT);
  enum input_pattern pat =
      (enum input_pattern) hegel_draw_int (tc, 0, N_PATTERNS - 1);

  uint8_t *in = malloc (in_len);
  HEGEL_ASSERT (in != NULL, "malloc(%zu) for input failed", in_len);
  fill_input (tc, in, in_len, pat);

  size_t out_cap = lzo1x_worst_compress (in_len);
  uint8_t *compressed = malloc (out_cap);
  HEGEL_ASSERT (compressed != NULL, "malloc(%zu) for compressed failed", out_cap);

  /* wrkmem must be aligned for lzo_dict_t (unsigned short) — malloc is
  ** already 8-byte aligned, so no extra alignment work is needed. */
  void *wrkmem = malloc (LZO1X_1_MEM_COMPRESS);
  HEGEL_ASSERT (wrkmem != NULL, "malloc(%zu) for wrkmem failed",
                (size_t) LZO1X_1_MEM_COMPRESS);

  size_t compressed_len = 0;
  int err = lzo1x_1_compress (in, in_len, compressed, &compressed_len, wrkmem);
  HEGEL_ASSERT (err == LZO_E_OK,
                "lzo1x_1_compress: err=%d in_len=%zu pat=%d",
                err, in_len, (int) pat);
  HEGEL_ASSERT (compressed_len <= out_cap,
                "compressed_len=%zu exceeds worst_compress(%zu)=%zu",
                compressed_len, in_len, out_cap);

  /* For decompression, the caller passes the buffer's capacity in *out_len
  ** (the kernel decompressor uses it as an overrun guard).  Set it to
  ** in_len: the round-trip output must fit exactly. */
  uint8_t *decompressed = malloc (in_len);
  HEGEL_ASSERT (decompressed != NULL, "malloc(%zu) for decompressed failed",
                in_len);
  size_t decompressed_len = in_len;

  err = lzo1x_decompress_safe (compressed, compressed_len,
                               decompressed, &decompressed_len);
  HEGEL_ASSERT (err == LZO_E_OK,
                "lzo1x_decompress_safe: err=%d in_len=%zu c_len=%zu pat=%d",
                err, in_len, compressed_len, (int) pat);
  HEGEL_ASSERT (decompressed_len == in_len,
                "decompressed_len=%zu != in_len=%zu (pat=%d, c_len=%zu)",
                decompressed_len, in_len, (int) pat, compressed_len);

  if (memcmp (in, decompressed, in_len) != 0) {
    /* Find first mismatch for diagnostic. */
    size_t mismatch = 0;
    while (mismatch < in_len && in[mismatch] == decompressed[mismatch])
      mismatch++;
    HEGEL_ASSERT (0,
                  "round-trip mismatch at byte %zu: in=0x%02x out=0x%02x "
                  "(in_len=%zu, c_len=%zu, pat=%d)",
                  mismatch, in[mismatch], decompressed[mismatch],
                  in_len, compressed_len, (int) pat);
  }

  free (in);
  free (compressed);
  free (wrkmem);
  free (decompressed);
}

/* -------------------------------------------------------------------- *
**  Negative control: verify decompress actually exercises the decoder
**  by ensuring a corrupted compressed stream does NOT silently produce
**  the original input.  Run as a sanity check from main(), not a hegel
**  property — it's a one-shot guard. */
static int neg_control_verify (void)
{
  /* A non-trivial input that compresses meaningfully. */
  const size_t in_len = 4096;
  uint8_t *in = malloc (in_len);
  for (size_t i = 0; i < in_len; i++) in[i] = (uint8_t) (i & 0x07);

  size_t out_cap = lzo1x_worst_compress (in_len);
  uint8_t *compressed = malloc (out_cap);
  void *wrkmem = malloc (LZO1X_1_MEM_COMPRESS);

  size_t compressed_len = 0;
  int err = lzo1x_1_compress (in, in_len, compressed, &compressed_len, wrkmem);
  if (err != LZO_E_OK) {
    fprintf (stderr, "[neg-control] compress failed: %d\n", err);
    return 1;
  }
  if (compressed_len < 4) {
    fprintf (stderr, "[neg-control] compressed_len too small: %zu\n",
             compressed_len);
    return 1;
  }

  /* Corrupt several positions in the middle of the compressed stream — at
  ** least one of them must produce either an error or a mismatching
  ** decompression.  If ALL of them silently yield the original, the
  ** decompressor is not actually being exercised — false-PASS. */
  size_t silent_match = 0;
  size_t any_err = 0;
  size_t different_output = 0;

  for (size_t pos = 1; pos + 1 < compressed_len; pos += compressed_len / 8 + 1) {
    uint8_t saved = compressed[pos];
    compressed[pos] ^= 0xff;

    uint8_t *decompressed = malloc (in_len);
    size_t decompressed_len = in_len;
    int derr = lzo1x_decompress_safe (compressed, compressed_len,
                                      decompressed, &decompressed_len);
    if (derr != LZO_E_OK) {
      any_err++;
    } else if (decompressed_len != in_len ||
               memcmp (in, decompressed, in_len) != 0) {
      different_output++;
    } else {
      silent_match++;
    }
    free (decompressed);
    compressed[pos] = saved;
  }

  fprintf (stderr,
           "[neg-control] corruptions tried=%zu  err=%zu  diff=%zu  silent=%zu\n",
           any_err + different_output + silent_match,
           any_err, different_output, silent_match);

  free (in); free (compressed); free (wrkmem);

  if (any_err + different_output == 0) {
    fprintf (stderr, "[neg-control] FAILED: every corruption silently "
             "decompressed back to the original — decoder not exercised\n");
    return 1;
  }
  return 0;
}

/* -------------------------------------------------------------------- *
**  Coverage stats — process-global counters bumped during a nofork pass
**  so we can report what was actually exercised.  In fork mode each case
**  runs in a child, so cross-case accumulation doesn't survive — which is
**  why the second pass is needed for visibility. */
static long g_total_bytes_compressed;
static long g_total_bytes_round_tripped;
static long g_max_input_seen;
static long g_min_input_seen = (long) MAX_INPUT;
static long g_pat_count[N_PATTERNS];
static long g_n_cases;

static void test_lzo_round_trip_with_stats (hegel_testcase *tc)
{
  size_t in_len = (size_t) hegel_draw_usize (tc, 1, MAX_INPUT);
  enum input_pattern pat =
      (enum input_pattern) hegel_draw_int (tc, 0, N_PATTERNS - 1);

  uint8_t *in = malloc (in_len);
  HEGEL_ASSERT (in != NULL, "malloc(%zu) failed", in_len);
  fill_input (tc, in, in_len, pat);

  size_t out_cap = lzo1x_worst_compress (in_len);
  uint8_t *compressed = malloc (out_cap);
  void *wrkmem = malloc (LZO1X_1_MEM_COMPRESS);

  size_t compressed_len = 0;
  int err = lzo1x_1_compress (in, in_len, compressed, &compressed_len, wrkmem);
  HEGEL_ASSERT (err == LZO_E_OK, "compress err=%d", err);

  uint8_t *decompressed = malloc (in_len);
  size_t decompressed_len = in_len;
  err = lzo1x_decompress_safe (compressed, compressed_len,
                               decompressed, &decompressed_len);
  HEGEL_ASSERT (err == LZO_E_OK, "decompress err=%d", err);
  HEGEL_ASSERT (decompressed_len == in_len, "len mismatch");
  HEGEL_ASSERT (memcmp (in, decompressed, in_len) == 0, "bytes mismatch");

  g_total_bytes_compressed += (long) compressed_len;
  g_total_bytes_round_tripped += (long) in_len;
  if ((long) in_len > g_max_input_seen) g_max_input_seen = (long) in_len;
  if ((long) in_len < g_min_input_seen) g_min_input_seen = (long) in_len;
  g_pat_count[pat]++;
  g_n_cases++;

  free (in); free (compressed); free (wrkmem); free (decompressed);
}

int main (void)
{
  /* Negative control first — if the decoder isn't actually doing work, the
  ** round-trip property is a false positive. */
  if (neg_control_verify () != 0) return 1;

  /* 500 fork-mode cases under ASAN+UBSAN.  Inputs span 1..64 KiB across
  ** four pattern families (random, low-entropy, zeros, periodic).
  ** Crash isolation means each case starts fresh — sanitizer reports
  ** surface per-case if any fire. */
  hegel_run_test_n (test_lzo_round_trip, 500);

  /* 100 nofork cases for coverage stats — accumulates in this process.
  ** Note: hegel may invoke the test more than 100 times during shrinking
  ** exploration; the leak detector sees those extra invocations as leaked
  ** allocations.  We disable leak-check on this pass since it's
  ** instrumentation-only — the round-trip property has already been
  ** validated under fork mode above. */
  __lsan_disable ();
  hegel_run_test_nofork_n (test_lzo_round_trip_with_stats, 100);
  fprintf (stderr,
           "[lzo PBT coverage] fork=500 stats_cases=%ld  total_in_bytes=%ld  "
           "total_compressed_bytes=%ld  in_len=[%ld..%ld]  "
           "patterns: random=%ld low_entropy=%ld zeros=%ld periodic=%ld\n",
           g_n_cases, g_total_bytes_round_tripped, g_total_bytes_compressed,
           g_min_input_seen, g_max_input_seen,
           g_pat_count[PAT_UNIFORM_RANDOM], g_pat_count[PAT_LOW_ENTROPY],
           g_pat_count[PAT_ALL_ZEROS], g_pat_count[PAT_PERIODIC]);
  __lsan_enable ();
  return 0;
}
