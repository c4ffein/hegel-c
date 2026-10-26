/* Shared generator, oracle model and output checker for sg_split() property tests.
**
** Build model: CONFIG_NEED_SG_DMA_LENGTH is NOT defined by default in this harness
** (sg_dma_len() aliases ->length = the "!NEED" model).  Build with
**   make bin T=... CFLAGS="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"
** for the NEED model (separate dma_length field).  SG_NEED below reports which. */
#ifndef SGCASE_H
#define SGCASE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <linux/scatterlist.h>
#include "hegel_c.h"
#include "hegel_gen.h"

#ifdef CONFIG_NEED_SG_DMA_LENGTH
#define SG_NEED 1
#else
#define SG_NEED 0
#endif

int sg_split (struct scatterlist *in, const int in_mapped_nents,
              const off_t skip, const int nb_splits,
              const size_t *split_sizes,
              struct scatterlist **out, int *out_mapped_nents,
              gfp_t gfp_mask);

#define MAXN 7
#define MAXS 6
#define POISON_PTR ((struct scatterlist *) 0x5a5a5a5a5a5a5a50UL)
#define POISON_N   (-777)

typedef struct {
  /* drawn */
  int n;                       /* input entries */
  unsigned len[MAXN];
  unsigned gap[MAXN];          /* bytes between entry i and i+1 in memory */
  unsigned off0;               /* in-page offset of entry 0 */
  int chain_at;                /* 0 = flat table; else entries [0,chain_at) in table 1 */
  int mapped;                  /* pass in_mapped_nents != 0 */
  int ndma;                    /* in_mapped_nents */
  unsigned dlen[MAXN];         /* DMA segment j length (segment j lives in entry j) */
  unsigned dstart[MAXN];       /* first input entry of DMA segment j */
  long skip;
  int nb;
  size_t sizes[MAXS];
  /* derived */
  unsigned char *buf;
  size_t buflen;
  unsigned char *addr[MAXN];
  struct scatterlist *tab1, *tab2;
  struct scatterlist *ent[MAXN];
  unsigned long total;         /* CPU bytes */
  unsigned long dtotal;        /* DMA bytes over ndma */
  int coalesced;               /* some DMA segment spans >1 entry */
} sgcase;

static inline uint8_t pat (size_t p)
{
  uint32_t x = (uint32_t) p * 2654435761u;
  x ^= x >> 15;
  return (uint8_t) (x ^ (x >> 8));
}

/* -------------------------------------------------------------- drawing */

static unsigned draw_len (hegel_testcase * tc)
{
  int b = HEGEL_DRAW_INT (0, 99);
  if (b < 55) return (unsigned) HEGEL_DRAW_INT (1, 16);
  if (b < 63) return 0;                                   /* empty entry */
  if (b < 83) return (unsigned) HEGEL_DRAW_INT (17, 300);
  if (b < 93) return (unsigned) HEGEL_DRAW_INT (PAGE_SIZE - 2, PAGE_SIZE + 2);
  return (unsigned) HEGEL_DRAW_INT (301, 3 * PAGE_SIZE);
}

/* Boundaries of the CPU geometry at or after pos. */
static long pick_boundary (hegel_testcase * tc, const sgcase * c, long pos)
{
  long cand[MAXN + 1];
  int m = 0;
  long cum = 0;
  for (int i = 0; i <= c->n; i++) {
    if (cum >= pos) cand[m++] = cum;
    if (i < c->n) cum += c->len[i];
  }
  if (!m) return pos;
  return cand[HEGEL_DRAW_INT (0, m - 1)];
}

