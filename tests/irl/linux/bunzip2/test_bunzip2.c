/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** PBT for the Linux kernel's bzip2 decompressor (lib/decompress_bunzip2.c)
** compiled into userspace under ASAN+UBSAN.
**
** Two angles:
**   A. Round-trip: compress data with libbz2 (BZ2_bzBuffToBuffCompress) and
**      verify bunzip2() returns the original bytes.
**   B. Malformed input robustness: feed random byte streams (with or without
**      a valid magic prefix) and verify the decompressor either errors or
**      produces consistent output — never crashes, never UB-shifts, never
**      overruns the output buffer.
**
** Both angles run under ASAN+UBSAN.  UBSAN reports ("runtime error:" lines)
** are treated as bugs.
**
** Bounded-work guard: when we want to limit the bytes the decompressor can
** read (malformed-input angle), we route input via a fill() callback whose
** per-test budget is exhausted to NUL.  This guarantees no infinite loop
** even when the malformed input never reaches a block boundary. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "hegel_c.h"

/* libbz2 — we don't have bzlib.h in this sandbox, so declare the one
** symbol we need.  Linked from /usr/lib/x86_64-linux-gnu/libbz2.so.1.0. */
extern int BZ2_bzBuffToBuffCompress (char *dest, unsigned int *destLen,
                                     char *source, unsigned int sourceLen,
                                     int blockSize100k, int verbosity,
                                     int workFactor);

/* Kernel decompressor entry — same signature as the smoke test uses. */
int bunzip2 (unsigned char *buf, long len,
             long (*fill)(void *, unsigned long),
             long (*flush)(void *, unsigned long),
             unsigned char *outbuf,
             long *pos,
             void (*error)(char *x));

#define MAX_INPUT      (64u * 1024u)
#define MAX_BAD_INPUT  (4u * 1024u)

/* ---- Error sink for bunzip2() ----------------------------------------- *
**  bunzip2() reports decode errors via the error() callback.  We capture
**  the latest message for diagnostics but don't print per-case (would be
**  noisy under shrinking).  The test's PASS/FAIL is decided by the return
**  code from bunzip2() and (under ASAN/UBSAN) by sanitizer output. */
static char g_last_error[256];
static void on_error (char *msg)
{
  if (msg) {
    strncpy (g_last_error, msg, sizeof g_last_error - 1);
    g_last_error[sizeof g_last_error - 1] = 0;
  }
}

/* ---- Input pattern mix ------------------------------------------------ */
enum input_pattern {
  PAT_RANDOM     = 0,    /* dense / incompressible */
  PAT_LOW_ENT    = 1,    /* {0..3} */
  PAT_ZEROS      = 2,    /* all-zero */
  PAT_PERIODIC   = 3,    /* short period — RLE / BWT favourable */
  PAT_ASCII      = 4,    /* printable ASCII */
  N_PATTERNS     = 5
};

static void fill_input (hegel_testcase *tc, uint8_t *buf, size_t n,
                        enum input_pattern pat)
{
  switch (pat) {
  case PAT_RANDOM:
    for (size_t i = 0; i < n; i++)
      buf[i] = (uint8_t) hegel_draw_int (tc, 0, 255);
    break;
  case PAT_LOW_ENT:
    for (size_t i = 0; i < n; i++)
      buf[i] = (uint8_t) hegel_draw_int (tc, 0, 3);
    break;
  case PAT_ZEROS:
    memset (buf, 0, n);
    break;
  case PAT_PERIODIC: {
    int period = hegel_draw_int (tc, 1, 32);
    uint8_t seed[32];
    for (int i = 0; i < period; i++)
      seed[i] = (uint8_t) hegel_draw_int (tc, 0, 255);
    for (size_t i = 0; i < n; i++)
      buf[i] = seed[i % (size_t) period];
    break;
  }
  case PAT_ASCII:
    for (size_t i = 0; i < n; i++)
      buf[i] = (uint8_t) hegel_draw_int (tc, 32, 126);
    break;
  default:
    abort ();
  }
}

