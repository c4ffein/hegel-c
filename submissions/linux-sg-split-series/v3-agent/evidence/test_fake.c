/* sg_split() on fake-page inputs (sgfake.h): values a real-memory harness can't reach.
**
**   make bin T=work/v3x/test_fake.c SRC=<sg_split.c> OUT=work/v3x/bin/fake   (+ NEED CFLAGS, SAN=1)
**   SG_EXEC32=work/v3x/bin/exec32 ./work/v3x/bin/fake [cases] [name]
**
** P-HUGE   entry lengths up to UINT_MAX (around 2^31 and 2^32-1 on purpose), address gaps
**          up to 4 GiB, totals past 4 GiB, skip and split sizes on and around those
**          values, negative skips.  Valid request (0 <= skip, skip + sum(sizes) <= total,
**          computed without overflow) -> rc 0 and every split covers exactly its stream
**          range, CPU and DMA view (the v3 contract: a zero-size split is one zero-length
**          entry, a non-empty split starts with an entry that has bytes).  Otherwise
**          -EINVAL with out[] untouched.
** P-ABI32  the same kind of input, limited to what a 32-bit kernel can express (span
**          below 3.25 GiB, skip in off_t range, sizes in size_t range), runs through a
**          32-bit (i386) build of the same sg_split.c (exec32) and must give exactly
**          the same canonical output as this 64-bit build.  Then P-HUGE's checks.
**
** DMA model (from the in-tree dma_map_sg implementations): segments are consecutive
** groups of whole entries, any grouping (an IOMMU merges non-contiguous entries),
** dma_len = sum of the group's lengths (<= UINT_MAX), zero-length entries anywhere.
** Without NEED_SG_DMA_LENGTH dma_len is ->length, so the mapping is one-to-one. */
#include "sgfake.h"
#include "hegel_c.h"
#include "hegel_gen.h"
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>

static uint64_t NCASES = 1000;
static const char *EXEC32;

/* --------------------------------------------------------------- generation */

static unsigned draw_len (hegel_testcase *tc)
{
  int b = HEGEL_DRAW_INT (0, 99);
  if (b < 35) return (unsigned) HEGEL_DRAW_INT (1, 16);
  if (b < 45) return 0;
  if (b < 55) return (unsigned) HEGEL_DRAW_INT (PAGE_SIZE - 2, PAGE_SIZE + 2);
  if (b < 65) return (unsigned) HEGEL_DRAW_INT (17, 1 << 20);
  if (b < 77) return (unsigned) HEGEL_DRAW_I64 (0x7ffffffeLL, 0x80000002LL);
  if (b < 89) return (unsigned) HEGEL_DRAW_I64 (0xfffffffcLL, 0xffffffffLL);
  return (unsigned) HEGEL_DRAW_U64 (0, UINT_MAX);
}

static unsigned long long draw_gap (hegel_testcase *tc)
{
  int g = HEGEL_DRAW_INT (0, 9);
  if (g < 4) return 0;
  if (g < 7) return (unsigned long long) HEGEL_DRAW_INT (1, 64);
  if (g < 9) return PAGE_SIZE;
  return HEGEL_DRAW_U64 (0, 1ULL << 32);
}

static long long pick_boundary (hegel_testcase *tc, const fdesc *d, long long pos)
{
  long long cand[FMAXN + 1], cum = 0;
  int m = 0;
  for (int i = 0; i <= d->n; i++) {
    if (cum >= pos) cand[m++] = cum;
    if (i < d->n) cum += d->len[i];
  }
  if (!m) return pos;
  return cand[HEGEL_DRAW_INT (0, m - 1)];
}

