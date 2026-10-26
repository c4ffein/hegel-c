/* Differential test: v3 (work/v3/lib/sg_split.c, linked as sg_split) vs v2
** (kernel/lib/sg_split.c, #included below as sg_split_v2).
**
**   make bin T=work/v3/test_diff_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_diff_v3
**   make bin T=work/v3/test_diff_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_diff_v3_need \
**        CFLAGS="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"
**   (+ SAN=1 for ASan)            ./work/v3/test_diff_v3 [cases]
**
** PRECONDITION: inputs v3 is SUPPOSED to change relative to v2 (written from the
** v3 commit messages, evaluated with the geometry model of sgcase.h on the CPU
** geometry and, if mapped, on the DMA geometry over in_mapped_nents):
**   E1 (v3 3/6) invalid request (skip + sum(sizes) > total) in which some split is
**      reached with no input left (model any_empty): v2 returns 0, v3 -EINVAL.
**   E2 (v3 3/6) valid request in which some split starts at the very end of the
**      input (model any_empty): v2 gives it 0 entries, v3 one zero-length entry.
**   E3 (v3 4/6) some split with size > 0 starts in an entry from which it takes
**      0 bytes (the entry ending exactly at its start, or empty entries): v2 emits
**      a leading zero-length (zero dma_len) entry, v3 does not.
**   (v3 5/6 changes only nb_splits <= 0 and skip < 0, never generated here.)
** Everywhere else v2 and v3 must agree exactly: rc, out_mapped_nents[], every out
** entry (page_link, offset, length, dma_address, dma_len).  On E1..E3 v3 is
** additionally required to return -EINVAL for every invalid request (v3 contract),
** and the rest of the behaviour is checked by test_props_v3 / test_v3_new.
** v2 is memory-safe on every generated input (v2 P-CORE/P-INVALID ran under ASan),
** so it is always called. */
#include "sgcase_x.h"

/* Step-wise use (bisect check): -DOLDSRC='"steps/s3.c"' -DALLOW=4 compares a v3 step
** (SRC=work/v3/steps/s4.c) with its parent, allowing only the E-bits that step is
** supposed to change (1 = E1, 2 = E2, 4 = E3). */
#ifndef OLDSRC
#define OLDSRC "../../kernel/lib/sg_split.c"
#endif
/* -DOLD_UNSAFE_EMPTY: the old file writes out of bounds when a split starts at the end
** of the input (upstream / v3 steps 1-2, before the zero-nents fix): don't call it there. */
#ifndef ALLOW
#define ALLOW 7
#endif
#define sg_split sg_split_v2
#include OLDSRC
#undef sg_split

static uint64_t NCASES = 2000;
static long n_old_skipped, n_same, n_changed, n_changed_but_equal, n_e[4], n_valid_changed_but_equal;

/* E3 helper: does split k (size > 0) begin in an entry it takes no bytes from? */
static int zero_lead (const unsigned * lens, int N, const sgcase * c, const model * m)
{
  long p = c->skip;
  for (int k = 0; k < c->nb; k++) {
    const segm * s = &m->s[k];
    if (c->sizes[k] && s->start < N) {
      long cum = 0;
      for (int j = 0; j < s->start; j++) cum += lens[j];
      if ((long) lens[s->start] - (p - cum) == 0) return 1;
    }
    p += (long) c->sizes[k];
  }
  return 0;
}

