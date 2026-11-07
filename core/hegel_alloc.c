/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Generator helpers that live entirely in the test body's process:
**
**   hegel_alloc / _realloc / _free — memory owned by the test case.
**     Each block carries a header linking it into tc->allocs (newest
**     first) with a serial number; the case driver frees whatever is
**     left once the case is over, however it ended.  Nothing here is a
**     wire request: in fork mode the blocks belong to the child, which
**     is where the generator runs and where they are used.
**
**   hegel_draw_recursive / hegel_recursive_child — the engine's
**     recursion protocol (branch / leaf / retry / finish) run around a
**     user node function.  An attempt that outgrows its leaf budget is
**     unwound with longjmp and its blocks freed by serial: everything
**     allocated since the attempt began goes.  That is why node
**     functions must allocate with hegel_alloc.
*/

#include "hegel_internal.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Per-case blocks ---- */

#define ALLOC_MAGIC 0x6865676c616c6f63ULL   /* "heglaloc" */

typedef struct hegel__alloc_hdr {
  union {
    struct {
      struct hegel__alloc_hdr * newer;   /* toward tc->allocs' head, or NULL */
      struct hegel__alloc_hdr * older;
      uint64_t                  serial;
      size_t                    size;
      uint64_t                  magic;
    } h;
    max_align_t               align_;    /* the payload after us stays aligned */
  } u;
} hdr_t;

#define HDR(p)  ((hdr_t *) ((char *) (p) - sizeof (hdr_t)))
#define DATA(b) ((void *) ((char *) (b) + sizeof (hdr_t)))

static void alloc_health (hegel_testcase * tc, const char * fmt, size_t n)
{
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg, fmt, n);
  hegel__escape (tc, HEGEL_ESC_HEALTH);
}

static hdr_t * checked_hdr (hegel_testcase * tc, void * p, const char * fn)
{
  hdr_t * b = HDR (p);
  if (b->u.h.magic != ALLOC_MAGIC) {
    char msg[200];
    snprintf (msg, sizeof msg, "hegel-c: %s: pointer was not returned by hegel_alloc "
              "(or was already freed)", fn);
    snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", msg);
    hegel__escape (tc, HEGEL_ESC_HEALTH);
  }
  return b;
}

void * hegel_alloc (hegel_testcase * tc, size_t size)
{
  if (size > SIZE_MAX - sizeof (hdr_t))
    alloc_health (tc, "hegel-c: hegel_alloc: size %zu is too large", size);
  hdr_t * b = calloc (1, sizeof (hdr_t) + size);
  if (!b) alloc_health (tc, "hegel-c: hegel_alloc: out of memory (%zu bytes)", size);
  b->u.h.serial = tc->alloc_serial ++;
  b->u.h.size = size;
  b->u.h.magic = ALLOC_MAGIC;
  b->u.h.older = tc->allocs;
  if (tc->allocs) tc->allocs->u.h.newer = b;
  tc->allocs = b;
  return DATA (b);
}

void * hegel_realloc (hegel_testcase * tc, void * p, size_t size)
{
  if (!p) return hegel_alloc (tc, size);
  hdr_t * b = checked_hdr (tc, p, "hegel_realloc");
  if (size > SIZE_MAX - sizeof (hdr_t))
    alloc_health (tc, "hegel-c: hegel_realloc: size %zu is too large", size);
  size_t old = b->u.h.size;
  hdr_t * nb = realloc (b, sizeof (hdr_t) + size);
  if (!nb) alloc_health (tc, "hegel-c: hegel_realloc: out of memory (%zu bytes)", size);
  /* Same place in the list, same serial: a block made before a mark
  ** stays the outer scope's, however it grows. */
  if (nb->u.h.newer) nb->u.h.newer->u.h.older = nb;
  else               tc->allocs = nb;
  if (nb->u.h.older) nb->u.h.older->u.h.newer = nb;
  if (size > old) memset ((char *) DATA (nb) + old, 0, size - old);
  nb->u.h.size = size;
  return DATA (nb);
}

static void unlink_free (hegel_testcase * tc, hdr_t * b)
{
  if (b->u.h.newer) b->u.h.newer->u.h.older = b->u.h.older;
  else              tc->allocs = b->u.h.older;
  if (b->u.h.older) b->u.h.older->u.h.newer = b->u.h.newer;
  b->u.h.magic = 0;
  free (b);
}