/* allow_invalid: may draw skip/sizes beyond the end of the input. */
static void draw_request (hegel_testcase * tc, sgcase * c, int allow_invalid)
{
  long total = (long) c->total;
  int sm = HEGEL_DRAW_INT (0, allow_invalid ? 9 : 8);
  switch (sm) {
  case 0: case 1: case 2: c->skip = 0; break;
  case 3: case 4: c->skip = pick_boundary (tc, c, 0); break;          /* exact boundary */
  case 5: {
    long b = pick_boundary (tc, c, 0);
    c->skip = b + (HEGEL_DRAW_BOOL () ? 1 : -1);
    if (c->skip < 0) c->skip = 0;
    if (c->skip > total) c->skip = total;
    break;
  }
  case 6: c->skip = HEGEL_DRAW_I64 (0, total); break;
  case 7: case 8: c->skip = total; break;                              /* at the very end */
  default: c->skip = total + HEGEL_DRAW_I64 (1, 3); break;            /* past the end */
  }
  c->nb = HEGEL_DRAW_INT (1, MAXS);
  long pos = c->skip;
  for (int k = 0; k < c->nb; k++) {
    long rem = total - pos;
    if (rem < 0) rem = 0;
    int m = HEGEL_DRAW_INT (0, allow_invalid ? 19 : 18);
    long s;
    if (m <= 5) s = rem ? HEGEL_DRAW_I64 (0, rem < 40 ? rem : 40) : 0;
    else if (m <= 10) s = pick_boundary (tc, c, pos) - pos;          /* end exactly on boundary */
    else if (m <= 12) {
      s = pick_boundary (tc, c, pos) - pos + (HEGEL_DRAW_BOOL () ? 1 : -1);
      if (s < 0) s = 0;
      if (s > rem) s = rem;
    }
    else if (m <= 15) s = 0;                                          /* zero-size split */
    else if (m <= 18) s = rem;                                        /* consume all */
    else s = rem + HEGEL_DRAW_INT (1, 3);                             /* overrun */
    c->sizes[k] = (size_t) s;
    pos += s;
  }
}

/* Builds the input table + backing memory.  want_mapped: -1 draw, 0 no, 1 yes. */
static void draw_input (hegel_testcase * tc, sgcase * c, int want_mapped)
{
  memset (c, 0, sizeof *c);
  c->n = HEGEL_DRAW_INT (1, MAXN);
  for (int i = 0; i < c->n; i++) {
    c->len[i] = draw_len (tc);
    int g = HEGEL_DRAW_INT (0, 9);
    c->gap[i] = g < 4 ? 0 : g < 8 ? (unsigned) HEGEL_DRAW_INT (1, 64) : (unsigned) PAGE_SIZE;
  }
  int o = HEGEL_DRAW_INT (0, 4);
  c->off0 = o == 0 ? 0 : o == 1 ? PAGE_SIZE - 1 : (unsigned) HEGEL_DRAW_INT (0, PAGE_SIZE - 1);

  /* memory: entries laid out with gaps; gap 0 = physically contiguous */
  size_t need = PAGE_SIZE + c->off0;
  for (int i = 0; i < c->n; i++) need += c->len[i] + c->gap[i];
  c->buflen = (need + PAGE_SIZE) & ~(PAGE_SIZE - 1);
  c->buf = aligned_alloc (PAGE_SIZE, c->buflen);
  for (size_t p = 0; p < c->buflen; p++) c->buf[p] = pat (p);
  size_t at = c->off0;
  for (int i = 0; i < c->n; i++) {
    c->addr[i] = c->buf + at;
    at += c->len[i] + c->gap[i];
    c->total += c->len[i];
  }

  /* table, optionally chained */
  c->chain_at = (c->n >= 2 && HEGEL_DRAW_INT (0, 3) == 0) ? HEGEL_DRAW_INT (1, c->n - 1) : 0;
  if (c->chain_at) {
    c->tab1 = calloc (c->chain_at + 1, sizeof (struct scatterlist));
    c->tab2 = calloc (c->n - c->chain_at, sizeof (struct scatterlist));
    sg_init_table (c->tab1, c->chain_at + 1);
    sg_init_table (c->tab2, c->n - c->chain_at);
    sg_chain (c->tab1, c->chain_at + 1, c->tab2);
    for (int i = 0; i < c->n; i++)
      c->ent[i] = i < c->chain_at ? &c->tab1[i] : &c->tab2[i - c->chain_at];
  } else {
    c->tab1 = calloc (c->n, sizeof (struct scatterlist));
    sg_init_table (c->tab1, c->n);
    for (int i = 0; i < c->n; i++) c->ent[i] = &c->tab1[i];
  }
  for (int i = 0; i < c->n; i++) sg_set_buf (c->ent[i], c->addr[i], c->len[i]);

  /* DMA view: identity IOVA (dma_address == kernel VA) so bytes are checkable.
  ** NEED: physically contiguous neighbours may be coalesced, segments packed
  ** into entries 0..ndma-1 as dma_map_sg does; the rest get dma_len 0.
  ** !NEED: dma_len IS ->length, so the DMA geometry equals the CPU one. */
  c->mapped = want_mapped < 0 ? HEGEL_DRAW_BOOL () : want_mapped;
  if (c->mapped) {
    int j = 0;
    for (int i = 0; i < c->n; i++) {
      int merge = SG_NEED && i > 0 && c->gap[i - 1] == 0 && HEGEL_DRAW_INT (0, 2) == 0;
      if (merge) { c->dlen[j - 1] += c->len[i]; c->coalesced = 1; }
      else { c->dstart[j] = i; c->dlen[j] = c->len[i]; j++; }
    }
    c->ndma = j;
    for (int s = 0; s < c->ndma; s++) {
      sg_dma_address (c->ent[s]) = (dma_addr_t) c->addr[c->dstart[s]];
      c->dtotal += c->dlen[s];
#if SG_NEED
      sg_dma_len (c->ent[s]) = c->dlen[s];
#endif
    }
  }
}

