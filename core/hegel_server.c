/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** The engine-side request server: hegel__serve_op executes one wire
** request (hegel_internal.h) against libhegel for the current test
** case.  It is the ONLY code that touches the engine on a test body's
** behalf — nofork bodies call it directly, fork-mode children reach it
** through the parent's pipe loop (hegel_runner.c), and a future remote
** target would reach it through a serial / TCP link.
*/

#include "hegel_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Byte buffer ---- */

void hegel__buf_init (hegel_buf * b)
{
  b->data = b->small;
  b->len = 0;
  b->cap = sizeof b->small;
}

void hegel__buf_free (hegel_buf * b)
{
  if (b->data != b->small) free (b->data);
  hegel__buf_init (b);
}

void hegel__buf_put (hegel_buf * b, const void * p, size_t n)
{
  if (b->len + n > b->cap) {
    size_t cap = b->cap * 2;
    while (cap < b->len + n) cap *= 2;
    uint8_t * d = malloc (cap);
    if (!d) abort ();
    memcpy (d, b->data, b->len);
    if (b->data != b->small) free (b->data);
    b->data = d;
    b->cap = cap;
  }
  if (n) memcpy (b->data + b->len, p, n);
  b->len += n;
}

/* ---- Per-case state ---- */

enum { H_FREE = 0, H_COLL, H_REC, H_POOL, H_SM };

typedef struct {
  int    kind;
  void * ptr;
} srv_handle;

struct hegel_case_srv {
  const hg_engine * e;
  hg_tc           * htc;
  int               capture;
  int               stop_seen;
  srv_handle      * handles;
  int               n_handles;
  int               cap_handles;
  hegel_buf         lines;
  char              iso_site[256];
};

hegel_case_srv * hegel__srv_case_new (const hg_engine * e, hg_tc * htc, int capture)
{
  hegel_case_srv * c = calloc (1, sizeof *c);
  if (!c) abort ();
  c->e = e;
  c->htc = htc;
  c->capture = capture;
  hegel__buf_init (&c->lines);
  return c;
}

static void free_handle (hegel_case_srv * c, srv_handle * h)
{
  const hg_engine * e = c->e;
  switch (h->kind) {
    case H_COLL: e->collection_free (e->ctx, h->ptr); break;
    case H_REC:  e->recursion_free (e->ctx, h->ptr);  break;
    case H_POOL: e->pool_free (e->ctx, h->ptr);       break;
    case H_SM:   e->sm_free (e->ctx, h->ptr);         break;
  }
  h->kind = H_FREE;
  h->ptr = NULL;
}

void hegel__srv_case_free (hegel_case_srv * c)
{
  if (!c) return;
  for (int i = 0; i < c->n_handles; i++)
    if (c->handles[i].kind != H_FREE) free_handle (c, &c->handles[i]);
  free (c->handles);
  hegel__buf_free (&c->lines);
  free (c);
}

int hegel__srv_capture (const hegel_case_srv * c)   { return c->capture; }
int hegel__srv_stop_seen (const hegel_case_srv * c) { return c->stop_seen; }

const char * hegel__srv_iso_site (const hegel_case_srv * c)
{
  return c->iso_site[0] ? c->iso_site : NULL;
}

void hegel__srv_add_line (hegel_case_srv * c, const char * line, size_t len)
{
  if (!c->capture) return;
  hegel__buf_put (&c->lines, line, len);
  hegel__buf_put (&c->lines, "\n", 1);
}

char * hegel__srv_take_lines (hegel_case_srv * c)
{
  char * s = malloc (c->lines.len + 1);
  if (!s) abort ();
  memcpy (s, c->lines.data, c->lines.len);
  s[c->lines.len] = '\0';
  hegel__buf_free (&c->lines);
  return s;
}

static int64_t add_handle (hegel_case_srv * c, int kind, void * ptr)
{
  if (c->n_handles == c->cap_handles) {
    c->cap_handles = c->cap_handles ? 2 * c->cap_handles : 16;
    c->handles = realloc (c->handles, (size_t) c->cap_handles * sizeof *c->handles);
    if (!c->handles) abort ();
  }
  c->handles[c->n_handles].kind = kind;
  c->handles[c->n_handles].ptr = ptr;
  return c->n_handles++;
}