/* abi: keep everything expressible on a 32-bit kernel. */
static void draw_desc (hegel_testcase *tc, fdesc *d, int abi)
{
  memset (d, 0, sizeof *d);
  const unsigned long long budget = 0xd0000000ULL;   /* 32-bit address span */
  unsigned long long span;
  d->n = HEGEL_DRAW_INT (0, 9) < 7 ? HEGEL_DRAW_INT (1, 4) : HEGEL_DRAW_INT (5, 12);
  int o = HEGEL_DRAW_INT (0, 3);
  d->off0 = o == 0 ? 0 : o == 1 ? PAGE_SIZE - 1 : (unsigned) HEGEL_DRAW_INT (0, PAGE_SIZE - 1);
  span = d->off0;
  unsigned long long at = d->off0;   /* address of the next entry */
  for (int i = 0; i < d->n; i++) {
    unsigned long long l = draw_len (tc), g = draw_gap (tc);
    /* Precondition: offset + length <= UINT_MAX for every entry.  Kernel code adds the
    ** two in 32 bits; no allocator builds an entry past that (sg_alloc_table_from_pages
    ** caps a segment at UINT_MAX & PAGE_MASK).  Without it, sg_split_phys()'s
    ** `->offset += skip_sg0` wraps (found by this test, out of contract). */
    unsigned long long o = at & (PAGE_SIZE - 1);
    if (l > UINT_MAX - o) l = UINT_MAX - o;
    at += l + g;
    if (abi) {
      if (l > budget - span) l = budget - span;
      span += l;
      if (g > budget - span) g = budget - span;
      span += g;
    }
    d->len[i] = (unsigned) l;
    d->gap[i] = g;
  }
  if (d->n >= 2 && HEGEL_DRAW_INT (0, 3) == 0) {
    int want = HEGEL_DRAW_INT (1, FMAXCH < d->n - 1 ? FMAXCH : d->n - 1), prev = 0;
    for (int t = 0; t < want && prev < d->n - 1; t++) {
      int p = HEGEL_DRAW_INT (prev + 1, d->n - 1);
      d->chp[d->nch++] = p;
      prev = p;
    }
  }
  d->mapped = HEGEL_DRAW_BOOL ();
  if (d->mapped) {
    int j = 0;
    for (int i = 0; i < d->n; i++) {
      int merge = F_NEED && i > 0 && HEGEL_DRAW_INT (0, 2) == 0 &&
                  (unsigned long long) d->dlen[j - 1] + d->len[i] <= UINT_MAX;
      if (merge) d->dlen[j - 1] += d->len[i];
      else { d->dstart[j] = i; d->dlen[j] = d->len[i]; j++; }
    }
    d->ndma = j;
  }

  long long total = 0;
  for (int i = 0; i < d->n; i++) total += d->len[i];
  int sm = HEGEL_DRAW_INT (0, 11);
  switch (sm) {
  case 0: case 1: d->skip = 0; break;
  case 2: case 3: d->skip = pick_boundary (tc, d, 0); break;
  case 4: d->skip = pick_boundary (tc, d, 0) + (HEGEL_DRAW_BOOL () ? 1 : -1); break;
  case 5: d->skip = HEGEL_DRAW_I64 (0, total); break;
  case 6: d->skip = total; break;
  case 7: d->skip = total + HEGEL_DRAW_INT (1, 3); break;
  case 8: d->skip = -HEGEL_DRAW_I64 (1, 3); break;
  case 9: d->skip = HEGEL_DRAW_I64 (0x7ffffffeLL, 0x80000001LL); break;
  case 10: d->skip = HEGEL_DRAW_I64 (0xfffffffeLL, 0x100000001LL); break;
  default: d->skip = HEGEL_DRAW_BOOL () ? LLONG_MIN / 2 : -0x80000000LL; break;
  }
  if (abi) {
    if (d->skip > INT_MAX) d->skip = INT_MAX;
    if (d->skip < INT_MIN) d->skip = INT_MIN;
  }

  d->nb = HEGEL_DRAW_INT (0, 9) < 8 ? HEGEL_DRAW_INT (1, 4) : HEGEL_DRAW_INT (5, FMAXS);
  /* pos: where the next split starts, saturated to [0, total + 4] (past the end is all alike) */
  long long pos = d->skip < 0 ? 0 : d->skip > total ? total + 4 : d->skip;
  for (int k = 0; k < d->nb; k++) {
    long long rem = total - pos;
    if (rem < 0) rem = 0;
    int m = HEGEL_DRAW_INT (0, 15);
    unsigned long long s;
    if (m <= 2) s = rem ? (unsigned long long) HEGEL_DRAW_I64 (0, rem) : 0;
    else if (m <= 5) s = pick_boundary (tc, d, pos) - pos;
    else if (m <= 6) {
      long long t = pick_boundary (tc, d, pos) - pos + (HEGEL_DRAW_BOOL () ? 1 : -1);
      s = t < 0 ? 0 : t > rem ? rem : t;
    }
    else if (m <= 8) s = 0;
    else if (m <= 11) s = rem;
    else if (m <= 12) s = rem + HEGEL_DRAW_INT (1, 3);
    else if (m <= 13) s = HEGEL_DRAW_U64 (0xfffffffeULL, 0x100000001ULL);
    else if (m <= 14) s = HEGEL_DRAW_U64 (0x7ffffffeULL, 0x80000001ULL);
    else s = HEGEL_DRAW_BOOL () ? ULLONG_MAX : ULLONG_MAX - rem;
    if (abi && s > UINT_MAX) s = UINT_MAX;
    d->sizes[k] = s;
    pos = s >= (unsigned long long) (total + 4 - pos) ? total + 4 : pos + (long long) s;
  }
}

static void note_desc (hegel_testcase *tc, const fdesc *d, const char *tag)
{
  char b[2048];
  int p = snprintf (b, sizeof b, "[%s] NEED=%d desc: ", tag, F_NEED);
  fdesc_print (d, b + p, sizeof b - p);
  if (getenv ("SGTRACE")) fprintf (stderr, "%s", b);
  hegel_note (tc, b);
}

