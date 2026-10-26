/* Fake-page inputs for sg_split() and a canonical text form of its output.
**
** sg_split() never reads the data an entry points at; it only copies and adjusts
** scatterlist fields.  So entries can point at made-up page addresses: entry
** lengths can go up to UINT_MAX and totals past 4 GiB without any memory behind
** them.  Every address is printed relative to FBASE, so the same description gives
** the same text on a 64-bit and a 32-bit build when the result is ABI-independent.
**
** No hegel dependency: shared by the 64-bit property tests and exec32.c. */
#ifndef SGFAKE_H
#define SGFAKE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/scatterlist.h>

#ifdef CONFIG_NEED_SG_DMA_LENGTH
#define F_NEED 1
#else
#define F_NEED 0
#endif

#define FMAXN 48
#define FMAXS 16
#define FMAXCH 3
#define FCAP (2 * FMAXN + 4)          /* walk cap for a runaway output list */

#if UINTPTR_MAX > 0xffffffffu
#define FBASE ((uintptr_t) 1 << 44)
#else
#define FBASE ((uintptr_t) 0x10000000u)
#endif

typedef struct {
  int n;
  unsigned len[FMAXN];
  unsigned long long gap[FMAXN];   /* address gap after entry i (0 = contiguous) */
  unsigned off0;                   /* in-page offset of entry 0 */
  int nch, chp[FMAXCH];            /* chain points, increasing, in 1..n-1 */
  int mapped, ndma;
  unsigned dlen[FMAXN];            /* DMA segment s length */
  int dstart[FMAXN];               /* first input entry of segment s */
  long long skip;
  int nb;
  unsigned long long sizes[FMAXS];
} fdesc;

typedef struct {
  struct scatterlist *tabs[FMAXCH + 1];
  struct scatterlist *ent[FMAXN];
  unsigned long long addr[FMAXN];  /* entry address relative to FBASE */
  unsigned long long cum[FMAXN + 1];
} fbuilt;

/* ---------------------------------------------------------------- text form */

static int fdesc_print (const fdesc *d, char *b, size_t cap)
{
  int p = snprintf (b, cap, "%d %u %d", d->n, d->off0, d->nch);
  for (int t = 0; t < d->nch; t++) p += snprintf (b + p, cap - p, " %d", d->chp[t]);
  for (int i = 0; i < d->n; i++) p += snprintf (b + p, cap - p, " %u %llu", d->len[i], d->gap[i]);
  p += snprintf (b + p, cap - p, " %d %d", d->mapped, d->ndma);
  for (int s = 0; s < d->ndma; s++) p += snprintf (b + p, cap - p, " %u %d", d->dlen[s], d->dstart[s]);
  p += snprintf (b + p, cap - p, " %lld %d", d->skip, d->nb);
  for (int k = 0; k < d->nb; k++) p += snprintf (b + p, cap - p, " %llu", d->sizes[k]);
  p += snprintf (b + p, cap - p, "\n");
  return p;
}

static int fdesc_scan (fdesc *d, FILE *f)
{
  memset (d, 0, sizeof *d);
  if (fscanf (f, "%d %u %d", &d->n, &d->off0, &d->nch) != 3) return -1;
  if (d->n < 1 || d->n > FMAXN || d->nch < 0 || d->nch > FMAXCH) return -1;
  for (int t = 0; t < d->nch; t++) if (fscanf (f, "%d", &d->chp[t]) != 1) return -1;
  for (int i = 0; i < d->n; i++) if (fscanf (f, "%u %llu", &d->len[i], &d->gap[i]) != 2) return -1;
  if (fscanf (f, "%d %d", &d->mapped, &d->ndma) != 2 || d->ndma < 0 || d->ndma > FMAXN) return -1;
  for (int s = 0; s < d->ndma; s++) if (fscanf (f, "%u %d", &d->dlen[s], &d->dstart[s]) != 2) return -1;
  if (fscanf (f, "%lld %d", &d->skip, &d->nb) != 2 || d->nb < 1 || d->nb > FMAXS) return -1;
  for (int k = 0; k < d->nb; k++) if (fscanf (f, "%llu", &d->sizes[k]) != 1) return -1;
  return 0;
}

/* ------------------------------------------------------------------- build */