/* Look up a live handle of `kind`, or NULL for a stale / wrong id. */
static void * get_handle (hegel_case_srv * c, int64_t id, int kind)
{
  if (id < 0 || id >= c->n_handles || c->handles[id].kind != kind) return NULL;
  return c->handles[id].ptr;
}

/* ---- Run-scoped string-generator caches ----
**
** Generators are immutable and engine-validated at construction, so
** one per distinct (bounds) / (pattern) is built and reused across test
** cases.  Text never contains NUL: drawn strings are C strings, and an
** embedded '\0' would silently truncate them. */

typedef struct {
  uint64_t    min, max;
  char      * pattern;     /* NULL for text generators */
  hg_strgen * gen;
} strgen_entry;

static strgen_entry * g_strgens = NULL;
static int            g_n_strgens = 0;
static hg_strgen    * g_no_nul_alphabet = NULL;

static int make_text_gen (const hg_engine * e, uint64_t min, uint64_t max, hg_strgen ** out)
{
  hg_str none = { NULL, 0 };
  return e->string_generator_text (e->ctx, min, max, NULL, 1, UINT32_MAX,
                                   NULL, 0, NULL, 0, none, none, out);
}

static void remember (uint64_t min, uint64_t max, char * pattern, hg_strgen * gen)
{
  g_strgens = realloc (g_strgens, (size_t) (g_n_strgens + 1) * sizeof *g_strgens);
  if (!g_strgens) abort ();
  g_strgens[g_n_strgens].min = min;
  g_strgens[g_n_strgens].max = max;
  g_strgens[g_n_strgens].pattern = pattern;
  g_strgens[g_n_strgens].gen = gen;
  g_n_strgens++;
}

static int text_gen (const hg_engine * e, uint64_t min, uint64_t max, hg_strgen ** out)
{
  for (int i = 0; i < g_n_strgens; i++)
    if (!g_strgens[i].pattern && g_strgens[i].min == min && g_strgens[i].max == max) {
      *out = g_strgens[i].gen;
      return HG_OK;
    }
  int rc = make_text_gen (e, min, max, out);
  if (rc == HG_OK) remember (min, max, NULL, *out);
  return rc;
}

static int regex_gen (const hg_engine * e, const uint8_t * pat, uint32_t plen, hg_strgen ** out)
{
  for (int i = 0; i < g_n_strgens; i++)
    if (g_strgens[i].pattern && strlen (g_strgens[i].pattern) == plen
        && memcmp (g_strgens[i].pattern, pat, plen) == 0) {
      *out = g_strgens[i].gen;
      return HG_OK;
    }
  if (!g_no_nul_alphabet) {
    int rc = make_text_gen (e, 0, 1, &g_no_nul_alphabet);
    if (rc != HG_OK) return rc;
  }
  int rc = e->string_generator_regex (e->ctx, (hg_str){ (const char *) pat, plen }, true,
                                      g_no_nul_alphabet, out);
  if (rc != HG_OK) return rc;
  char * p = malloc (plen + 1);
  if (!p) abort ();
  memcpy (p, pat, plen);
  p[plen] = '\0';
  remember (0, 0, p, *out);
  return HG_OK;
}

void hegel__srv_run_end (void)
{
  const hg_engine * e = hg_engine_get ();
  for (int i = 0; i < g_n_strgens; i++) {
    if (e) e->string_generator_free (e->ctx, g_strgens[i].gen);
    free (g_strgens[i].pattern);
  }
  free (g_strgens);
  g_strgens = NULL;
  g_n_strgens = 0;
  if (e && g_no_nul_alphabet) e->string_generator_free (e->ctx, g_no_nul_alphabet);
  g_no_nul_alphabet = NULL;
}

/* ---- Request decoding ---- */

typedef struct {
  const uint8_t * p;
  uint32_t        left;
  int             bad;
} rd;

static void rd_get (rd * r, void * out, size_t n)
{
  if (r->left < n) { r->bad = 1; memset (out, 0, n); return; }
  memcpy (out, r->p, n);
  r->p += n;
  r->left -= (uint32_t) n;
}

#define RD(r, T) ({ T _v; rd_get ((r), &_v, sizeof _v); _v; })

/* ---- Serving ---- */