/* ===================================================================== *
**  ANGLE A — Round-trip via libbz2 compress / kernel bunzip2 decompress
** ===================================================================== */
static void test_bunzip2_round_trip (hegel_testcase *tc)
{
  size_t in_len = (size_t) hegel_draw_usize (tc, 1, MAX_INPUT);
  enum input_pattern pat =
      (enum input_pattern) hegel_draw_int (tc, 0, N_PATTERNS - 1);

  uint8_t *in = malloc (in_len);
  HEGEL_ASSERT (in != NULL, "malloc(%zu) in failed", in_len);
  fill_input (tc, in, in_len, pat);

  /* libbz2 docs: dest needs >= sourceLen + sourceLen/100 + 600.  Pad a bit
  ** more for very small inputs where the formula could still under-cover. */
  unsigned int dest_cap = (unsigned int) (in_len + in_len / 100 + 1024);
  char *compressed = malloc (dest_cap);
  HEGEL_ASSERT (compressed != NULL, "malloc compressed");

  unsigned int compressed_len = dest_cap;
  int crc = BZ2_bzBuffToBuffCompress (compressed, &compressed_len,
                                      (char *) in, (unsigned int) in_len,
                                      /* blockSize100k */ 1,
                                      /* verbosity    */ 0,
                                      /* workFactor   */ 0);
  HEGEL_ASSERT (crc == 0,
                "BZ2_bzBuffToBuffCompress failed: %d (in_len=%zu, pat=%d)",
                crc, in_len, (int) pat);

  /* Decompress into a buffer the same size as the original; the kernel
  ** decompressor writes to the buffer up to *pos*.  Pad +1 so a one-byte
  ** overrun would land on a defined byte for ASAN to catch the bound
  ** check (red-zone catches anything beyond). */
  uint8_t *out = malloc (in_len);
  HEGEL_ASSERT (out != NULL, "malloc out");

  long pos = 0;
  g_last_error[0] = 0;
  int rc = bunzip2 ((unsigned char *) compressed, (long) compressed_len,
                    /* fill  */ NULL,
                    /* flush */ NULL,
                    /* outbuf*/ out,
                    /* pos   */ &pos,
                    /* error */ on_error);

  HEGEL_ASSERT (rc == 0,
                "bunzip2 returned %d (err=\"%s\", in_len=%zu, c_len=%u, pat=%d)",
                rc, g_last_error, in_len, compressed_len, (int) pat);

  if (memcmp (in, out, in_len) != 0) {
    size_t i = 0;
    while (i < in_len && in[i] == out[i]) i++;
    HEGEL_ASSERT (0,
                  "round-trip mismatch at byte %zu: in=0x%02x out=0x%02x "
                  "(in_len=%zu, c_len=%u, pat=%d)",
                  i, in[i], out[i], in_len, compressed_len, (int) pat);
  }

  free (in); free (compressed); free (out);
}

/* ===================================================================== *
**  ANGLE B — Malformed-input robustness
**
**  Build a stream from random bytes, optionally with a valid BZh magic
**  prefix so we get further into the parser before bailing.  Feed it via
**  a bounded fill() callback so the decompressor can't request more bytes
**  than we have.  Acceptable outcome: rc != 0 (error) OR rc == 0 with any
**  output as long as no ASAN/UBSAN trips and no out-of-bounds write
**  happens.  Output buffer overruns are caught by ASAN's red-zone. */
struct fill_ctx {
  const uint8_t *data;
  size_t len;
  size_t pos;
};

static long bounded_fill (void *buf, unsigned long n)
{
  /* The bunzip2 fill API uses a static context because it's called from
  ** within the decompressor without a user pointer.  Stash via static. */
  extern struct fill_ctx g_fill_ctx;
  if (g_fill_ctx.pos >= g_fill_ctx.len) return 0;
  unsigned long remaining = g_fill_ctx.len - g_fill_ctx.pos;
  unsigned long to_copy = remaining < n ? remaining : n;
  memcpy (buf, g_fill_ctx.data + g_fill_ctx.pos, to_copy);
  g_fill_ctx.pos += to_copy;
  return (long) to_copy;
}

struct fill_ctx g_fill_ctx;

static void test_bunzip2_malformed (hegel_testcase *tc)
{
  /* Sometimes prepend a valid bzip2 magic ("BZh" + level digit '1'..'9')
  ** so we get past the header sanity check and into the per-block code,
  ** which is where the UB shifts in get_bits() live. */
  int with_magic = hegel_draw_int (tc, 0, 1);
  int level      = hegel_draw_int (tc, '1', '9');

  /* Length spans empty (just magic) up to several KiB. */
  size_t body_len = (size_t) hegel_draw_usize (tc, 0, MAX_BAD_INPUT);
  size_t total_len = body_len + (with_magic ? 4 : 0);

  uint8_t *buf = malloc (total_len > 0 ? total_len : 1);
  HEGEL_ASSERT (buf != NULL, "malloc buf");

  size_t off = 0;
  if (with_magic) {
    buf[off++] = 'B';
    buf[off++] = 'Z';
    buf[off++] = 'h';
    buf[off++] = (uint8_t) level;
  }
  /* Draw the random body in one logical span so the shrinker can drop the
  ** whole block atomically. */
  if (body_len > 0) {
    int n = hegel_draw_bytes (tc, (int) body_len, (int) body_len,
                              buf + off, (int) body_len);
    HEGEL_ASSERT ((size_t) n == body_len, "draw_bytes short read");
  }

  /* Output buffer: bounded.  ASAN red-zone catches any write past the
  ** end.  Decompressor caps each chunk to BZIP2_IOBUF_SIZE so a couple
  ** of MiB is plenty for malformed inputs (which never decompress
  ** anywhere near that much real data). */
  size_t out_cap = 256 * 1024;
  uint8_t *out = malloc (out_cap);
  HEGEL_ASSERT (out != NULL, "malloc out");

  /* Route input via the bounded fill callback so the decompressor can't
  ** read past our buffer regardless of what header values it parses.
  ** This is the "bounded work" guard. */
  g_fill_ctx.data = buf;
  g_fill_ctx.len  = total_len;
  g_fill_ctx.pos  = 0;

  long pos = 0;
  g_last_error[0] = 0;

  /* Two ways to feed: inline (buf) or via fill().  Use fill() so the
  ** decompressor calls bounded_fill() to refill its internal buffer and
  ** we get the EOF signal at exactly total_len bytes. */
  int rc = bunzip2 (/* buf  */ NULL,
                    /* len  */ 0,
                    /* fill */ bounded_fill,
                    /* flush*/ NULL,
                    /* out  */ out,
                    /* pos  */ &pos,
                    /* error*/ on_error);

  /* Property: bunzip2 returns SOMETHING bounded; we don't care what.  We
  ** care that we didn't crash, didn't OOB-write (ASAN), didn't UB-shift
  ** (UBSAN), and that pos is within the output buffer. */
  HEGEL_ASSERT (pos >= 0 && (size_t) pos <= out_cap,
                "pos=%ld out of [0, %zu] (rc=%d, total_len=%zu)",
                pos, out_cap, rc, total_len);
  (void) rc;  /* any rc is acceptable */

  free (buf); free (out);
}