static void fbuild (const fdesc *d, fbuilt *B)
{
  memset (B, 0, sizeof *B);
  unsigned long long a = d->off0;
  for (int i = 0; i < d->n; i++) {
    B->addr[i] = a;
    B->cum[i + 1] = B->cum[i] + d->len[i];
    a += d->len[i] + d->gap[i];
  }
  int lo = 0;
  for (int t = 0; t <= d->nch; t++) {
    int hi = t < d->nch ? d->chp[t] : d->n;
    int cnt = hi - lo + (t < d->nch ? 1 : 0);
    B->tabs[t] = calloc (cnt, sizeof (struct scatterlist));
    sg_init_table (B->tabs[t], cnt);
    for (int i = lo; i < hi; i++) B->ent[i] = &B->tabs[t][i - lo];
    lo = hi;
  }
  for (int t = 0; t < d->nch; t++) {
    int cnt = (t ? d->chp[t] - d->chp[t - 1] : d->chp[t]) + 1;
    sg_chain (B->tabs[t], cnt, B->tabs[t + 1]);
  }
  for (int i = 0; i < d->n; i++) {
    uintptr_t va = FBASE + (uintptr_t) B->addr[i];
    sg_set_page (B->ent[i], (struct page *) (va & ~(uintptr_t) (PAGE_SIZE - 1)), d->len[i],
                 (unsigned) (va & (PAGE_SIZE - 1)));
  }
  if (d->mapped)
    for (int s = 0; s < d->ndma; s++) {
      /* IOVA space = the byte stream: segment s starts at its stream position, so an
      ** IOMMU-style merge of non-contiguous entries is representable. */
      sg_dma_address (B->ent[s]) = (dma_addr_t) (FBASE + (uintptr_t) B->cum[d->dstart[s]]);
#if F_NEED
      sg_dma_len (B->ent[s]) = d->dlen[s];
#endif
    }
}

static void ffree (const fdesc *d, fbuilt *B)
{
  for (int t = 0; t <= d->nch; t++) free (B->tabs[t]);
}

/* ------------------------------------------------------------------ the call */

int sg_split (struct scatterlist *in, const int in_mapped_nents, const off_t skip,
              const int nb_splits, const size_t *split_sizes, struct scatterlist **out,
              int *out_mapped_nents, gfp_t gfp_mask);

#define F_POISON_PTR ((struct scatterlist *) (uintptr_t) 0x5a5a5a50u)
#define F_POISON_N (-777)

typedef struct {
  int rc;
  struct scatterlist *out[FMAXS];
  int on[FMAXS];
} fresult;

/* Exactly-sized heap arrays so ASan sees any access past the caller's arrays. */
static void fcall (const fdesc *d, fbuilt *B, fresult *r)
{
  int nb = d->nb;
  size_t *sz = malloc (nb * sizeof *sz);
  struct scatterlist **out = malloc (nb * sizeof *out);
  int *on = malloc (nb * sizeof *on);
  for (int k = 0; k < nb; k++) {
    sz[k] = (size_t) d->sizes[k];
    out[k] = F_POISON_PTR;
    on[k] = F_POISON_N;
  }
  r->rc = sg_split (B->ent[0], d->mapped ? d->ndma : 0, (off_t) d->skip, nb, sz, out, on, 0);
  for (int k = 0; k < FMAXS; k++) {
    r->out[k] = k < nb ? out[k] : F_POISON_PTR;
    r->on[k] = k < nb ? on[k] : F_POISON_N;
  }
  free (sz);
  free (out);
  free (on);
}

static void ffree_result (const fdesc *d, fresult *r)
{
  if (r->rc) return;
  for (int k = 0; k < d->nb; k++) free (r->out[k]);
}

/* -------------------------------------------------------------- canonical */

static unsigned long long fpos (struct scatterlist *e)
{
  return (unsigned long long) ((uintptr_t) sg_page (e) + e->offset - FBASE);
}

/* rc, then per split: out_mapped_nents, CPU entries (pos,len,flags), DMA entries. */
static int fcanon (const fdesc *d, const fresult *r, char *b, size_t cap)
{
  int p = snprintf (b, cap, "rc=%d", r->rc);
  for (int k = 0; k < d->nb && p < (int) cap - 64; k++) {
    p += snprintf (b + p, cap - p, "\n k%d on=%d", k, r->on[k]);
    if (r->rc || r->out[k] == F_POISON_PTR) {
      p += snprintf (b + p, cap - p, " out=%s", r->out[k] == F_POISON_PTR ? "untouched" : "SET");
      continue;
    }
    if (r->on[k] < 1) { p += snprintf (b + p, cap - p, " (not walked)"); continue; }
    int cn = 0;
    struct scatterlist *e = r->out[k];
    for (;; e++) {
      cn++;
      p += snprintf (b + p, cap - p, " c(%llu,%u%s%s)", fpos (e), e->length,
                     sg_is_last (e) ? ",L" : "", sg_is_chain (e) ? ",CH" : "");
      if (sg_is_last (e)) break;
      if (cn >= FCAP) { p += snprintf (b + p, cap - p, " RUNAWAY"); break; }
    }
    int dn = r->on[k] < cn ? r->on[k] : cn;
    for (int j = 0; j < dn; j++) {
      struct scatterlist *x = &r->out[k][j];
      unsigned long long dp = sg_dma_address (x) ? (unsigned long long) ((uintptr_t) sg_dma_address (x) - FBASE) : 0;
      p += snprintf (b + p, cap - p, " d(%llu,%u)", dp, sg_dma_len (x));
    }
  }
  p += snprintf (b + p, cap - p, "\n");
  return p;
}

#endif
