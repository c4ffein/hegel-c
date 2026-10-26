/* COPY of work/test_props.c for v3 (work/v3/lib/sg_split.c).  Only change: includes
** sgcase_v3.h, whose CHK_GEOMETRY expectation follows the v3 contract (EVIDENCE.md).
**
** Property tests for sg_split() (kernel/lib/sg_split.c, series applied).
**
**   make bin T=work/test_props.c                         # !NEED model
**   make bin T=work/test_props.c SAN=1 OUT=work/test_props_san
**   make bin T=work/test_props.c OUT=work/test_props_need \
**        CFLAGS="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"
**   ./work/test_props [case-count]      (default 1000 per property)
**   ./work/test_props <case-count> <name>   run one property only
**
** Each property is its own engine run so each failure shrinks on its own. */
#include "sgcase_v3.h"   /* v3 copy: only the CHK_GEOMETRY expectation differs */

static uint64_t NCASES = 1000;

/* ---------------------------------------------------------------- malloc
** Allocation tracking + failure injection around the sg_split() call only.
** Not under ASan (it owns malloc). */
#if !defined(__SANITIZE_ADDRESS__)
#define TRACK_ALLOC 1
extern void * __libc_malloc (size_t);
extern void * __libc_calloc (size_t, size_t);
extern void   __libc_free (void *);
static int armed, fail_at = -1, nalloc, injected;
static void * live[64];
static int nlive;
static void track (void * p) { if (p && nlive < 64) live[nlive++] = p; }
void * malloc (size_t n)
{
  if (!armed) return __libc_malloc (n);
  if (n && nalloc++ == fail_at) { injected = 1; return NULL; }  /* kmalloc(0) never fails */
  void * p = __libc_malloc (n);
  track (p);
  return p;
}
void * calloc (size_t a, size_t b)
{
  if (!armed) return __libc_calloc (a, b);
  if (a * b && nalloc++ == fail_at) { injected = 1; return NULL; }
  void * p = __libc_calloc (a, b);
  track (p);
  return p;
}
void free (void * p)
{
  if (armed)
    for (int i = 0; i < nlive; i++)
      if (live[i] == p) { live[i] = live[--nlive]; break; }
  __libc_free (p);
}
static void arm (int at) { armed = 1; fail_at = at; nalloc = 0; injected = 0; nlive = 0; }
static void disarm (void) { armed = 0; }
#else
#define TRACK_ALLOC 0
static void arm (int at) { (void) at; }
static void disarm (void) {}
#endif

/* ------------------------------------------------------------ properties */

static int FORCE_MAPPED = -1;
static void run_valid (hegel_testcase * tc, unsigned mask)
{
  sgcase c;
  draw_input (tc, &c, FORCE_MAPPED);
  draw_request (tc, &c, 0);
  note_case (tc, &c, "valid");
  int ok = spec_valid (&c);
  /* P-NOZERO is about zero-length entries sg_split() creates, not ones it copies:
  ** only judge it on inputs without empty entries. */
  if (mask & (CHK_NOZERO | CHK_NOZERO_DMA))
    for (int i = 0; i < c.n; i++) ok &= c.len[i] != 0;
  if (!ok) { free_case (&c); hegel_assume (tc, 0); }
  result r;
  call_split (sg_split, &c, &r);
  check_output (tc, &c, &r, mask);
  free_result (&c, &r);
  free_case (&c);
}

/* P-CORE: valid request -> rc 0, each split exposes exactly its bytes (CPU and
** DMA view), one end marker, entries inside input entries, DMA view scrubbed
** when unmapped, dma_len 0 past the mapped count. */
static void t_core (hegel_testcase * tc) { run_valid (tc, CHK_CORE); }
/* P-WALK: every returned out[k] is a scatterlist with >= 1 entry. */
static void t_walkable (hegel_testcase * tc) { run_valid (tc, CHK_WALKABLE); }
/* P-NOZERO: a non-empty split has no zero-length (CPU) / zero dma_len (DMA) entry. */
static void t_nozero (hegel_testcase * tc) { run_valid (tc, CHK_NOZERO); }
static void t_nozero_mapped (hegel_testcase * tc)
{ FORCE_MAPPED = 1; run_valid (tc, CHK_NOZERO_DMA); FORCE_MAPPED = -1; }
/* P-GEOM: entry count matches the geometry model (validates the model). */
static void t_geometry (hegel_testcase * tc) { run_valid (tc, CHK_GEOMETRY); }

