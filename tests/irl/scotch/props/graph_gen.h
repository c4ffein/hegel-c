/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Shared input generators for Scotch tests.
**
** Each generator builds a CSR graph (verttab/edgetab + counts) directly into
** caller-provided structures, drawing one shrinkable bit per potential edge
** (or following a regular pattern). The output graph is guaranteed to satisfy
** SCOTCH_graphCheck — simple, symmetric, no self-loops, no duplicate arcs.
**
** Caller owns the malloc'd verttab/edgetab and must free them. Build a
** SCOTCH_Graph from them with SCOTCH_graphBuild(0, n, verttab, NULL, NULL,
** NULL, edgenbr, edgetab, NULL).
**
** The topology selector csr_gen_any() draws a topology choice and dispatches.
** Use it when a single test should run across multiple shapes.
*/

#ifndef SCOTCH_GRAPH_GEN_H
#define SCOTCH_GRAPH_GEN_H

#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"

typedef struct {
    SCOTCH_Num   n;            /* number of vertices */
    SCOTCH_Num   edgenbr;      /* number of arcs (= 2 * undirected edges) */
    SCOTCH_Num * verttab;      /* size n+1 */
    SCOTCH_Num * edgetab;      /* size edgenbr (or 1 if 0) */
    int          baseval;      /* 0 or 1 */
    int          topology;     /* TOPO_* tag for diagnostics */
} CSRGraph;

enum {
    TOPO_RANDOM      = 0,
    TOPO_GRID        = 1,
    TOPO_PATH        = 2,
    TOPO_COMPLETE    = 3,
    TOPO_DISCONNECT  = 4,   /* multiple disjoint random components */
    TOPO_ISOLATED    = 5,   /* n vertices, 0 edges */
    TOPO_STAR        = 6,
    TOPO_CYCLE       = 7,
    TOPO_END
};

/* Free what was malloc'd by csr_gen_*(). NULL-safe. */
static inline void csr_free (CSRGraph * g)
{
    if (g == NULL) return;
    free (g->verttab);
    free (g->edgetab);
    g->verttab = NULL;
    g->edgetab = NULL;
}

/* Build CSR from a symmetric n×n adjacency-matrix bitmap.
** adj must be at least cap*cap bytes; rows are 0..n-1, cap is the row stride.
** Caller-owned out->verttab / out->edgetab will be malloc'd here.
** Returns 0 on success, -1 on malloc failure (call hegel_health_fail).
*/
static inline int csr_from_adj (CSRGraph * out, const char * adj, int n, int cap, int baseval)
{
    SCOTCH_Num edgenbr = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (adj[i*cap + j]) edgenbr++;

    SCOTCH_Num * verttab = (SCOTCH_Num *) malloc ((size_t)(n + 1) * sizeof *verttab);
    SCOTCH_Num * edgetab = (SCOTCH_Num *) malloc ((size_t)(edgenbr > 0 ? edgenbr : 1) * sizeof *edgetab);
    if (verttab == NULL || edgetab == NULL) {
        free (verttab); free (edgetab);
        return -1;
    }

    SCOTCH_Num e = 0;
    for (int i = 0; i < n; i++) {
        verttab[i] = e + (SCOTCH_Num) baseval;
        for (int j = 0; j < n; j++) {
            if (adj[i*cap + j]) edgetab[e++] = (SCOTCH_Num) j + (SCOTCH_Num) baseval;
        }
    }
    verttab[n] = e + (SCOTCH_Num) baseval;

    out->n = (SCOTCH_Num) n;
    out->edgenbr = edgenbr;
    out->verttab = verttab;
    out->edgetab = edgetab;
    out->baseval = baseval;
    return 0;
}

/* Random Erdős–Rényi-like simple graph: one bit per unordered pair (i, j). */
static inline int csr_gen_random (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (1, max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);

    /* Use heap (max_n could be large), 1D row-major. */
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;

    for (int i = 1; i < n; i++) {
        for (int j = 0; j < i; j++) {
            int b = HEGEL_DRAW_INT (0, 1);
            adj[i*n + j] = adj[j*n + i] = (char) b;
        }
    }
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_RANDOM;
    free (adj);
    return rc;
}

/* 2D grid graph: side*side vertices, edges between 4-neighbors. */
static inline int csr_gen_grid (hegel_testcase * tc, CSRGraph * out, int max_side)
{
    int side = HEGEL_DRAW_INT (1, max_side);
    int baseval = HEGEL_DRAW_INT (0, 1);
    int n = side * side;
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;

    for (int r = 0; r < side; r++) {
        for (int c = 0; c < side; c++) {
            int v = r*side + c;
            if (c + 1 < side) {
                int u = v + 1;
                adj[v*n + u] = adj[u*n + v] = 1;
            }
            if (r + 1 < side) {
                int u = v + side;
                adj[v*n + u] = adj[u*n + v] = 1;
            }
        }
    }
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_GRID;
    free (adj);
    return rc;
}

