/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Shared helpers for Scotch tests.
**
** - scotch_strerror: turn a Scotch return code into a string.
** - scotch_flags_fuzz: draw a random subset of SCOTCH_STRAT* flag bits,
**   one shrinkable bit per documented flag.
** - scotch_strat_part_build / order_build: construct a strategy via the
**   higher-level Build API with fuzzed (flags, partnbr, balance).
** - scotch_build_from_csr: SCOTCH_graphInit + SCOTCH_graphBuild glue.
*/

#ifndef SCOTCH_HELPERS_H
#define SCOTCH_HELPERS_H

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"

static inline const char * scotch_strerror (int rc)
{
    /* Scotch convention: 0 == ok, nonzero == error. The internals don't
    ** expose specific codes; the error handler library prints to stderr.
    ** This keeps our assertion messages readable. */
    return rc == 0 ? "ok" : "error";
}

/* Draw a random subset of the documented SCOTCH_STRAT* flag bits.
** Each flag is one shrinkable bit, so the shrinker can independently
** drop flags from a failing combination.
*/
static inline SCOTCH_Num scotch_flags_fuzz (hegel_testcase * tc)
{
    SCOTCH_Num f = SCOTCH_STRATDEFAULT;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATQUALITY;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATSPEED;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATBALANCE;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATSAFETY;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATSCALABILITY;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATRECURSIVE;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATREMAP;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATLEVELMAX;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATLEVELMIN;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATLEAFSIMPLE;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATSEPASIMPLE;
    if (HEGEL_DRAW_INT (0, 1)) f |= SCOTCH_STRATDISCONNECTED;
    return f;
}

/* Build a SCOTCH_Strat for graph partitioning via the higher-level
** SCOTCH_stratGraphMapBuild API with random flags and balance.
** Returns 0 on success, nonzero on failure. *strat_inited is set to 1
** on success.
*/
static inline int scotch_strat_part_build (hegel_testcase * tc,
                                            SCOTCH_Strat * stradat,
                                            SCOTCH_Num partnbr)
{
    if (SCOTCH_stratInit (stradat) != 0) return -1;
    SCOTCH_Num flags = scotch_flags_fuzz (tc);
    /* Balance ratio in [0, 1] roughly — Scotch documents this as "balance ratio". */
    double balance = (double) HEGEL_DRAW_INT (0, 100) / 100.0;
    int rc = SCOTCH_stratGraphMapBuild (stradat, flags, partnbr, balance);
    if (rc != 0) {
        SCOTCH_stratExit (stradat);
        return rc;
    }
    return 0;
}

/* Build a SCOTCH_Strat for graph ordering via SCOTCH_stratGraphOrderBuild
** with random (flags, partnbr, balance).
*/
static inline int scotch_strat_order_build (hegel_testcase * tc,
                                              SCOTCH_Strat * stradat)
{
    if (SCOTCH_stratInit (stradat) != 0) return -1;
    SCOTCH_Num flags = scotch_flags_fuzz (tc);
    SCOTCH_Num levlnbr = HEGEL_DRAW_INT (0, 10);
    double balance = (double) HEGEL_DRAW_INT (0, 100) / 100.0;
    int rc = SCOTCH_stratGraphOrderBuild (stradat, flags, levlnbr, balance);
    if (rc != 0) {
        SCOTCH_stratExit (stradat);
        return rc;
    }
    return 0;
}

/* Build SCOTCH_Strat for partitioning with overlap. */
static inline int scotch_strat_part_ovl_build (hegel_testcase * tc,
                                                SCOTCH_Strat * stradat,
                                                SCOTCH_Num partnbr)
{
    if (SCOTCH_stratInit (stradat) != 0) return -1;
    SCOTCH_Num flags = scotch_flags_fuzz (tc);
    double balance = (double) HEGEL_DRAW_INT (0, 100) / 100.0;
    int rc = SCOTCH_stratGraphPartOvlBuild (stradat, flags, partnbr, balance);
    if (rc != 0) {
        SCOTCH_stratExit (stradat);
        return rc;
    }
    return 0;
}

/* Build a SCOTCH_Graph from a CSRGraph; returns 0 on success.
** *graf_inited is set to 1 on success so caller's cleanup knows to call exit.
*/
static inline int scotch_build_from_csr (const CSRGraph * g, SCOTCH_Graph * grafdat)
{
    if (SCOTCH_graphInit (grafdat) != 0) return -1;
    int rc = SCOTCH_graphBuild (grafdat, (SCOTCH_Num) g->baseval, g->n,
                                 g->verttab, NULL, NULL, NULL,
                                 g->edgenbr, g->edgetab, NULL);
    if (rc != 0) {
        SCOTCH_graphExit (grafdat);
        return rc;
    }
    return 0;
}

#endif /* SCOTCH_HELPERS_H */