/* Map an engine return code to a response status (ERROR carries the
** engine diagnostic). */
static uint8_t eng_status (hegel_case_srv * c, int rc, const char * what, hegel_buf * resp)
{
  switch (rc) {
    case HG_OK:          return HEGEL_RESP_OK;
    case HG_E_STOP_TEST: c->stop_seen = 1; return HEGEL_RESP_STOP;
    case HG_E_ASSUME:    return HEGEL_RESP_ASSUME;
    case HG_E_RETRY:     return HEGEL_RESP_RETRY;
    default: {
      char msg[2300];
      int n = snprintf (msg, sizeof msg, "hegel-c: %s rejected by the engine (rc=%d): %s",
                        what, rc, hg_last_error (c->e));
      resp->len = 0;
      hegel__buf_put (resp, msg, (size_t) (n < (int) sizeof msg ? n : (int) sizeof msg - 1));
      return HEGEL_RESP_ERROR;
    }
  }
}

static uint8_t usage_error (hegel_buf * resp, const char * msg)
{
  resp->len = 0;
  hegel__buf_put (resp, msg, strlen (msg));
  return HEGEL_RESP_ERROR;
}

/* Unsigned 64-bit draw: the engine's int64 entry point covers ranges
** inside INT64_MAX; beyond that the big-integer entry point takes
** two's-complement little-endian bounds (9 bytes keeps them positive). */
static int draw_u64 (const hg_engine * e, hg_tc * t, uint64_t min, uint64_t max, uint64_t * out)
{
  if (max <= (uint64_t) INT64_MAX) {
    int64_t v;
    int rc = e->generate_integer (e->ctx, t, (int64_t) min, (int64_t) max, &v);
    *out = (uint64_t) v;
    return rc;
  }
  uint8_t lo[9], hi[9], val[9] = {0};
  for (int i = 0; i < 8; i++) {
    lo[i] = (uint8_t) (min >> (8 * i));
    hi[i] = (uint8_t) (max >> (8 * i));
  }
  lo[8] = hi[8] = 0;
  size_t vlen;
  int rc = e->generate_integer_big (e->ctx, t, lo, 9, hi, 9, val, 9, &vlen);
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t) val[i] << (8 * i);
  *out = v;
  return rc;
}

static int draw_float (const hg_engine * e, hg_tc * t, double min, double max, uint32_t width,
                       double * out)
{
  /* A bounded draw never returns NaN; infinities only from an
  ** unbounded end. */
  int allow_inf = isinf (min) || isinf (max);
  double tiny = width == 32 ? 1.401298464324817e-45 : 5e-324;
  return e->generate_float (e->ctx, t, width, min, max, false, allow_inf, false, false,
                            tiny, out);
}