/* Path graph: 0—1—2—...—(n-1). */
static inline int csr_gen_path (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (1, max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;
    for (int i = 0; i + 1 < n; i++) {
        adj[i*n + (i+1)] = adj[(i+1)*n + i] = 1;
    }
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_PATH;
    free (adj);
    return rc;
}

/* Cycle graph: path closed with edge (n-1, 0). n>=3 for cycle, otherwise just path. */
static inline int csr_gen_cycle (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (3, max_n < 3 ? 3 : max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        adj[i*n + j] = adj[j*n + i] = 1;
    }
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_CYCLE;
    free (adj);
    return rc;
}

/* Complete graph K_n. */
static inline int csr_gen_complete (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (1, max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (i != j) adj[i*n + j] = 1;
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_COMPLETE;
    free (adj);
    return rc;
}

/* Star graph: vertex 0 connected to all others, no other edges. */
static inline int csr_gen_star (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (1, max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);
    char * adj = (char *) calloc ((size_t)n * (size_t)n, 1);
    if (adj == NULL) return -1;
    for (int i = 1; i < n; i++) {
        adj[0*n + i] = adj[i*n + 0] = 1;
    }
    int rc = csr_from_adj (out, adj, n, n, baseval);
    out->topology = TOPO_STAR;
    free (adj);
    return rc;
}

/* All isolated vertices: n vertices, 0 edges. */
static inline int csr_gen_isolated (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int n = HEGEL_DRAW_INT (1, max_n);
    int baseval = HEGEL_DRAW_INT (0, 1);
    /* No adjacency needed — directly construct. */
    SCOTCH_Num * verttab = (SCOTCH_Num *) malloc ((size_t)(n + 1) * sizeof *verttab);
    SCOTCH_Num * edgetab = (SCOTCH_Num *) malloc (sizeof *edgetab);
    if (verttab == NULL || edgetab == NULL) {
        free (verttab); free (edgetab);
        return -1;
    }
    for (int i = 0; i <= n; i++) verttab[i] = (SCOTCH_Num) baseval;
    out->n = n;
    out->edgenbr = 0;
    out->verttab = verttab;
    out->edgetab = edgetab;
    out->baseval = baseval;
    out->topology = TOPO_ISOLATED;
    return 0;
}

/* Disconnected: 2-3 random components glued together (no edges between them). */
static inline int csr_gen_disconnected (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int ncomp = HEGEL_DRAW_INT (2, 3);
    int baseval = HEGEL_DRAW_INT (0, 1);
    int sizes[3];
    int total = 0;
    for (int c = 0; c < ncomp; c++) {
        sizes[c] = HEGEL_DRAW_INT (1, max_n / ncomp);
        if (sizes[c] < 1) sizes[c] = 1;
        total += sizes[c];
    }
    if (total < 2) total = 2;     /* defensive */
    char * adj = (char *) calloc ((size_t)total * (size_t)total, 1);
    if (adj == NULL) return -1;
    int offset = 0;
    for (int c = 0; c < ncomp; c++) {
        int sz = sizes[c];
        /* Random edges within this component. */
        for (int i = 1; i < sz; i++) {
            for (int j = 0; j < i; j++) {
                int b = HEGEL_DRAW_INT (0, 1);
                int gi = offset + i, gj = offset + j;
                adj[gi*total + gj] = adj[gj*total + gi] = (char) b;
            }
        }
        offset += sz;
    }
    int rc = csr_from_adj (out, adj, total, total, baseval);
    out->topology = TOPO_DISCONNECT;
    free (adj);
    return rc;
}

/* Topology selector: draws a topology and dispatches. */
static inline int csr_gen_any (hegel_testcase * tc, CSRGraph * out, int max_n)
{
    int top = HEGEL_DRAW_INT (0, TOPO_END - 1);
    switch (top) {
    case TOPO_RANDOM:     return csr_gen_random       (tc, out, max_n);
    case TOPO_GRID:       return csr_gen_grid         (tc, out, max_n < 4 ? 2 : 4);
    case TOPO_PATH:       return csr_gen_path         (tc, out, max_n);
    case TOPO_COMPLETE:   return csr_gen_complete     (tc, out, max_n);
    case TOPO_DISCONNECT: return csr_gen_disconnected (tc, out, max_n);
    case TOPO_ISOLATED:   return csr_gen_isolated     (tc, out, max_n);
    case TOPO_STAR:       return csr_gen_star         (tc, out, max_n);
    case TOPO_CYCLE:      return csr_gen_cycle        (tc, out, max_n);
    default:              return csr_gen_random       (tc, out, max_n);
    }
}

/* Diagnostic name for the topology tag. */
static inline const char * csr_topo_name (int topo)
{
    switch (topo) {
    case TOPO_RANDOM:     return "random";
    case TOPO_GRID:       return "grid";
    case TOPO_PATH:       return "path";
    case TOPO_COMPLETE:   return "complete";
    case TOPO_DISCONNECT: return "disconnected";
    case TOPO_ISOLATED:   return "isolated";
    case TOPO_STAR:       return "star";
    case TOPO_CYCLE:      return "cycle";
    default:              return "?";
    }
}

#endif /* SCOTCH_GRAPH_GEN_H */