static void free_case (sgcase * c)
{
  free (c->buf);
  free (c->tab1);
  free (c->tab2);
}

static void note_case (hegel_testcase * tc, const sgcase * c, const char * tag)
{
  char b[1024];
  int p = snprintf (b, sizeof b, "[%s] NEED=%d n=%d lens=[", tag, SG_NEED, c->n);
  for (int i = 0; i < c->n; i++)
    p += snprintf (b + p, sizeof b - p, "%s%u%s", i ? "," : "", c->len[i],
                   (i < c->n - 1 && c->gap[i] == 0) ? "~" : "");
  p += snprintf (b + p, sizeof b - p, "] (~ = contiguous with next) off0=%u chain_at=%d mapped=%d",
                 c->off0, c->chain_at, c->mapped);
  if (c->mapped) {
    p += snprintf (b + p, sizeof b - p, " ndma=%d dlens=[", c->ndma);
    for (int s = 0; s < c->ndma; s++)
      p += snprintf (b + p, sizeof b - p, "%s%u", s ? "," : "", c->dlen[s]);
    p += snprintf (b + p, sizeof b - p, "]");
  }
  p += snprintf (b + p, sizeof b - p, " skip=%ld nb=%d sizes=[", c->skip, c->nb);
  for (int k = 0; k < c->nb; k++)
    p += snprintf (b + p, sizeof b - p, "%s%zu", k ? "," : "", c->sizes[k]);
  snprintf (b + p, sizeof b - p, "] total=%lu", c->total);
  if (getenv ("SGTRACE")) { fprintf (stderr, "%s\n", b); fflush (stderr); }
  hegel_note (tc, b);
}

/* ---------------------------------------------------------- oracle model
** Spec (kdoc): out[k] covers bytes [skip + sum(sizes[<k]), + sizes[k]) of in.
** valid <=> skip + sum(sizes) <= total. */
static int spec_valid (const sgcase * c)
{
  unsigned long end = (unsigned long) c->skip;
  for (int k = 0; k < c->nb; k++) end += c->sizes[k];
  return c->skip >= 0 && end <= c->total;
}