/* ---------------------------------------------------------------- checking */

typedef unsigned __int128 u128;

static int spec_valid (const fdesc *d)
{
  if (d->skip < 0) return 0;
  u128 end = (u128) d->skip, total = 0;
  for (int k = 0; k < d->nb; k++) end += d->sizes[k];
  for (int i = 0; i < d->n; i++) total += d->len[i];
  return end <= total;
}

/* Is relative address a the address of stream position q?  For q strictly inside an
** entry there is one answer; at an entry boundary both the end of one entry and the
** start of the next qualify.  `need_bytes`: the position must have bytes after it in
** the same entry, and [q, q+len) must stay inside that entry. */
static int addr_of_pos (const fdesc *d, const fbuilt *B, unsigned long long q,
                        unsigned long long a, unsigned len)
{
  for (int i = 0; i < d->n; i++) {
    unsigned long long lo = B->cum[i], hi = B->cum[i + 1];
    if (len ? (q >= lo && q < hi && q + len <= hi) : (q >= lo && q <= hi))
      if (a == B->addr[i] + (q - lo)) return 1;
  }
  return 0;
}

static int dma_in_segment (const fdesc *d, const fbuilt *B, unsigned long long q, unsigned len)
{
  for (int s = 0; s < d->ndma; s++) {
    unsigned long long lo = B->cum[d->dstart[s]], hi = lo + d->dlen[s];
    if (len ? (q >= lo && q + len <= hi) : (q >= lo && q <= hi)) return 1;
  }
  return 0;
}

static void check (hegel_testcase *tc, const fdesc *d, const fbuilt *B, const fresult *r)
{
  if (!spec_valid (d)) {
    HEGEL_ASSERT (r->rc == -EINVAL, "invalid request (skip=%lld) not rejected: rc=%d", d->skip, r->rc);
    for (int k = 0; k < d->nb; k++)
      HEGEL_ASSERT (r->out[k] == F_POISON_PTR && r->on[k] == F_POISON_N,
                    "failed call wrote out[%d]/out_mapped_nents[%d]", k, k);
    return;
  }
  HEGEL_ASSERT (r->rc == 0, "valid request rejected: rc=%d", r->rc);
  unsigned long long cursor = (unsigned long long) d->skip;
  for (int k = 0; k < d->nb; k++) {
    unsigned long long want = d->sizes[k], got = 0;
    HEGEL_ASSERT (r->out[k] != F_POISON_PTR && r->out[k] != NULL, "out[%d] not set", k);
    HEGEL_ASSERT (r->on[k] >= 1, "split %d: out_mapped_nents %d < 1", k, r->on[k]);
    struct scatterlist *e = r->out[k];
    int cn = 0;
    for (;; e++) {
      cn++;
      HEGEL_ASSERT (cn <= FCAP, "split %d: runaway list (no end marker)", k);
      HEGEL_ASSERT (!sg_is_chain (e), "split %d entry %d has a chain bit", k, cn - 1);
      unsigned long long a = fpos (e);
      HEGEL_ASSERT (addr_of_pos (d, B, cursor + got, a, e->length),
                    "split %d entry %d: (addr %llu, len %u) is not stream position %llu of "
                    "the input (offset %u)", k, cn - 1, a, e->length, cursor + got, e->offset);
      if (cn == 1 && want)
        HEGEL_ASSERT (e->length, "split %d (size %llu) starts with a zero-length entry", k, want);
      HEGEL_ASSERT (got + e->length <= want, "split %d: bytes exceed size (%llu + %u > %llu)",
                    k, got, e->length, want);
      got += e->length;
      if (sg_is_last (e)) break;
    }
    HEGEL_ASSERT (got == want, "split %d: %llu bytes, size %llu", k, got, want);
    if (!want) HEGEL_ASSERT (cn == 1, "zero-size split %d has %d entries", k, cn);
    if (!d->mapped) {
      HEGEL_ASSERT (r->on[k] == cn, "split %d: out_mapped_nents %d != %d entries", k, r->on[k], cn);
      for (int j = 0; j < cn; j++) {
        HEGEL_ASSERT (sg_dma_address (&r->out[k][j]) == 0, "unmapped split %d entry %d: dma_address set", k, j);
#if F_NEED
        HEGEL_ASSERT (sg_dma_len (&r->out[k][j]) == 0, "unmapped split %d entry %d: dma_len set", k, j);
#endif
      }
    } else {
      HEGEL_ASSERT (r->on[k] <= cn, "split %d: %d DMA entries > %d allocated", k, r->on[k], cn);
      unsigned long long dgot = 0;
      for (int j = 0; j < r->on[k]; j++) {
        struct scatterlist *x = &r->out[k][j];
        unsigned dl = sg_dma_len (x);
        unsigned long long dp = (unsigned long long) ((uintptr_t) sg_dma_address (x) - FBASE);
        HEGEL_ASSERT (dp == cursor + dgot, "split %d DMA entry %d at stream %llu, expected %llu",
                      k, j, dp, cursor + dgot);
        HEGEL_ASSERT (dma_in_segment (d, B, dp, dl), "split %d DMA entry %d [%llu,+%u) crosses "
                      "a DMA segment boundary", k, j, dp, dl);
        if (j == 0 && want) HEGEL_ASSERT (dl, "split %d starts with a zero dma_len entry", k);
        HEGEL_ASSERT (dgot + dl <= want, "split %d: DMA bytes exceed size", k);
        dgot += dl;
      }
      HEGEL_ASSERT (dgot == want, "split %d: %llu DMA bytes, size %llu", k, dgot, want);
    }
    cursor += want;
  }
}

