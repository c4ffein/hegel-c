/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted (buggy) bch.c links and bch_init returns a
** valid control struct.  This calls into build_mod8_tables, which is
** where the signed-shift UB lives — UBSAN may emit a runtime report,
** but the smoke test still returns 0 (we're only verifying linkage). */

#include <stdio.h>
#include <stdlib.h>
#include <linux/bch.h>

int
main (void)
{
  struct bch_control *bch = bch_init (8, 4, 0, false);
  printf ("smoke: bch_init(m=8, t=4) -> %p\n", bch);
  if (!bch) return 1;
  printf ("smoke: ecc_bytes=%u, ecc_bits=%u\n",
          bch->ecc_bytes, bch->ecc_bits);
  bch_free (bch);
  printf ("smoke OK\n");
  return 0;
}