/* P-INVALID: skip + sum(sizes) > total -> -EINVAL, out[] / out_mapped_nents untouched.
** invalid_other: same, excluding the known class (a split reached with no input left). */
static int EXCLUDE_EMPTY;
static void t_invalid (hegel_testcase * tc)
{
  sgcase c;
  draw_input (tc, &c, -1);
  draw_request (tc, &c, 1);
  if (spec_valid (&c)) {        /* make it invalid without filtering: overrun the last split */
    unsigned long end = (unsigned long) c.skip;
    for (int k = 0; k < c.nb; k++) end += c.sizes[k];
    c.sizes[c.nb - 1] += (c.total - end) + (size_t) HEGEL_DRAW_INT (1, 3);
  }
  note_case (tc, &c, "invalid");
  model m;
  model_split (c.len, c.n, &c, &m);
  char b[128];
  if (EXCLUDE_EMPTY && m.any_empty) { free_case (&c); hegel_assume (tc, 0); }
  snprintf (b, sizeof b, "model: first_empty=%d any_empty=%d", m.first_empty, m.any_empty);
  hegel_note (tc, b);
  result r;
  call_split (sg_split, &c, &r);
  int rc = r.rc;
  free_result (&c, &r);
  HEGEL_ASSERT (rc == -EINVAL,
                "request past the end of the input accepted: rc=%d (kdoc: out[k] covers "
                "bytes that do not exist)", rc);
  for (int k = 0; k < MAXS; k++)
    HEGEL_ASSERT (r.out[k] == POISON_PTR && r.onents[k] == POISON_N,
                  "failed call wrote out[%d]/out_mapped_nents[%d]", k, k);
  free_case (&c);
}

static void t_invalid_other (hegel_testcase * tc)
{ EXCLUDE_EMPTY = 1; t_invalid (tc); EXCLUDE_EMPTY = 0; }

/* P-ERRMEM: on every failure path (-ENOMEM injected at each allocation; -EINVAL from
** the mapped pass after the outputs were allocated) nothing leaks and out[] is
** untouched; on success the only live allocations are exactly out[0..nb-1]. */
static void t_errpaths (hegel_testcase * tc)
{
  sgcase c;
  draw_input (tc, &c, -1);
  draw_request (tc, &c, 0);
  /* mapped-pass failure: map fewer segments than the CPU list holds */
  int trunc = c.mapped && c.ndma >= 2 && HEGEL_DRAW_BOOL ();
  if (trunc) c.ndma = HEGEL_DRAW_INT (1, c.ndma - 1);
  int at = TRACK_ALLOC ? HEGEL_DRAW_INT (-1, c.nb + 1) : -1;
  note_case (tc, &c, trunc ? "errpath/truncated-mapping" : "errpath");
  if (!spec_valid (&c)) { free_case (&c); hegel_assume (tc, 0); }
  model m;
  model_split (c.len, c.n, &c, &m);
  if (m.any_empty) { free_case (&c); hegel_assume (tc, 0); }  /* reported by P-WALK */
  result r;
  arm (at);
  call_split (sg_split, &c, &r);
  disarm ();
#if TRACK_ALLOC
  if (injected)
    HEGEL_ASSERT (r.rc == -ENOMEM, "allocation %d failed but rc=%d", at, r.rc);
#endif
  if (r.rc) {
    for (int k = 0; k < MAXS; k++)
      HEGEL_ASSERT (r.out[k] == POISON_PTR && r.onents[k] == POISON_N,
                    "failed call (rc=%d) wrote out[%d]", r.rc, k);
#if TRACK_ALLOC
    HEGEL_ASSERT (nlive == 0, "rc=%d leaked %d allocation(s)", r.rc, nlive);
#endif
  } else {
#if TRACK_ALLOC
    HEGEL_ASSERT (nlive == c.nb, "success: %d live allocations, expected %d (out[])",
                  nlive, c.nb);
    for (int k = 0; k < c.nb; k++) {
      int f = 0;
      for (int i = 0; i < nlive; i++) f |= live[i] == r.out[k];
      HEGEL_ASSERT (f, "out[%d] is not a fresh allocation", k);
    }
#endif
    if (!trunc) check_output (tc, &c, &r, CHK_CORE);
    free_result (&c, &r);
  }
  free_case (&c);
}

int main (int argc, char ** argv)
{
  if (argc > 1) NCASES = strtoull (argv[1], 0, 10);
  const char * only = argc > 2 ? argv[2] : NULL;
#ifdef MUT_GREEN
  NCASES = 400;   /* green subset for tools/mutate.py: only properties the series passes */
#endif
  struct { const char * name; void (*fn) (hegel_testcase *); } T[] = {
    { "core", t_core }, { "walkable", t_walkable }, { "nozero", t_nozero }, { "nozero_mapped", t_nozero_mapped },
    { "geometry", t_geometry }, { "invalid", t_invalid }, { "invalid_other", t_invalid_other }, { "errpaths", t_errpaths },
  };
  int fails = 0;
  printf ("sg_split props: NEED_SG_DMA_LENGTH=%d ASan=%d cases=%llu\n", SG_NEED, !TRACK_ALLOC,
          (unsigned long long) NCASES);
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    if (only && strcmp (only, T[i].name)) continue;
#ifdef MUT_GREEN
    if (!strcmp (T[i].name, "walkable") || !strncmp (T[i].name, "invalid", 7) ||
        !strncmp (T[i].name, "nozero", 6)) continue;
#endif
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