int hegel__serve_op (hegel_case_srv * c, uint8_t op, const uint8_t * payload, uint32_t len,
                     uint8_t * resp_status, hegel_buf * resp, hegel_outcome * out)
{
  const hg_engine * e = c->e;
  hg_context * x = e->ctx;
  hg_tc * t = c->htc;
  rd r = { payload, len, 0 };
  uint8_t st = HEGEL_RESP_OK;
  int rc;

  switch (op) {

    /* ---- Draws ---- */
    case HEGEL_OP_DRAW_INT: {
      int64_t min = RD (&r, int64_t), max = RD (&r, int64_t), v = 0;
      if (min > max) { st = usage_error (resp, "hegel-c: integer draw with min > max"); break; }
      rc = e->generate_integer (x, t, min, max, &v);
      if ((st = eng_status (c, rc, "integer draw", resp)) == HEGEL_RESP_OK) HEGEL_BUF_PUT (resp, v);
      break;
    }
    case HEGEL_OP_DRAW_U64: {
      uint64_t min = RD (&r, uint64_t), max = RD (&r, uint64_t), v = 0;
      if (min > max) { st = usage_error (resp, "hegel-c: u64 draw with min > max"); break; }
      rc = draw_u64 (e, t, min, max, &v);
      if ((st = eng_status (c, rc, "u64 draw", resp)) == HEGEL_RESP_OK) HEGEL_BUF_PUT (resp, v);
      break;
    }
    case HEGEL_OP_DRAW_FLOAT: {
      double min = RD (&r, double), max = RD (&r, double), v = 0;
      uint32_t width = RD (&r, uint32_t);
      rc = draw_float (e, t, min, max, width, &v);
      if ((st = eng_status (c, rc, "float draw", resp)) == HEGEL_RESP_OK) HEGEL_BUF_PUT (resp, v);
      break;
    }
    case HEGEL_OP_DRAW_BOOL: {
      double p = RD (&r, double);
      bool b = false;
      rc = e->generate_boolean (x, t, p, false, false, &b);
      uint8_t v = b;
      if ((st = eng_status (c, rc, "boolean draw", resp)) == HEGEL_RESP_OK) HEGEL_BUF_PUT (resp, v);
      break;
    }
    case HEGEL_OP_DRAW_TEXT:
    case HEGEL_OP_DRAW_REGEX: {
      hg_strgen * g = NULL;
      if (op == HEGEL_OP_DRAW_TEXT) {
        uint64_t min = RD (&r, uint64_t), max = RD (&r, uint64_t);
        rc = text_gen (e, min, max, &g);
      } else {
        rc = regex_gen (e, r.p, r.left, &g);
      }
      if ((st = eng_status (c, rc, op == HEGEL_OP_DRAW_TEXT ? "text generator"
                                                           : "regex pattern", resp)))
        break;
      hg_str s = { NULL, 0 };
      rc = e->generate_string (x, t, g, &s);
      if ((st = eng_status (c, rc, "string draw", resp)) == HEGEL_RESP_OK) {
        hegel__buf_put (resp, s.data, s.len);
        e->owned_string_free (x, &s);
      }
      break;
    }
    case HEGEL_OP_DRAW_BYTES: {
      uint64_t min = RD (&r, uint64_t), max = RD (&r, uint64_t);
      hg_bytes b = { NULL, 0 };
      rc = e->generate_bytes (x, t, min, max, &b);
      if ((st = eng_status (c, rc, "bytes draw", resp)) == HEGEL_RESP_OK) {
        hegel__buf_put (resp, b.data, b.len);
        e->generate_bytes_result_free (x, &b);
      }
      break;
    }

    /* ---- Structure ---- */
    case HEGEL_OP_SPAN_START: {
      uint64_t label = RD (&r, uint64_t);
      st = eng_status (c, e->start_span (x, t, label), "start_span", resp);
      break;
    }
    case HEGEL_OP_SPAN_STOP: {
      uint8_t discard = RD (&r, uint8_t);
      st = eng_status (c, e->stop_span (x, t, discard != 0), "stop_span", resp);
      break;
    }
    case HEGEL_OP_COLL_NEW: {
      uint64_t min = RD (&r, uint64_t), max = RD (&r, uint64_t);
      hg_collection * h = NULL;
      if (min > max) { st = usage_error (resp, "hegel-c: collection with min > max"); break; }
      rc = e->new_collection (x, t, min, max, &h);
      if ((st = eng_status (c, rc, "new_collection", resp)) == HEGEL_RESP_OK) {
        int64_t id = add_handle (c, H_COLL, h);
        HEGEL_BUF_PUT (resp, id);
      }
      break;
    }
    case HEGEL_OP_COLL_MORE:
    case HEGEL_OP_COLL_REJECT:
    case HEGEL_OP_COLL_FREE: {
      int64_t id = RD (&r, int64_t);
      hg_collection * h = get_handle (c, id, H_COLL);
      if (!h) { st = usage_error (resp, "hegel-c: stale or invalid collection handle"); break; }
      if (op == HEGEL_OP_COLL_FREE) { free_handle (c, &c->handles[id]); break; }
      if (op == HEGEL_OP_COLL_REJECT) {
        st = eng_status (c, e->collection_reject (x, t, h, NULL), "collection_reject", resp);
        break;
      }
      bool more = false;
      rc = e->collection_more (x, t, h, &more);
      uint8_t v = more;
      if ((st = eng_status (c, rc, "collection_more", resp)) == HEGEL_RESP_OK)
        HEGEL_BUF_PUT (resp, v);
      break;
    }
    case HEGEL_OP_REC_NEW: {
      uint64_t depth = RD (&r, uint64_t), leaves = RD (&r, uint64_t);
      hg_recursion * h = NULL;
      rc = e->new_recursion (x, t, depth, leaves, &h);
      if ((st = eng_status (c, rc, "new_recursion", resp)) == HEGEL_RESP_OK) {
        int64_t id = add_handle (c, H_REC, h);
        HEGEL_BUF_PUT (resp, id);
      }
      break;
    }
    case HEGEL_OP_REC_BRANCH:
    case HEGEL_OP_REC_LEAF:
    case HEGEL_OP_REC_RETRY:
    case HEGEL_OP_REC_FINISH:
    case HEGEL_OP_REC_FREE: {
      int64_t id = RD (&r, int64_t);
      hg_recursion * h = get_handle (c, id, H_REC);
      if (!h) { st = usage_error (resp, "hegel-c: stale or invalid recursion handle"); break; }
      if (op == HEGEL_OP_REC_FREE) { free_handle (c, &c->handles[id]); break; }
      if (op == HEGEL_OP_REC_BRANCH) {
        uint64_t depth = RD (&r, uint64_t);
        bool b = false;
        rc = e->recursion_branch (x, t, h, depth, &b);
        uint8_t v = b;
        if ((st = eng_status (c, rc, "recursion_branch", resp)) == HEGEL_RESP_OK)
          HEGEL_BUF_PUT (resp, v);
        break;
      }
      rc = op == HEGEL_OP_REC_LEAF  ? e->recursion_leaf (x, t, h)
         : op == HEGEL_OP_REC_RETRY ? e->recursion_retry (x, t, h)
         :                            e->recursion_finish (x, t, h);
      st = eng_status (c, rc, "recursion step", resp);
      break;
    }

    /* ---- Stateful ---- */
    case HEGEL_OP_POOL_NEW: {
      hg_pool * h = NULL;
      rc = e->new_pool (x, t, &h);
      if ((st = eng_status (c, rc, "new_pool", resp)) == HEGEL_RESP_OK) {
        int64_t id = add_handle (c, H_POOL, h);
        HEGEL_BUF_PUT (resp, id);
      }
      break;
    }
    case HEGEL_OP_POOL_ADD:
    case HEGEL_OP_POOL_DRAW:
    case HEGEL_OP_POOL_RESTORE:
    case HEGEL_OP_POOL_FREE: {
      int64_t id = RD (&r, int64_t);
      hg_pool * h = get_handle (c, id, H_POOL);
      if (!h) { st = usage_error (resp, "hegel-c: stale or invalid pool handle"); break; }
      if (op == HEGEL_OP_POOL_FREE) { free_handle (c, &c->handles[id]); break; }
      if (op == HEGEL_OP_POOL_RESTORE) {
        int64_t var = RD (&r, int64_t);
        st = eng_status (c, e->pool_restore (x, t, h, var), "pool_restore", resp);
        break;
      }
      int64_t var = 0;
      if (op == HEGEL_OP_POOL_ADD) {
        rc = e->pool_add (x, t, h, &var);
      } else {
        uint8_t consume = RD (&r, uint8_t);
        rc = e->pool_generate (x, t, h, consume != 0, &var);
      }
      if ((st = eng_status (c, rc, "pool draw", resp)) == HEGEL_RESP_OK) HEGEL_BUF_PUT (resp, var);
      break;
    }
    case HEGEL_OP_SM_NEW: {
      uint32_t nrules = RD (&r, uint32_t), ninvs = RD (&r, uint32_t);
      int64_t steps = RD (&r, int64_t);
      uint32_t n = nrules + ninvs;
      hg_str * names = calloc (n ? n : 1, sizeof *names);
      int64_t * groups = calloc (nrules ? nrules : 1, sizeof *groups);
      bool * always = calloc (ninvs ? ninvs : 1, sizeof *always);
      if (!names || !groups || !always) abort ();
      for (uint32_t i = 0; i < n && !r.bad; i++) {
        uint32_t l = RD (&r, uint32_t);
        if (r.left < l) { r.bad = 1; break; }
        names[i] = (hg_str){ (const char *) r.p, l };
        r.p += l;
        r.left -= l;
      }
      /* Invariants run after every rule — the hegel-c contract. */
      for (uint32_t i = 0; i < ninvs; i++) always[i] = true;
      hg_sm * h = NULL;
      int64_t conc = 0;
      if (r.bad) {
        st = usage_error (resp, "hegel-c: malformed state machine request");
      } else {
        rc = e->new_state_machine (x, t, names, groups, NULL, nrules, names + nrules, always,
                                   ninvs, 1, 1, steps, &h, &conc);
        if ((st = eng_status (c, rc, "new_state_machine", resp)) == HEGEL_RESP_OK) {
          int64_t id = add_handle (c, H_SM, h);
          HEGEL_BUF_PUT (resp, id);
        }
      }
      free (names);
      free (groups);
      free (always);
      break;
    }
    case HEGEL_OP_SM_GROUP:
    case HEGEL_OP_SM_RULE:
    case HEGEL_OP_SM_REJECTED:
    case HEGEL_OP_SM_CHECK_INV:
    case HEGEL_OP_SM_FREE: {
      int64_t id = RD (&r, int64_t);
      hg_sm * h = get_handle (c, id, H_SM);
      if (!h) { st = usage_error (resp, "hegel-c: stale or invalid state machine handle"); break; }
      if (op == HEGEL_OP_SM_FREE) { free_handle (c, &c->handles[id]); break; }
      if (op == HEGEL_OP_SM_REJECTED) {
        st = eng_status (c, e->sm_rule_rejected (x, t, h, 0), "rule_rejected", resp);
        break;
      }
      if (op == HEGEL_OP_SM_CHECK_INV) {
        int64_t inv = RD (&r, int64_t);
        bool b = false;
        rc = e->sm_should_check_invariant (x, t, h, inv, &b);
        uint8_t v = b;
        if ((st = eng_status (c, rc, "should_check_invariant", resp)) == HEGEL_RESP_OK)
          HEGEL_BUF_PUT (resp, v);
        break;
      }
      int64_t v = 0;
      rc = op == HEGEL_OP_SM_GROUP ? e->sm_next_group (x, t, h, &v)
                                   : e->sm_next_rule (x, t, h, 0, &v);
      if ((st = eng_status (c, rc, "state machine step", resp)) == HEGEL_RESP_OK)
        HEGEL_BUF_PUT (resp, v);
      break;
    }

    /* ---- Reporting ---- */
    case HEGEL_OP_TARGET: {
      double v = RD (&r, double);
      hg_str label = { (const char *) r.p, r.left };
      st = eng_status (c, e->target (x, t, v, label), "target", resp);
      break;
    }
    case HEGEL_OP_NOTE:
      hegel__srv_add_line (c, (const char *) payload, len);
      return 1;
    case HEGEL_OP_ISO_SITE: {
      size_t n = len < sizeof c->iso_site - 1 ? len : sizeof c->iso_site - 1;
      memcpy (c->iso_site, payload, n);
      c->iso_site[n] = '\0';
      return 1;
    }

    /* ---- Terminal messages ---- */
    case HEGEL_OP_DONE:
      out->status = c->stop_seen ? HG_STATUS_OVERRUN : HG_STATUS_VALID;
      return 2;
    case HEGEL_OP_STOPPED:
      out->status = HG_STATUS_OVERRUN;
      return 2;
    case HEGEL_OP_ASSUME:
      out->status = HG_STATUS_INVALID;
      return 2;
    case HEGEL_OP_FAIL: {
      uint32_t olen = RD (&r, uint32_t);
      if (olen > r.left) olen = r.left;
      size_t oc = olen < sizeof out->origin - 1 ? olen : sizeof out->origin - 1;
      memcpy (out->origin, r.p, oc);
      out->origin[oc] = '\0';
      size_t ml = r.left - olen;
      size_t mc = ml < sizeof out->msg - 1 ? ml : sizeof out->msg - 1;
      memcpy (out->msg, r.p + olen, mc);
      out->msg[mc] = '\0';
      out->status = HG_STATUS_INTERESTING;
      return 2;
    }
    case HEGEL_OP_HEALTH: {
      size_t mc = len < sizeof out->msg - 1 ? len : sizeof out->msg - 1;
      memcpy (out->msg, payload, mc);
      out->msg[mc] = '\0';
      out->health = 1;
      out->status = HG_STATUS_INVALID;
      return 2;
    }

    default: {
      char msg[96];
      snprintf (msg, sizeof msg, "hegel-c: unknown request op %u", op);
      st = usage_error (resp, msg);
      break;
    }
  }

  if (r.bad && st == HEGEL_RESP_OK) st = usage_error (resp, "hegel-c: truncated request");
  *resp_status = st;
  return 0;
}
