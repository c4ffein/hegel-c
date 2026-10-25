/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: every architecture constructor returns 0 and produces an arch
** whose SCOTCH_archSize agrees with the constructor's size argument.
**
** This is a no-crash + bounds property over the arch zoo: cmplt, hcub,
** mesh2, mesh3, torus2, torus3.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"

enum {
    A_CMPLT  = 0,
    A_HCUB   = 1,
    A_MESH2  = 2,
    A_MESH3  = 3,
    A_TORUS2 = 4,
    A_TORUS3 = 5,
    A_END
};

static const char * arch_name (int t) {
    switch (t) {
    case A_CMPLT:  return "cmplt";
    case A_HCUB:   return "hcub";
    case A_MESH2:  return "mesh2";
    case A_MESH3:  return "mesh3";
    case A_TORUS2: return "torus2";
    case A_TORUS3: return "torus3";
    default:       return "?";
    }
}

static void test_arch_init_exit (hegel_testcase * tc)
{
    SCOTCH_Arch a;
    int inited = 0;

    int kind = HEGEL_DRAW_INT (0, A_END - 1);
    /* Each constructor needs different parameters. Aim small. */
    int p1 = HEGEL_DRAW_INT (1, 8);
    int p2 = HEGEL_DRAW_INT (1, 8);
    int p3 = HEGEL_DRAW_INT (1, 8);
    SCOTCH_Num expected_size = 0;
    int rc = -1;

    if (SCOTCH_archInit (&a) != 0) {
        hegel_health_fail ("archInit"); goto cleanup;
    }
    inited = 1;

    switch (kind) {
    case A_CMPLT:  rc = SCOTCH_archCmplt  (&a, p1); expected_size = p1; break;
    case A_HCUB:   rc = SCOTCH_archHcub   (&a, p1); expected_size = (SCOTCH_Num) 1 << p1; break;
    case A_MESH2:  rc = SCOTCH_archMesh2  (&a, p1, p2); expected_size = (SCOTCH_Num) p1 * p2; break;
    case A_MESH3:  rc = SCOTCH_archMesh3  (&a, p1, p2, p3); expected_size = (SCOTCH_Num) p1 * p2 * p3; break;
    case A_TORUS2: rc = SCOTCH_archTorus2 (&a, p1, p2); expected_size = (SCOTCH_Num) p1 * p2; break;
    case A_TORUS3: rc = SCOTCH_archTorus3 (&a, p1, p2, p3); expected_size = (SCOTCH_Num) p1 * p2 * p3; break;
    }
    HEGEL_ASSERT (rc == 0,
                  "arch %s rc=%d (p1=%d p2=%d p3=%d)",
                  arch_name (kind), rc, p1, p2, p3);

    SCOTCH_Num sz = SCOTCH_archSize (&a);
    HEGEL_ASSERT (sz == expected_size,
                  "arch %s size: got %ld, expected %ld (p1=%d p2=%d p3=%d)",
                  arch_name (kind), (long) sz, (long) expected_size, p1, p2, p3);

cleanup:
    if (inited) SCOTCH_archExit (&a);
}

int main (void)
{
    hegel_run_test_n (test_arch_init_exit, 1000);
    return 0;
}