/* Geometry model of sg_calculate_split (written from the algorithm's documented
** rules, not from its code): where each split starts/ends in a list of segment
** lengths.  Used ONLY for preconditions (which inputs the series changes) and
** to explain counterexamples. */
typedef struct {
  int start, end;     /* entry indices; start == N means "no input entry left" */
  int mid;            /* ends strictly inside entry `end` */
  int nents;
} segm;

typedef struct {
  segm s[MAXS];
  int any_empty;           /* some split got no entry (zero-nents) */
  int first_empty;         /* index of first such split, -1 if none */
  int overshoot;           /* pre-series nb_splits overshoot fires */
  int multi_entry;         /* some split has nents >= 2 */
  int insufficient;        /* a size>0 split ran off the end */
  int no_first;            /* skip beyond the input */
} model;

static void model_split (const unsigned * lens, int N, const sgcase * c, model * m)
{
  memset (m, 0, sizeof *m);
  m->first_empty = -1;
  long cum[MAXN + 1];
  cum[0] = 0;
  for (int i = 0; i < N; i++) cum[i + 1] = cum[i] + lens[i];
  int start = N;
  long pos = 0;
  for (int i = 0; i < N; i++)
    if (c->skip <= cum[i + 1]) { start = i; pos = c->skip - cum[i]; break; }
  if (start == N) { m->no_first = 1; return; }
  for (int k = 0; k < c->nb; k++) {
    segm * s = &m->s[k];
    if (start >= N) {
      s->start = s->end = N; s->nents = 0;
      if (!m->any_empty) m->first_empty = k;
      m->any_empty = 1;
      continue;
    }
    s->start = start;
    long need = (long) c->sizes[k];
    int i = start;
    long avail = lens[i] - pos;
    if (need == 0) { s->end = i; s->mid = pos < (long) lens[i]; pos = pos; }
    else {
      while (need > avail) {
        need -= avail;
        i++;
        if (i >= N) { m->insufficient = 1; return; }
        avail = lens[i];
        pos = 0;
      }
      pos += need;
      s->end = i;
      s->mid = pos < (long) lens[i];
    }
    s->nents = s->end - s->start + 1;
    if (s->nents >= 2) m->multi_entry = 1;
    if (s->mid) start = s->end;
    else { start = s->end + 1; pos = 0; }
  }
  segm * last = &m->s[c->nb - 1];
  m->overshoot = !m->any_empty && last->mid && last->end < N - 1;
}

/* v3: expected entry count of split k.  Same rules as model_split(), plus the two
** v3 contract changes: an empty split (start == N) gets one zero-length entry, and
** a split with size > 0 does not begin with entries it takes no bytes from (the
** entry that ends exactly at the split's start position, or empty entries). */
static int model_nents_v3 (const unsigned * lens, int N, const sgcase * c, const model * m, int k)
{
  const segm * s = &m->s[k];
  if (s->start >= N) return 1;
  if (c->sizes[k] == 0) return s->nents;
  /* position of the split start inside entry s->start */
  long cum = 0, p = (long) c->skip;
  for (int j = 0; j < k; j++) p += (long) c->sizes[j];
  for (int j = 0; j < s->start; j++) cum += lens[j];
  int j = s->start;
  long avail = (long) lens[j] - (p - cum);
  while (avail == 0 && j < s->end) { j++; avail = lens[j]; }
  return s->end - j + 1;
}

/* ------------------------------------------------------------ the call */

typedef struct {
  int rc;
  struct scatterlist * out[MAXS];
  int onents[MAXS];
} result;

typedef int (*sg_split_fn) (struct scatterlist *, const int, const off_t, const int,
                            const size_t *, struct scatterlist **, int *, gfp_t);

static void call_split (sg_split_fn fn, sgcase * c, result * r)
{
  for (int k = 0; k < MAXS; k++) { r->out[k] = POISON_PTR; r->onents[k] = POISON_N; }
  r->rc = fn (c->ent[0], c->mapped ? c->ndma : 0, (off_t) c->skip, c->nb, c->sizes,
              r->out, r->onents, 0);
}