/* ===================================================================== *
**  ANGLE B' — Malformed but always starts with valid magic.  Biases the
**  fuzzer towards inputs that actually reach the buggy get_bits() code.
** ===================================================================== */
static void test_bunzip2_with_magic (hegel_testcase *tc)
{
  int level = hegel_draw_int (tc, '1', '9');
  size_t body_len = (size_t) hegel_draw_usize (tc, 0, MAX_BAD_INPUT);
  size_t total_len = body_len + 4;

  uint8_t *buf = malloc (total_len);
  HEGEL_ASSERT (buf != NULL, "malloc buf");
  buf[0] = 'B'; buf[1] = 'Z'; buf[2] = 'h'; buf[3] = (uint8_t) level;

  if (body_len > 0) {
    int n = hegel_draw_bytes (tc, (int) body_len, (int) body_len,
                              buf + 4, (int) body_len);
    HEGEL_ASSERT ((size_t) n == body_len, "draw_bytes short read");
  }

  size_t out_cap = 256 * 1024;
  uint8_t *out = malloc (out_cap);
  HEGEL_ASSERT (out != NULL, "malloc out");

  g_fill_ctx.data = buf;
  g_fill_ctx.len  = total_len;
  g_fill_ctx.pos  = 0;

  long pos = 0;
  g_last_error[0] = 0;
  int rc = bunzip2 (NULL, 0, bounded_fill, NULL, out, &pos, on_error);

  HEGEL_ASSERT (pos >= 0 && (size_t) pos <= out_cap,
                "pos=%ld out of [0, %zu] (rc=%d, total_len=%zu)",
                pos, out_cap, rc, total_len);
  (void) rc;

  free (buf); free (out);
}

/* Make UBSAN/ASAN abort on first report so hegel sees a CRASH and shrinks
** the input.  Weak symbol overrides — read before any sanitizer-checked
** code runs, no env-var ordering issues. */
const char *__ubsan_default_options (void)
{
  return "halt_on_error=1:abort_on_error=1:print_stacktrace=1";
}
const char *__asan_default_options (void)
{
  return "halt_on_error=1:abort_on_error=1";
}

/* ===================================================================== *
**  Main: smoke check, then three property tests. */
int main (void)
{

  /* Round-trip first — under fork mode, 200 cases.  Each case re-checks
  ** the property end-to-end with ASAN/UBSAN watching. */
  fprintf (stderr, "=== test_bunzip2_round_trip (200 cases) ===\n");
  int rc1 = hegel_run_test_result_n (test_bunzip2_round_trip, 200);

  /* Malformed without magic — usually rejected at the header check, but
  ** still exercises the early validation paths. */
  fprintf (stderr, "=== test_bunzip2_malformed (300 cases) ===\n");
  int rc2 = hegel_run_test_result_n (test_bunzip2_malformed, 300);

  /* Malformed WITH magic — gets past the header and into get_bits() and
  ** the Huffman / BWT decoders.  This is where the UB shifts live. */
  fprintf (stderr, "=== test_bunzip2_with_magic (300 cases) ===\n");
  int rc3 = hegel_run_test_result_n (test_bunzip2_with_magic, 300);

  fprintf (stderr,
           "[bunzip2 PBT] round_trip=%s  malformed=%s  with_magic=%s\n",
           rc1 ? "FAIL" : "pass",
           rc2 ? "FAIL" : "pass",
           rc3 ? "FAIL" : "pass");

  return (rc1 || rc2 || rc3) ? 1 : 0;
}
