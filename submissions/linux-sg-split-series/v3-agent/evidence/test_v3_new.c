/* New properties for the v3 changes that no existing property covers.
**
**   make bin T=work/v3/test_v3_new.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_v3_new
**   (NEED: CFLAGS="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"; ASan: SAN=1)
**   ./work/v3/test_v3_new [cases] [name]
**
** P-ARGS      nb_splits <= 0 or skip < 0 -> -EINVAL, out[] / out_mapped_nents untouched,
**             nothing read out of bounds (ASan), nothing allocated (allocation tracker).
** P-ZEROSPLIT every zero-size split of a valid request is exactly one end-marked entry
**             of length 0, out_mapped_nents == 1, sitting at its stream position (CPU
**             address, and DMA address with dma_len 0 if mapped) -- including the
**             splits that start at the very end of the input (v3 3/6).
** P-NOLEAD    every split with size > 0 starts with an entry that has bytes, in the CPU
**             view and, if mapped, the DMA view -- also on inputs with empty entries,
**             which P-NOZERO (test_props) excludes (v3 4/6). */
#include "../sgcase.h"
#include <limits.h>

static uint64_t NCASES = 1000;

#if !defined(__SANITIZE_ADDRESS__)
#define TRACK_ALLOC 1
extern void * __libc_malloc (size_t);
extern void * __libc_calloc (size_t, size_t);
static int armed, nalloc;
void * malloc (size_t n) { if (armed) nalloc++; return __libc_malloc (n); }
void * calloc (size_t a, size_t b) { if (armed) nalloc++; return __libc_calloc (a, b); }
#else
#define TRACK_ALLOC 0
static int armed, nalloc;
#endif

/* stream position q -> is p the CPU address of q (end of entry i or start of entry i+1)? */
static int at_stream_pos (const sgcase * c, const unsigned char * p, unsigned long q)
{
  unsigned long cum = 0;
  for (int i = 0; i < c->n; i++) {
    if (q >= cum && q <= cum + c->len[i] && p == c->addr[i] + (q - cum)) return 1;
    cum += c->len[i];
  }
  return 0;
}

static void t_args (hegel_testcase * tc)
{
  sgcase c;
  draw_input (tc, &c, -1);
  draw_request (tc, &c, 0);
  int which = HEGEL_DRAW_INT (0, 2);
  int nb = c.nb;
  long skip = c.skip;
  if (which != 1) nb = HEGEL_DRAW_BOOL () ? 0 : -HEGEL_DRAW_INT (1, INT_MAX);
  if (which != 0) skip = HEGEL_DRAW_BOOL () ? -1 : -HEGEL_DRAW_I64 (1, LONG_MAX);
  char b[96];
  snprintf (b, sizeof b, "nb_splits=%d skip=%ld", nb, skip);
  hegel_note (tc, b);
  struct scatterlist * out[MAXS];
  int on[MAXS];
  for (int k = 0; k < MAXS; k++) { out[k] = POISON_PTR; on[k] = POISON_N; }
  armed = 1; nalloc = 0;
  int rc = sg_split (c.ent[0], c.mapped ? c.ndma : 0, (off_t) skip, nb, c.sizes, out, on, 0);
  armed = 0;
  free_case (&c);
  HEGEL_ASSERT (rc == -EINVAL, "nb_splits=%d skip=%ld accepted: rc=%d", nb, skip, rc);
  for (int k = 0; k < MAXS; k++)
    HEGEL_ASSERT (out[k] == POISON_PTR && on[k] == POISON_N, "rejected call wrote out[%d]", k);
#if TRACK_ALLOC
  HEGEL_ASSERT (nalloc == 0, "rejected call allocated %d time(s)", nalloc);
#endif
}

static void run_valid (hegel_testcase * tc, int zerosplit)
{
  sgcase c;
  draw_input (tc, &c, -1);
  draw_request (tc, &c, 0);
  note_case (tc, &c, zerosplit ? "zerosplit" : "nolead");
  if (!spec_valid (&c)) { free_case (&c); hegel_assume (tc, 0); }
  result r;
  call_split (sg_split, &c, &r);
  HEGEL_ASSERT (r.rc == 0, "valid request rejected: rc=%d", r.rc);
  unsigned long cursor = (unsigned long) c.skip;
  for (int k = 0; k < c.nb; k++) {
    struct scatterlist * e = r.out[k];
    size_t want = c.sizes[k];
    HEGEL_ASSERT (r.onents[k] >= 1, "split %d (size %zu): out_mapped_nents %d", k, want,
                  r.onents[k]);
    if (zerosplit && want == 0) {
      HEGEL_ASSERT (r.onents[k] == 1, "zero-size split %d: out_mapped_nents %d", k, r.onents[k]);
      HEGEL_ASSERT (sg_is_last (e) && !sg_is_chain (e), "zero-size split %d: entry 0 not the end", k);
      HEGEL_ASSERT (e->length == 0, "zero-size split %d: length %u", k, e->length);
      HEGEL_ASSERT (sg_dma_len (e) == 0, "zero-size split %d: dma_len %u", k, sg_dma_len (e));
      HEGEL_ASSERT (at_stream_pos (&c, sg_virt (e), cursor),
                    "zero-size split %d: CPU address is not stream position %lu", k, cursor);
      if (c.mapped)
        HEGEL_ASSERT (at_stream_pos (&c, (unsigned char *) sg_dma_address (e), cursor),
                      "zero-size split %d: DMA address is not stream position %lu", k, cursor);
      else
        HEGEL_ASSERT (sg_dma_address (e) == 0, "zero-size split %d: stale dma_address", k);
    }
    if (!zerosplit && want) {
      HEGEL_ASSERT (e->length != 0, "split %d (size %zu): first CPU entry has length 0", k, want);
      if (c.mapped)
        HEGEL_ASSERT (sg_dma_len (e) != 0, "split %d (size %zu): first DMA entry has dma_len 0",
                      k, want);
    }
    cursor += want;
  }
  free_result (&c, &r);
  free_case (&c);
}

static void t_zerosplit (hegel_testcase * tc) { run_valid (tc, 1); }
static void t_nolead (hegel_testcase * tc) { run_valid (tc, 0); }

int main (int argc, char ** argv)
{
  if (argc > 1) NCASES = strtoull (argv[1], 0, 10);
  const char * only = argc > 2 ? argv[2] : NULL;
  struct { const char * name; void (*fn) (hegel_testcase *); } T[] = {
    { "args", t_args }, { "zerosplit", t_zerosplit }, { "nolead", t_nolead },
  };
  int fails = 0;
  printf ("sg_split v3 new props: NEED_SG_DMA_LENGTH=%d ASan=%d cases=%llu\n", SG_NEED,
          !TRACK_ALLOC, (unsigned long long) NCASES);
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    if (only && strcmp (only, T[i].name)) continue;
    hegel_run_options o = { .test_cases = NCASES,
                            .suppress_health = HEGEL_SUPPRESS_FILTER_TOO_MUCH };
    printf ("=== %s\n", T[i].name);
    fflush (stdout);
    int rc = hegel_run_test_with (T[i].fn, &o);
    printf ("=== %s: %s\n", T[i].name, rc ? "FAIL" : "pass");
    fflush (stdout);
    fails += rc;
  }
  return fails ? 1 : 0;
}