static void free_result (const sgcase * c, result * r)
{
  if (r->rc) return;
  for (int k = 0; k < c->nb; k++) free (r->out[k]);
}

/* -------------------------------------------------------------- checks */

#define CHK_CORE        (1u << 0)   /* rc, sizes, content, end marker, DMA view */
#define CHK_WALKABLE    (1u << 1)   /* every out[k] is a real list (>= 1 entry) */
#define CHK_NOZERO      (1u << 2)   /* no zero-length entry in a non-empty split */
#define CHK_GEOMETRY    (1u << 3)   /* nents == model nents (minimality explained) */
#define CHK_NOZERO_DMA  (1u << 4)   /* mapped: no dma_len 0 among the first out_mapped_nents */

static int in_some_entry (const sgcase * c, const unsigned char * p, unsigned l)
{
  for (int i = 0; i < c->n; i++)
    if (p >= c->addr[i] && p + l <= c->addr[i] + c->len[i]) return 1;
  return 0;
}

/* Checks a successful result against the spec.  Requires spec_valid(c). */
static void check_output (hegel_testcase * tc, const sgcase * c, const result * r, unsigned mask)
{
  unsigned long cursor = (unsigned long) c->skip;
  /* flattened CPU stream -> buffer offset */
  size_t flat[MAXN + 1];
  flat[0] = 0;
  for (int i = 0; i < c->n; i++) flat[i + 1] = flat[i] + c->len[i];
#define FLATBYTE(q) ({ size_t _q = (q); int _i = 0; while (_q >= flat[_i + 1]) _i++; \
                       c->addr[_i][_q - flat[_i]]; })

  HEGEL_ASSERT (r->rc == 0, "valid request rejected: rc=%d", r->rc);
  for (int k = 0; k < c->nb; k++) {
    size_t want = c->sizes[k];
    HEGEL_ASSERT (r->out[k] != POISON_PTR && r->out[k] != NULL, "out[%d] not set", k);
    HEGEL_ASSERT (r->onents[k] != POISON_N, "out_mapped_nents[%d] not set", k);
    if (mask & CHK_WALKABLE)
      HEGEL_ASSERT (r->onents[k] >= 1,
                    "split %d (size %zu) returned with 0 entries: out[k] is a 0-byte "
                    "allocation (ZERO_SIZE_PTR in-kernel), not a scatterlist — "
                    "sg_nents()/sg_next()/sg_copy_*() on it read out of bounds", k, want);
    if (r->onents[k] < 1) { cursor += want; continue; }   /* cannot walk it safely */

    /* --- CPU view: walk to the end marker */
    struct scatterlist * sg = r->out[k];
    int cpu_n = 0;
    size_t got = 0;
    for (;;) {
      cpu_n++;
      HEGEL_ASSERT (cpu_n <= MAXN + 1, "split %d: runaway list (no end marker)", k);
      HEGEL_ASSERT (!sg_is_chain (sg), "split %d entry %d carries a chain bit", k, cpu_n - 1);
      if (mask & CHK_CORE) {
        unsigned char * v = sg_virt (sg);
        HEGEL_ASSERT (in_some_entry (c, v, sg->length),
                      "split %d entry %d [%p,+%u) not inside any input entry", k, cpu_n - 1,
                      (void *) v, sg->length);
        HEGEL_ASSERT (got + sg->length <= want,
                      "split %d: CPU bytes exceed size (%zu + %u > %zu)", k, got, sg->length, want);
        for (unsigned b = 0; b < sg->length; b++)
          HEGEL_ASSERT (v[b] == FLATBYTE (cursor + got + b),
                        "split %d entry %d byte %u: content mismatch (stream pos %lu)",
                        k, cpu_n - 1, b, cursor + got + b);
      }
      if (mask & CHK_NOZERO)
        HEGEL_ASSERT (want == 0 || sg->length != 0,
                      "split %d (size %zu): entry %d has length 0", k, want, cpu_n - 1);
      got += sg->length;
      if (sg_is_last (sg)) break;
      sg = sg + 1;   /* out lists are flat arrays */
    }
    if (mask & CHK_CORE) {
      HEGEL_ASSERT (got == want, "split %d: CPU bytes %zu != size %zu", k, got, want);
      if (!c->mapped)
        HEGEL_ASSERT (r->onents[k] == cpu_n, "split %d: out_mapped_nents %d != list length %d",
                      k, r->onents[k], cpu_n);
    }
    if ((mask & CHK_GEOMETRY) && !c->mapped) {
      model m;
      model_split (c->len, c->n, c, &m);
      /* v3 CONTRACT CHANGE (see work/v3/EVIDENCE.md): expectation = model_nents_v3(),
      ** not m.s[k].nents.  (a) a split that starts at the very end of the input
      ** (model nents 0) is one zero-length entry; (b) a split with bytes to cover
      ** does not start with entries it takes 0 bytes from. */
      int want_n = model_nents_v3 (c->len, c->n, c, &m, k);
      HEGEL_ASSERT (cpu_n == want_n, "split %d: %d entries, model(v3) %d", k, cpu_n, want_n);
    }

    if ((mask & CHK_NOZERO_DMA) && c->mapped && want)
      for (int j = 0; j < r->onents[k] && j < cpu_n; j++)
        HEGEL_ASSERT (sg_dma_len (&r->out[k][j]) != 0,
                      "split %d (size %zu): DMA entry %d of %d has dma_len 0 — a consumer that "
                      "stops at the first zero sg_dma_len() sees a truncated split",
                      k, want, j, r->onents[k]);

    /* --- DMA view */
    if (mask & CHK_CORE) {
      if (!c->mapped) {
        for (int j = 0; j < cpu_n; j++) {
          HEGEL_ASSERT (sg_dma_address (&r->out[k][j]) == 0,
                        "unmapped split %d entry %d: stale dma_address", k, j);
#if SG_NEED
          HEGEL_ASSERT (sg_dma_len (&r->out[k][j]) == 0,
                        "unmapped split %d entry %d: stale dma_length %u", k, j,
                        sg_dma_len (&r->out[k][j]));
#endif
        }
      } else {
        int dn = r->onents[k];
        HEGEL_ASSERT (dn <= cpu_n, "split %d: %d DMA entries > %d allocated", k, dn, cpu_n);
        size_t dgot = 0;
        for (int j = 0; j < dn; j++) {
          struct scatterlist * e = &r->out[k][j];
          unsigned char * v = (unsigned char *) sg_dma_address (e);
          unsigned dl = sg_dma_len (e);
          HEGEL_ASSERT (dgot + dl <= want, "split %d: DMA bytes exceed size", k);
          for (unsigned b = 0; b < dl; b++)
            HEGEL_ASSERT (v[b] == FLATBYTE (cursor + dgot + b),
                          "split %d dma entry %d byte %u: content mismatch", k, j, b);
          if (mask & CHK_NOZERO)
            HEGEL_ASSERT (want == 0 || dl != 0,
                          "split %d (size %zu): DMA entry %d has dma_len 0 — a consumer that "
                          "stops at the first zero sg_dma_len() sees a truncated split", k, want, j);
          dgot += dl;
        }
        HEGEL_ASSERT (dgot == want, "split %d: DMA bytes %zu != size %zu", k, dgot, want);
#if SG_NEED
        for (int j = dn; j < cpu_n; j++)
          HEGEL_ASSERT (sg_dma_len (&r->out[k][j]) == 0,
                        "split %d entry %d past mapped nents has dma_len %u", k, j,
                        sg_dma_len (&r->out[k][j]));
#endif
      }
    }
    cursor += want;
  }
#undef FLATBYTE
}

#endif