/* --------------------------------------------------------------- the 32-bit side */

static char *run_exec32 (const char *in, int *status)
{
  int pin[2], pout[2];
  if (pipe (pin) || pipe (pout)) hegel_health_fail ("pipe");
  pid_t pid = fork ();
  if (pid == 0) {
    dup2 (pin[0], 0);
    dup2 (pout[1], 1);
    close (pin[0]); close (pin[1]); close (pout[0]); close (pout[1]);
    execl (EXEC32, EXEC32, (char *) 0);
    _exit (127);
  }
  close (pin[0]);
  close (pout[1]);
  size_t inl = strlen (in);
  if (write (pin[1], in, inl) != (ssize_t) inl) hegel_health_fail ("write to exec32");
  close (pin[1]);
  size_t cap = 1 << 16, len = 0;
  char *buf = malloc (cap);
  ssize_t got;
  while ((got = read (pout[0], buf + len, cap - 1 - len)) > 0) len += got;
  buf[len] = 0;
  close (pout[0]);
  waitpid (pid, status, 0);
  return buf;
}

/* ------------------------------------------------------------------- properties */

static void t_huge (hegel_testcase *tc)
{
  fdesc d;
  fbuilt B;
  fresult r;
  draw_desc (tc, &d, 0);
  note_desc (tc, &d, "huge");
  fbuild (&d, &B);
  fcall (&d, &B, &r);
  check (tc, &d, &B, &r);
  ffree_result (&d, &r);
  ffree (&d, &B);
}

static void t_abi32 (hegel_testcase *tc)
{
  fdesc d;
  fbuilt B;
  fresult r;
  draw_desc (tc, &d, 1);
  note_desc (tc, &d, "abi32");
  char in[2048], a[1 << 15];
  fdesc_print (&d, in, sizeof in);
  fbuild (&d, &B);
  fcall (&d, &B, &r);
  fcanon (&d, &r, a, sizeof a);
  int st;
  char *b = run_exec32 (in, &st);
  if (!(WIFEXITED (st) && WEXITSTATUS (st) == 0) || strcmp (a, b)) {
    hegel_note (tc, "64-bit:");
    hegel_note (tc, a);
    hegel_note (tc, "32-bit:");
    hegel_note (tc, b);
  }
  HEGEL_ASSERT (WIFEXITED (st) && WEXITSTATUS (st) == 0, "32-bit build died (status %#x)", st);
  HEGEL_ASSERT (!strcmp (a, b), "32-bit and 64-bit builds disagree");
  free (b);
  check (tc, &d, &B, &r);
  ffree_result (&d, &r);
  ffree (&d, &B);
}

int main (int argc, char **argv)
{
  if (argc > 1) NCASES = strtoull (argv[1], 0, 10);
  const char *only = argc > 2 ? argv[2] : NULL;
  EXEC32 = getenv ("SG_EXEC32");
  struct { const char *name; void (*fn) (hegel_testcase *); } T[] = {
    { "huge", t_huge }, { "abi32", t_abi32 },
  };
  int fails = 0;
  printf ("sg_split fake-page props: NEED_SG_DMA_LENGTH=%d cases=%llu exec32=%s\n", F_NEED,
          (unsigned long long) NCASES, EXEC32 ? EXEC32 : "(none: abi32 skipped)");
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    if (only && strcmp (only, T[i].name)) continue;
    if (T[i].fn == t_abi32 && !EXEC32) continue;
    hegel_run_options o = { .test_cases = NCASES };
    printf ("=== %s\n", T[i].name);
    fflush (stdout);
    int rc = hegel_run_test_with (T[i].fn, &o);
    printf ("=== %s: %s\n", T[i].name, rc ? "FAIL" : "pass");
    fflush (stdout);
    fails += rc;
  }
  return fails ? 1 : 0;
}
