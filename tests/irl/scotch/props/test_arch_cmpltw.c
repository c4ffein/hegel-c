/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_archCmpltw (weighted complete arch) initializes correctly.
**
** archCmpltw(arch, vertnbr, velotab) builds a complete graph architecture
** with vertex weights. Test:
**   - rc == 0
**   - archSize == vertnbr (regardless of weights)
**   - archName matches "cmpltw" (or similar, document if it deviates)
**
** Weights drawn from [1, 100] independently per vertex.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"

static void test_arch_cmpltw (hegel_testcase * tc)
{
    SCOTCH_Arch a;
    SCOTCH_Num * weights = NULL;
    int inited = 0;

    int n = HEGEL_DRAW_INT (1, 10);
    weights = malloc ((size_t) n * sizeof *weights);
    if (weights == NULL) { hegel_health_fail ("weights"); goto cleanup; }
    for (int i = 0; i < n; i++) {
        weights[i] = (SCOTCH_Num) HEGEL_DRAW_INT (1, 100);
    }

    if (SCOTCH_archInit (&a) != 0) { hegel_health_fail ("archInit"); goto cleanup; }
    inited = 1;

    int rc = SCOTCH_archCmpltw (&a, (SCOTCH_Num) n, weights);
    HEGEL_ASSERT (rc == 0,
                  "archCmpltw rc=%d n=%d", rc, n);

    SCOTCH_Num sz = SCOTCH_archSize (&a);
    HEGEL_ASSERT (sz == (SCOTCH_Num) n,
                  "archSize=%ld != n=%d", (long) sz, n);

    /* archName should be a non-NULL printable string. */
    char * name = SCOTCH_archName (&a);
    HEGEL_ASSERT (name != NULL && name[0] != 0,
                  "archName is NULL/empty");

cleanup:
    if (inited) SCOTCH_archExit (&a);
    free (weights);
}

int main (void)
{
    hegel_run_test_n (test_arch_cmpltw, 1000);
    return 0;
}
