/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: HEGEL_TERMINATED_ARRAY must abort at schema-build
** time when the sentinel value lies within the elem schema's bounded
** range — otherwise a drawn element could equal the sentinel and the
** terminator would silently appear mid-buffer.
**
** Here: sentinel = 0, elem = HEGEL_U8(0, 127).  The elem range
** *includes* the sentinel, so the constructor must abort with an
** actionable message.
**
** Expected: EXIT non-zero (classified as CRASH).
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct { char * s; } CStr;

HEGEL_BINDING (n);

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing HEGEL_TERMINATED_ARRAY sentinel-in-range abort...\n");
  /* elem range [0, 127] *includes* sentinel 0 — schema construction
  ** must abort before main() returns. */
  (void) HEGEL_STRUCT (CStr,
      HEGEL_LET (n, HEGEL_INT (0, 32)),
      HEGEL_TERMINATED_ARRAY (HEGEL_USE (n), HEGEL_U8 (0, 127), 0));

  printf ("BUG: schema construction returned despite sentinel collision\n");
  return (1);
}