void hegel_free (hegel_testcase * tc, void * p)
{
  if (!p) return;
  unlink_free (tc, checked_hdr (tc, p, "hegel_free"));
}

void hegel__alloc_release (hegel_testcase * tc, uint64_t mark)
{
  /* Newest first, and serials only grow toward the head. */
  while (tc->allocs && tc->allocs->u.h.serial >= mark)
    unlink_free (tc, tc->allocs);
}

/* ---- Recursive values ---- */

struct hegel__recursive {
  hegel_recursion           handle;
  hegel_recursive_fn        node;
  void                    * ctx;
  uint64_t                  depth;      /* of the sub-value being built */
  int                       in_leaf;    /* ... and whether it is a leaf */
  uint64_t                  mark;       /* first serial of the current attempt */
  void                    * value;
  jmp_buf                   unwind;     /* over the leaf budget: back to the top */
  struct hegel__recursive * prev;       /* the draw this one runs inside */
};

static uint64_t node_label (void)
{
  static uint64_t label = 0;
  if (!label) label = hegel_label ("hegel_c.recursive.node");
  return label;
}

/* One sub-value at `depth`: the engine decides branch or leaf, the user's
** function builds it.  Over budget: longjmp to hegel_draw_recursive. */
static void * draw_sub (hegel_testcase * tc, struct hegel__recursive * r, uint64_t depth)
{
  hegel_start_span (tc, node_label ());
  bool branch = hegel_recursion_branch (tc, r->handle, depth);
  if (!branch && hegel_recursion_leaf (tc, r->handle))
    longjmp (r->unwind, 1);           /* the engine closes the spans left open */
  uint64_t saved_depth = r->depth;
  int      saved_leaf  = r->in_leaf;
  r->depth = depth;
  r->in_leaf = !branch;
  void * v = r->node (tc, !branch, r->ctx);
  r->depth = saved_depth;
  r->in_leaf = saved_leaf;
  hegel_stop_span (tc, 0);
  return v;
}

void * hegel_draw_recursive (hegel_testcase * tc, uint64_t max_depth, uint64_t max_leaves,
                             hegel_recursive_fn node, void * ctx)
{
  /* A case block, so a jump out of the case (an engine call stopping the
  ** test, a failed assumption) frees it with everything else; fields
  ** written between setjmp and longjmp must not live in automatic
  ** storage anyway. */
  struct hegel__recursive * r = hegel_alloc (tc, sizeof *r);
  r->node = node;
  r->ctx = ctx;
  r->prev = tc->recursive;
  r->handle = hegel_recursion_new (tc, max_depth, max_leaves);
  r->mark = tc->alloc_serial;
  /* r stays current until it is freed: finish / retry / free may jump
  ** out of the case, and then the driver resets it. */
  tc->recursive = r;

  for (;;) {
    r->depth = 0;
    r->in_leaf = 0;
    if (setjmp (r->unwind) == 0) {
      r->value = draw_sub (tc, r, 0);
      if (!hegel_recursion_finish (tc, r->handle))
        break;
      /* Mispriced: the engine dropped the value; redraw (no retry). */
      hegel__alloc_release (tc, r->mark);
      continue;
    }
    /* Over the leaf budget: drop the partial value, let the engine
    ** retry (it discards the case once attempts run out), redraw. */
    tc->recursive = r;
    hegel__alloc_release (tc, r->mark);
    hegel_recursion_retry (tc, r->handle);
  }

  void * value = r->value;
  hegel_recursion_free (tc, r->handle);
  tc->recursive = r->prev;
  hegel_free (tc, r);
  return value;
}

void * hegel_recursive_child (hegel_testcase * tc)
{
  struct hegel__recursive * r = tc->recursive;
  if (!r) {
    snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
              "hegel-c: hegel_recursive_child called outside a hegel_draw_recursive "
              "node function");
    hegel__escape (tc, HEGEL_ESC_HEALTH);
  }
  if (r->in_leaf) {
    snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
              "hegel-c: hegel_recursive_child called from a leaf (the node function "
              "got leaf = true: build the node without children)");
    hegel__escape (tc, HEGEL_ESC_HEALTH);
  }
  return draw_sub (tc, r, r->depth + 1);
}