static void t_diff (hegel_testcase * tc)
{
  sgcase c;
  draw_input (tc, &c, -1);
  draw_request (tc, &c, 1);
  note_case (tc, &c, "diff-v2-v3");

  model mc, md;
  model_split (c.len, c.n, &c, &mc);
  if (c.mapped) model_split (c.dlen, c.ndma, &c, &md);
  else memset (&md, 0, sizeof md);
  int valid = spec_valid (&c);
  int empty = mc.any_empty || (c.mapped && md.any_empty);
  int e1 = !valid && empty;
  int e2 = valid && empty;
  /* the model stops at an insufficient split; E3 only matters for valid requests
  ** (an invalid one must be -EINVAL in both, checked below) */
  int e3 = valid && (zero_lead (c.len, c.n, &c, &mc) ||
                     (c.mapped && zero_lead (c.dlen, c.ndma, &c, &md)));
  int changed = (e1 && (ALLOW & 1)) || (e2 && (ALLOW & 2)) || (e3 && (ALLOW & 4));
  char b[200];
  snprintf (b, sizeof b, "precondition: E1=%d E2=%d E3=%d -> %s", e1, e2, e3,
            changed ? "v3 may change behaviour" : "must be identical");
  hegel_note (tc, b);

  result r3, r2;
  call_split (sg_split, &c, &r3);
#ifdef OLD_UNSAFE_EMPTY
  if (empty) {
    int rc3 = r3.rc;
    n_old_skipped++;
    free_result (&c, &r3);
    free_case (&c);
    if (!valid)
      HEGEL_ASSERT (rc3 == -EINVAL, "v3 accepted an invalid request: rc=%d", rc3);
    return;
  }
#endif
  call_split (sg_split_v2, &c, &r2);

  int equal = r3.rc == r2.rc;
  if (equal && r3.rc == 0) {
    for (int k = 0; k < c.nb && equal; k++) {
      equal &= r3.onents[k] == r2.onents[k];
      if (r3.onents[k] < 1 || r2.onents[k] < 1) continue;
      for (int j = 0; equal; j++) {
        struct scatterlist * a = &r3.out[k][j], * o = &r2.out[k][j];
        equal &= a->page_link == o->page_link && a->offset == o->offset &&
                 a->length == o->length && sg_dma_address (a) == sg_dma_address (o) &&
                 sg_dma_len (a) == sg_dma_len (o);
        if (!equal) {
          snprintf (b, sizeof b, "split %d entry %d: v3{off=%u len=%u dlen=%u last=%d} "
                    "v2{off=%u len=%u dlen=%u last=%d}", k, j, a->offset, a->length,
                    sg_dma_len (a), sg_is_last (a), o->offset, o->length, sg_dma_len (o),
                    sg_is_last (o));
          hegel_note (tc, b);
        }
        if (sg_is_last (a) || sg_is_last (o) || j > MAXN) break;
      }
    }
  }
  if (!equal) {
    snprintf (b, sizeof b, "rc v3=%d v2=%d", r3.rc, r2.rc);
    hegel_note (tc, b);
  }
  int rc3 = r3.rc;
  free_result (&c, &r3);
  free_result (&c, &r2);
  free_case (&c);
  if (!valid)
    HEGEL_ASSERT (rc3 == -EINVAL, "v3 accepted an invalid request: rc=%d", rc3);
  if (changed) {
    n_changed++;
    n_e[1] += e1; n_e[2] += e2; n_e[3] += e3;
    if (equal) n_changed_but_equal++;
    if (equal && valid) n_valid_changed_but_equal++;
    return;
  }
  n_same++;
  HEGEL_ASSERT (equal, "v2 and v3 disagree on an input v3 must not change");
}

int main (int argc, char ** argv)
{
  if (argc > 1) NCASES = strtoull (argv[1], 0, 10);
  hegel_run_options o = { .test_cases = NCASES };
  printf ("sg_split differential old=%s vs new: ALLOW=%d NEED_SG_DMA_LENGTH=%d cases=%llu\n",
          OLDSRC, ALLOW, SG_NEED, (unsigned long long) NCASES);
  int rc = getenv ("SGDIFF_FORK") ? hegel_run_test_with (t_diff, &o)
                                  : hegel_run_test_nofork_with (t_diff, &o);
  printf ("differential v2/v3: %s  (identical-required=%ld, changed-allowed=%ld [E1=%ld E2=%ld "
          "E3=%ld] of which still identical=%ld, valid ones among those=%ld; old not called=%ld)\n",
          rc ? "FAIL" : "pass", n_same, n_changed, n_e[1], n_e[2], n_e[3], n_changed_but_equal, n_valid_changed_but_equal, n_old_skipped);
  return rc;
}
