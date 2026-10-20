/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted (buggy) sg_split.c links and a trivial 2-entry
** scatterlist splits without crashing.  Confirms the build chain only —
** does NOT assert the buggy behavior. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/scatterlist.h>

int sg_split (struct scatterlist *in, const int in_mapped_nents,
              const off_t skip, const int nb_splits,
              const size_t *split_sizes,
              struct scatterlist **out, int *out_mapped_nents,
              gfp_t gfp_mask);

int
main (void)
{
  /* Build a 2-entry scatterlist of pretend page-pointers. */
  static char page_a[4096], page_b[4096];
  struct scatterlist in[2];
  sg_init_table (in, 2);
  sg_set_buf (&in[0], page_a + 16, 64);  /* offset=16, length=64 */
  sg_set_buf (&in[1], page_b + 32, 64);  /* offset=32, length=64 */

  size_t sizes[1] = { 96 };
  struct scatterlist *out = NULL;
  int out_nents = 0;
  int rc = sg_split (in, 2, 0, 1, sizes, &out, &out_nents, 0);
  printf ("smoke: sg_split rc=%d, out_nents=%d, out=%p\n", rc, out_nents, (void *) out);
  if (rc == 0 && out) {
    printf ("smoke: out[0].offset=%u, out[1].offset=%u\n",
            out[0].offset, out[1].offset);
    free (out);
  }
  printf ("smoke OK\n");
  return 0;
}
