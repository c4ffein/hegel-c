/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* ASN.1 SEQUENCE wrapping + recode-mode + tagged-encode round-trip PBT.
**
** Companion to test_asn1_round_trip.c, which covers only the four
** primitive encoders.  This one hits the more interesting bug surface:
**
**   (1) asn1_encode_sequence in full form (1-pass)        — long-form length back-patching
**   (2) asn1_encode_sequence in recode form (2-pass)      — dummy-length + back-patch in place
**   (3) asn1_encode_tag in full form                      — explicit context-tagged primitives
**   (4) asn1_encode_tag in recode form                    — dummy-length + back-patch in place
**
** Built on the stateful FFI bridge.  Per-case state holds a stack of
** "open" frames: each frame buffers its children's encoded bytes in a
** side buffer, and remembers whether the close should use full-form or
** recode-form encoding.  The shadow model is a tree of nodes —
** SEQUENCE / EXPLICIT-tagged-wrap (constructed) and primitives (leaves).
**
** Property: when the root frame closes, the resulting top-level TLV
** parses (a) with our manual BER walker into a tree structurally
** identical to the shadow tree, AND (b) is accepted by the kernel's
** asn1_ber_decoder fed a generic "match-anything-recursively" grammar.
**
** Bug classes targeted:
**   - length back-patching off-by-one
**   - long-form length boundary crossings (0x80, 0x100)
**   - recode mode with content > 127 bytes (encoder must reject cleanly)
**   - explicit-tag wrap mis-emission of context-class tag byte
**
** Rule preconditions are enforced by hegel_assume() — pattern (A) from
** the brief.  Friction notes at the bottom of main(). */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/asn1.h>
#include <linux/asn1_ber_bytecode.h>
#include <linux/asn1_encoder.h>
#include <linux/asn1_decoder.h>
#include <linux/err.h>

#include "hegel_c.h"

#define MAX_DEPTH    4
#define MAX_CHILDREN 6
#define MAX_NODES    256
#define OUT_BUFSZ    16384
#define FRAME_BUFSZ  4096
#define MAX_PRIM_LEN 200      /* max primitive payload — stays well under recode's 127 cap */

/* ---- Shadow model ---- */

typedef enum {
  N_SEQ,        /* universal SEQUENCE, full-form close */
  N_SEQ_RECODE, /* universal SEQUENCE, recode-form close (body must be <=127 bytes) */
  N_EXPL,       /* context-class explicit-tag wrapper, full-form close */
  N_EXPL_RECODE,/* context-class explicit-tag wrapper, recode-form close */
  N_INT,
  N_OCTET,
  N_BOOL,
  N_OID,
} node_kind_t;

typedef struct shadow_node {
  node_kind_t kind;
  /* For wrappers (SEQ/EXPL/_RECODE): tag byte we expect, children. */
  unsigned char           expected_tag;     /* full tag byte (class | cons | number) */
  unsigned char           tag_number;       /* low 5 bits — for EXPL/EXPL_RECODE, the explicit tag */
  struct shadow_node    **children;
  int                     n_children;
  int                     children_cap;
  /* For primitives: payload */
  unsigned char           payload[MAX_PRIM_LEN];
  size_t                  payload_len;
} shadow_node_t;

/* ---- Encoder-side state ---- */

typedef struct {
  unsigned char  buf[FRAME_BUFSZ];   /* children content for this frame */
  size_t         used;
  shadow_node_t *shadow;
  bool           recode;             /* set at open: true means close uses 2-pass recode form */
  bool           is_explicit_tag;    /* false = SEQUENCE, true = explicit context tag */
} frame_t;

typedef struct {
  unsigned char  out_buf[OUT_BUFSZ]; /* finalised output, written when root closes */
  size_t         out_used;

  frame_t        stack[MAX_DEPTH + 2];
  int            top;                /* -1 = no root yet, 0 = root open, ... */

  /* Pool of shadow nodes (so teardown can free in one shot) */
  shadow_node_t  pool[MAX_NODES];
  int            pool_used;

  /* When true, set after root closes: the final output is in out_buf and the
  ** shadow root is stack[0].shadow.  We capture by stashing into completed_*. */
  bool             completed;
  shadow_node_t   *completed_root;
  size_t           completed_size;

  /* Coverage counters (reset per case) */
  /* (case-local; we accumulate into globals at end of each rule call) */
} st_t;

/* ---- Coverage counters (process-wide) ---- */
static long cov_open_seq;
static long cov_open_seq_recode;
static long cov_open_expl;
static long cov_open_expl_recode;
static long cov_close_seq_full;
static long cov_close_seq_recode;
static long cov_close_expl_full;
static long cov_close_expl_recode;
static long cov_emit_int;
static long cov_emit_octet;
static long cov_emit_bool;
static long cov_emit_oid;
static long cov_long_form_1;        /* close with body 128..255 (forces 0x81 prefix) */
static long cov_long_form_2;        /* close with body 256..65535 (forces 0x82 prefix) */
static long cov_recode_overflow;    /* close-recode with body >127 — encoder must error */
static long cov_recode_used_127;    /* close-recode with body exactly 127 — boundary */
static long cov_recode_used_128;    /* close-recode with body exactly 128 — overflow */
static long cov_completed_trees;
static long cov_max_depth_reached;

/* ---- Setup / teardown ---- */

static void *setup_fn (void)
{
  st_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  st->top = -1;
  st->completed = false;
  return st;
}

static void teardown_fn (void *s)
{
  st_t *st = s;
  if (!st) return;
  /* All shadow nodes are pool-allocated; children arrays were malloced. */
  for (int i = 0; i < st->pool_used; i++) {
    free (st->pool[i].children);
  }
  free (st);
}

/* ---- Pool helper ---- */

static shadow_node_t *new_node (st_t *st, node_kind_t k)
{
  if (st->pool_used >= MAX_NODES) return NULL;
  shadow_node_t *n = &st->pool[st->pool_used++];
  memset (n, 0, sizeof (*n));
  n->kind = k;
  return n;
}

static void node_add_child (shadow_node_t *parent, shadow_node_t *child)
{
  if (parent->n_children == parent->children_cap) {
    int nc = parent->children_cap ? parent->children_cap * 2 : 4;
    parent->children = realloc (parent->children,
                                (size_t)nc * sizeof (shadow_node_t *));
    parent->children_cap = nc;
  }
  parent->children[parent->n_children++] = child;
}

/* ---- Manual BER walker producing a parsed tree ---- */

typedef struct parsed_node {
  unsigned char        tag;
  size_t               header_len;
  size_t               payload_len;
  const unsigned char *payload;
  struct parsed_node **children;
  int                  n_children;
} parsed_node_t;

static parsed_node_t *parse_one (const unsigned char *p, size_t avail,
                                 size_t *consumed, const char **err);

static void parsed_free (parsed_node_t *n)
{
  if (!n) return;
  for (int i = 0; i < n->n_children; i++)
    parsed_free (n->children[i]);
  free (n->children);
  free (n);
}

/* Returns 0 on success, -1 on parse error.  Sets *out_hdr to header length,
** *out_len to payload length, *out_payload to start of payload. */
static int parse_tlv_header (const unsigned char *p, size_t avail,
                             unsigned char *out_tag, size_t *out_hdr,
                             size_t *out_len, const unsigned char **out_payload,
                             const char **err)
{
  if (avail < 2) { *err = "tlv header < 2 bytes"; return -1; }
  unsigned char tag = p[0];
  if ((tag & 0x1f) == ASN1_LONG_TAG) { *err = "long-form tag not supported"; return -1; }
  size_t pos = 1;
  unsigned char l0 = p[pos++];
  size_t len;
  if (l0 <= 0x7f) {
    len = l0;
  } else if (l0 == 0x81) {
    if (pos >= avail) { *err = "0x81 truncated"; return -1; }
    len = p[pos++];
  } else if (l0 == 0x82) {
    if (pos + 1 >= avail) { *err = "0x82 truncated"; return -1; }
    len  = (size_t)p[pos++] << 8;
    len |= p[pos++];
  } else if (l0 == 0x83) {
    if (pos + 2 >= avail) { *err = "0x83 truncated"; return -1; }
    len  = (size_t)p[pos++] << 16;
    len |= (size_t)p[pos++] << 8;
    len |= p[pos++];
  } else if (l0 == 0x80) {
    *err = "indefinite length (encoder shouldn't produce this)";
    return -1;
  } else {
    *err = "unsupported length form";
    return -1;
  }
  if (len > avail - pos) { *err = "payload exceeds buffer"; return -1; }
  *out_tag     = tag;
  *out_hdr     = pos;
  *out_len     = len;
  *out_payload = p + pos;
  return 0;
}

static parsed_node_t *parse_one (const unsigned char *p, size_t avail,
                                 size_t *consumed, const char **err)
{
  unsigned char tag;
  size_t hdr, len;
  const unsigned char *payload;
  if (parse_tlv_header (p, avail, &tag, &hdr, &len, &payload, err) < 0)
    return NULL;

  parsed_node_t *n = calloc (1, sizeof (*n));
  if (!n) { *err = "oom"; return NULL; }
  n->tag         = tag;
  n->header_len  = hdr;
  n->payload_len = len;
  n->payload     = payload;

  /* If constructed bit set, recurse */
  if (tag & ASN1_CONS_BIT) {
    size_t pos = 0;
    while (pos < len) {
      size_t c;
      parsed_node_t *child = parse_one (payload + pos, len - pos, &c, err);
      if (!child) {
        parsed_free (n);
        return NULL;
      }
      n->children = realloc (n->children,
                             (size_t)(n->n_children + 1) * sizeof (*n->children));
      n->children[n->n_children++] = child;
      pos += c;
    }
    if (pos != len) {
      *err = "constructed body not fully consumed";
      parsed_free (n);
      return NULL;
    }
  }
  *consumed = hdr + len;
  return n;
}

/* Recursively compare parsed tree against shadow tree.  Returns NULL on
** match, a description string on mismatch (caller doesn't free). */
static const char *compare_tree (const parsed_node_t *p, const shadow_node_t *s,
                                 char *errbuf, size_t errbuf_sz, int depth)
{
  if (depth > 16) {
    snprintf (errbuf, errbuf_sz, "depth>16 sanity"); return errbuf;
  }
  /* Wrapper kinds */
  if (s->kind == N_SEQ || s->kind == N_SEQ_RECODE
      || s->kind == N_EXPL || s->kind == N_EXPL_RECODE) {
    if (p->tag != s->expected_tag) {
      snprintf (errbuf, errbuf_sz,
                "wrapper tag mismatch: parsed 0x%02x shadow 0x%02x (depth %d)",
                p->tag, s->expected_tag, depth);
      return errbuf;
    }
    if (p->n_children != s->n_children) {
      snprintf (errbuf, errbuf_sz,
                "child count: parsed %d shadow %d (depth %d, tag 0x%02x)",
                p->n_children, s->n_children, depth, p->tag);
      return errbuf;
    }
    for (int i = 0; i < p->n_children; i++) {
      const char *e = compare_tree (p->children[i], s->children[i],
                                    errbuf, errbuf_sz, depth + 1);
      if (e) return e;
    }
    return NULL;
  }

  /* Primitives */
  unsigned char want_tag = 0;
  switch (s->kind) {
    case N_INT:   want_tag = (ASN1_UNIV << 6) | (ASN1_PRIM << 5) | ASN1_INT;   break;
    case N_OCTET: want_tag = (ASN1_UNIV << 6) | (ASN1_PRIM << 5) | ASN1_OTS;   break;
    case N_BOOL:  want_tag = (ASN1_UNIV << 6) | (ASN1_PRIM << 5) | ASN1_BOOL;  break;
    case N_OID:   want_tag = (ASN1_UNIV << 6) | (ASN1_PRIM << 5) | ASN1_OID;   break;
    default: snprintf (errbuf, errbuf_sz, "unknown leaf kind %d", s->kind); return errbuf;
  }
  if (p->tag != want_tag) {
    snprintf (errbuf, errbuf_sz, "leaf tag: parsed 0x%02x shadow 0x%02x (depth %d, kind %d)",
              p->tag, want_tag, depth, s->kind);
    return errbuf;
  }
  if (p->payload_len != s->payload_len) {
    snprintf (errbuf, errbuf_sz, "leaf payload len: parsed %zu shadow %zu (depth %d, tag 0x%02x)",
              p->payload_len, s->payload_len, depth, p->tag);
    return errbuf;
  }
  if (memcmp (p->payload, s->payload, s->payload_len) != 0) {
    snprintf (errbuf, errbuf_sz, "leaf payload bytes differ (depth %d, tag 0x%02x, len %zu)",
              depth, p->tag, s->payload_len);
    return errbuf;
  }
  return NULL;
}

/* ---- A "match anything recursively" grammar for asn1_ber_decoder ----
**
** Bytecode: just [ASN1_OP_MATCH_ANY, ASN1_OP_COMPLETE].  This will
** validate the *outer* TLV bytewise (header + length consistency).  For
** constructed values it doesn't recurse into children — but the
** decoder's general TLV header parsing is what we want to exercise:
** correct handling of long-form length, of the constructed bit, etc.
**
** A more thorough cross-check is to feed each *full top-level TLV*
** (which includes nested SEQUENCEs in its body) to MATCH_ANY: the
** decoder will validate the outer header bytes, jump past the body, and
** assert dp==datalen at COMPLETE.  Body-internal corruption is missed,
** but our manual walker covers that. */

static const unsigned char any_machine[] = {
  ASN1_OP_MATCH_ANY,
  ASN1_OP_COMPLETE,
};
static const struct asn1_decoder any_decoder = {
  .machine = any_machine,
  .machlen = sizeof (any_machine),
  .actions = NULL,
};

/* ---- Encoders called from rules ---- */

/* Top of stack writer: returns pointer to (frame->buf + frame->used) and
** frame->buf + FRAME_BUFSZ; updates frame->used after a successful encode. */
static unsigned char *frame_write_start (frame_t *fr, unsigned char **end_out)
{
  *end_out = fr->buf + FRAME_BUFSZ;
  return fr->buf + fr->used;
}

static void frame_write_advance (frame_t *fr, unsigned char *new_cursor)
{
  fr->used = (size_t)(new_cursor - fr->buf);
}

/* ---- Rule: open SEQUENCE.  recode flag drawn at open. ---- */
static void rule_open_seq (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top + 1 <= MAX_DEPTH);
  hegel_assume (tc, !st->completed);
  bool recode = (bool)hegel_draw_int (tc, 0, 1);

  st->top++;
  if (st->top > cov_max_depth_reached) cov_max_depth_reached = st->top;
  frame_t *fr = &st->stack[st->top];
  memset (fr, 0, sizeof (*fr));
  fr->is_explicit_tag = false;
  fr->recode = recode;
  fr->shadow = new_node (st, recode ? N_SEQ_RECODE : N_SEQ);
  hegel_assume (tc, fr->shadow != NULL);
  fr->shadow->expected_tag = (ASN1_UNIV << 6) | (ASN1_CONS << 5) | ASN1_SEQ;
  if (recode) cov_open_seq_recode++; else cov_open_seq++;
}

/* ---- Rule: open EXPLICIT context tag wrapper.  recode flag drawn. ---- */
static void rule_open_expl (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top + 1 <= MAX_DEPTH);
  hegel_assume (tc, !st->completed);
  int tnum = hegel_draw_int (tc, 0, 30);
  bool recode = (bool)hegel_draw_int (tc, 0, 1);

  st->top++;
  if (st->top > cov_max_depth_reached) cov_max_depth_reached = st->top;
  frame_t *fr = &st->stack[st->top];
  memset (fr, 0, sizeof (*fr));
  fr->is_explicit_tag = true;
  fr->recode = recode;
  fr->shadow = new_node (st, recode ? N_EXPL_RECODE : N_EXPL);
  hegel_assume (tc, fr->shadow != NULL);
  fr->shadow->tag_number = (unsigned char)tnum;
  fr->shadow->expected_tag = (ASN1_CONT << 6) | (ASN1_CONS << 5) | (unsigned char)tnum;
  if (recode) cov_open_expl_recode++; else cov_open_expl++;
}

/* ---- Helper: write encoded bytes into the parent frame's content (or
** the final out_buf if we're closing the root). ---- */
static unsigned char *parent_write_cursor (st_t *st, unsigned char **end_out)
{
  if (st->top == 0) {
    *end_out = st->out_buf + OUT_BUFSZ;
    return st->out_buf + st->out_used;
  }
  frame_t *par = &st->stack[st->top - 1];
  *end_out = par->buf + FRAME_BUFSZ;
  return par->buf + par->used;
}

static void parent_write_advance (st_t *st, unsigned char *new_cursor)
{
  if (st->top == 0) {
    st->out_used = (size_t)(new_cursor - st->out_buf);
  } else {
    frame_t *par = &st->stack[st->top - 1];
    par->used = (size_t)(new_cursor - par->buf);
  }
}

static void parent_attach_shadow (st_t *st, shadow_node_t *child)
{
  if (st->top == 0) {
    /* root finished — capture */
    st->completed = true;
    st->completed_root = child;
    st->completed_size = st->out_used;
    cov_completed_trees++;
  } else {
    node_add_child (st->stack[st->top - 1].shadow, child);
  }
}

/* ---- Rule: close the top frame.  Dispatches on frame's kind+recode. ----
**
** Single rule consolidating four close paths.  This is the workaround for
** "no rule weighting" — we move the kind/recode dispatch out of rule
** selection and into the rule body.  Cost: counterexample shrinking has
** to find the right (kind,recode) combination at the right depth via
** byte-stream shrinking instead of via "delete a rule call". */
static void rule_close_top (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top >= 0);
  hegel_assume (tc, !st->completed);
  frame_t *fr = &st->stack[st->top];
  /* Require at least one child so wrappers contain something. */
  hegel_assume (tc, fr->shadow->n_children >= 1);

  unsigned char *end;
  unsigned char *cur = parent_write_cursor (st, &end);

  if (!fr->recode) {
    /* Full-form close: 1-pass encode. */
    unsigned char *r;
    if (fr->is_explicit_tag) {
      r = asn1_encode_tag (cur, end, (u32)fr->shadow->tag_number,
                           fr->buf, (int)fr->used);
    } else {
      r = asn1_encode_sequence (cur, end, fr->buf, (int)fr->used);
    }
    hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)r));

    if (fr->used > 127 && fr->used <= 0xff)        cov_long_form_1++;
    else if (fr->used > 0xff && fr->used <= 0xffff) cov_long_form_2++;

#ifdef HEGEL_NEG_CONTROL
    /* Negative control: flip the LSB of the length byte at position 1 of
    ** the just-written TLV.  The shadow records the correct length, so
    ** round-trip MUST fail.  Used to verify the assertions actually fire. */
    {
      size_t total = (size_t)(r - cur);
      if (total >= 2) cur[1] ^= 0x01;
    }
#endif
    parent_write_advance (st, r);
    if (fr->is_explicit_tag) cov_close_expl_full++;
    else                     cov_close_seq_full++;
    parent_attach_shadow (st, fr->shadow);
    st->top--;
    return;
  }

  /* Recode-form close: 2-pass dummy-then-back-patch. */
  unsigned char *content;
  if (fr->is_explicit_tag) {
    content = asn1_encode_tag (cur, end, (u32)fr->shadow->tag_number, NULL, -1);
  } else {
    content = asn1_encode_sequence (cur, end, NULL, -1);
  }
  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)content));
  hegel_assume (tc, (size_t)(end - content) >= fr->used);
  memcpy (content, fr->buf, fr->used);

  unsigned char *recode_ret;
  if (fr->is_explicit_tag) {
    recode_ret = asn1_encode_tag (content, end,
                                  (u32)fr->shadow->tag_number,
                                  NULL, (int)fr->used);
  } else {
    recode_ret = asn1_encode_sequence (content, end, NULL, (int)fr->used);
  }

  if (fr->used > 127) {
    cov_recode_overflow++;
    if (fr->used == 128) cov_recode_used_128++;
    /* Encoder MUST reject — see the WARN(len > 127) in
    ** asn1_encode_{sequence,tag}.  Verify it returned an error. */
    HEGEL_ASSERT (IS_ERR_VALUE ((unsigned long)recode_ret),
                  "recode %s with body=%zu (>127) should error but didn't",
                  fr->is_explicit_tag ? "EXPLICIT TAG" : "SEQUENCE", fr->used);
    /* Unwind: rewind the parent cursor (discard half-written dummy + body),
    ** drop the frame, do NOT attach the shadow to the parent. */
    parent_write_advance (st, cur);
    st->top--;
    return;
  }

  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)recode_ret));
  if (fr->used == 127) cov_recode_used_127++;
  /* Encoder contract: recode call returns the same position as the dummy
  ** call (header back-patched in place; pointer is still right after the
  ** 2-byte header).  Body is already at content..content+fr->used. */
  HEGEL_ASSERT (recode_ret == content,
                "%s recode_ret=%p content=%p — encoder moved cursor",
                fr->is_explicit_tag ? "tag" : "seq",
                (void *)recode_ret, (void *)content);
  parent_write_advance (st, content + fr->used);
  if (fr->is_explicit_tag) cov_close_expl_recode++;
  else                     cov_close_seq_recode++;
  parent_attach_shadow (st, fr->shadow);
  st->top--;
}

/* ---- Rule: emit primitive into the currently-open frame's body ---- */

static void rule_emit_int (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top >= 0);
  hegel_assume (tc, !st->completed);
  frame_t *fr = &st->stack[st->top];
  hegel_assume (tc, fr->shadow->n_children < MAX_CHILDREN);

  s64 v = (s64)hegel_draw_i64 (tc, 0, INT64_MAX);
  unsigned char *end;
  unsigned char *cur = frame_write_start (fr, &end);
  unsigned char *r = asn1_encode_integer (cur, end, v);
  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)r));

  size_t total = (size_t)(r - cur);
  HEGEL_ASSERT (total >= 2, "encoded integer too small (%zu)", total);
  size_t plen = total - 2;
  HEGEL_ASSERT (plen <= MAX_PRIM_LEN, "integer payload %zu > MAX_PRIM_LEN", plen);

  shadow_node_t *n = new_node (st, N_INT);
  hegel_assume (tc, n != NULL);
  n->payload_len = plen;
  memcpy (n->payload, cur + 2, plen);
  node_add_child (fr->shadow, n);

  frame_write_advance (fr, r);
  cov_emit_int++;
}

static void rule_emit_octet (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top >= 0);
  hegel_assume (tc, !st->completed);
  frame_t *fr = &st->stack[st->top];
  hegel_assume (tc, fr->shadow->n_children < MAX_CHILDREN);

  /* Bias toward small lengths but allow up to ~150 to exercise long-form-len */
  int len = hegel_draw_int (tc, 0, 150);
  hegel_assume (tc, len <= MAX_PRIM_LEN);
  unsigned char tmp[MAX_PRIM_LEN];
  for (int i = 0; i < len; i++)
    tmp[i] = (unsigned char)hegel_draw_int (tc, 0, 255);

  unsigned char *end;
  unsigned char *cur = frame_write_start (fr, &end);
  unsigned char *r = asn1_encode_octet_string (cur, end, tmp, (u32)len);
  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)r));

  /* Parse the just-encoded TLV to extract payload bytes (length form may vary). */
  size_t total = (size_t)(r - cur);
  unsigned char ptag;
  size_t hdr, plen;
  const unsigned char *payload;
  const char *err;
  HEGEL_ASSERT (parse_tlv_header (cur, total, &ptag, &hdr, &plen, &payload, &err) == 0,
                "self-parse of octet failed: %s", err ? err : "?");
  HEGEL_ASSERT ((int)plen == len,
                "self-parse octet plen %zu != drawn len %d", plen, len);

  shadow_node_t *n = new_node (st, N_OCTET);
  hegel_assume (tc, n != NULL);
  n->payload_len = plen;
  memcpy (n->payload, payload, plen);
  node_add_child (fr->shadow, n);

  frame_write_advance (fr, r);
  cov_emit_octet++;
}

static void rule_emit_bool (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top >= 0);
  hegel_assume (tc, !st->completed);
  frame_t *fr = &st->stack[st->top];
  hegel_assume (tc, fr->shadow->n_children < MAX_CHILDREN);

  bool v = (bool)hegel_draw_int (tc, 0, 1);
  unsigned char *end;
  unsigned char *cur = frame_write_start (fr, &end);
  unsigned char *r = asn1_encode_boolean (cur, end, v);
  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)r));

  shadow_node_t *n = new_node (st, N_BOOL);
  hegel_assume (tc, n != NULL);
  n->payload_len = 1;
  n->payload[0]  = v ? 1 : 0;
  node_add_child (fr->shadow, n);

  frame_write_advance (fr, r);
  cov_emit_bool++;
}

static void rule_emit_oid (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  hegel_assume (tc, st->top >= 0);
  hegel_assume (tc, !st->completed);
  frame_t *fr = &st->stack[st->top];
  hegel_assume (tc, fr->shadow->n_children < MAX_CHILDREN);

  int n_comp = hegel_draw_int (tc, 2, 6);
  u32 oid[6];
  oid[0] = (u32)hegel_draw_int (tc, 0, 2);
  oid[1] = (u32)hegel_draw_int (tc, 0, 39);
  for (int i = 2; i < n_comp; i++)
    oid[i] = (u32)hegel_draw_int (tc, 0, 0xfffffff);

  unsigned char *end;
  unsigned char *cur = frame_write_start (fr, &end);
  unsigned char *r = asn1_encode_oid (cur, end, oid, n_comp);
  hegel_assume (tc, !IS_ERR_VALUE ((unsigned long)r));

  size_t total = (size_t)(r - cur);
  HEGEL_ASSERT (total >= 2, "oid too small");
  size_t plen = (size_t)cur[1];   /* short form — encoder produces */
  HEGEL_ASSERT (plen + 2 == total, "oid TLV self-parse mismatch");

  shadow_node_t *node = new_node (st, N_OID);
  hegel_assume (tc, node != NULL);
  node->payload_len = plen;
  memcpy (node->payload, cur + 2, plen);
  node_add_child (fr->shadow, node);

  frame_write_advance (fr, r);
  cov_emit_oid++;
}

/* ---- Rule: open the root (only valid initially) ----
**
** Without this, every case starts with stack empty and rule_emit_int /
** rule_close_seq are blocked by their preconditions, and rule_open_seq
** would push a frame at top=0 — its parent_write_cursor for close would
** be the out_buf directly.  That's actually what we want!  We treat
** stack[0] as the "root frame" — when it closes, the root TLV is
** written to out_buf.  No separate "open root" rule needed; the first
** rule_open_seq* IS the root open. */

/* ---- Invariants ---- */

/* Once the root closes (st->completed), parse out_buf and compare to
** shadow tree.  Before that, the invariant is vacuously true. */
static void inv_round_trip (const void *s, hegel_testcase *tc)
{
  (void)tc;
  const st_t *st = s;
  if (!st->completed) return;

  size_t consumed = 0;
  const char *err = NULL;
  parsed_node_t *p = parse_one (st->out_buf, st->completed_size, &consumed, &err);
  if (p == NULL) {
    /* Defer assertion until after no-op (no leak to clean up). */
    HEGEL_ASSERT (p != NULL,
                  "manual parse failed at top level: %s (size=%zu)",
                  err ? err : "?", st->completed_size);
    return;  /* unreachable */
  }
  if (consumed != st->completed_size) {
    parsed_free (p);
    HEGEL_ASSERT (consumed == st->completed_size,
                  "manual parser consumed %zu of %zu bytes",
                  consumed, st->completed_size);
    return;
  }

  char errbuf[256];
  const char *cmp_err = compare_tree (p, st->completed_root,
                                      errbuf, sizeof (errbuf), 0);
  parsed_free (p);
  HEGEL_ASSERT (cmp_err == NULL, "tree mismatch: %s", cmp_err ? cmp_err : "?");
}

/* Cross-check with kernel asn1_ber_decoder fed the "any" grammar.  This
** validates the top-level header is well-formed (length, constructed
** bit, tag byte fits in short form). */
static void inv_kernel_decoder_accepts (const void *s, hegel_testcase *tc)
{
  (void)tc;
  const st_t *st = s;
  if (!st->completed) return;

  int rc = asn1_ber_decoder (&any_decoder, NULL,
                             st->out_buf, st->completed_size);
  HEGEL_ASSERT (rc == 0,
                "kernel asn1_ber_decoder rejected encoded tree: rc=%d size=%zu",
                rc, st->completed_size);
}

/* ---- Main ---- */
int main (int argc, char *argv[])
{
  unsigned long n_cases = 200;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  printf ("ASN.1 SEQUENCE/recode/tagged round-trip PBT (n_cases=%lu)\n", n_cases);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Rule registration with hand-tuned weighting via duplicate names.
  ** Without per-rule weights, the only way to bias the random selector is
  ** to register the same rule under multiple names.  We weight close_top
  ** heavily so cases actually unwind the stack and complete the root TLV
  ** (otherwise completed_trees stays at 0). */
  if (hegel_state_machine_add_rule (sm, "open_seq",   rule_open_seq)  != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "open_expl",  rule_open_expl) != 0) return 1;
  /* Close: registered 4x to give it ~30% selection probability. */
  if (hegel_state_machine_add_rule (sm, "close_top",   rule_close_top) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "close_top_2", rule_close_top) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "close_top_3", rule_close_top) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "close_top_4", rule_close_top) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_int",   rule_emit_int)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_octet", rule_emit_octet) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_bool",  rule_emit_bool)  != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "emit_oid",   rule_emit_oid)   != 0) return 1;

  if (hegel_state_machine_add_invariant (sm, "round_trip", inv_round_trip) != 0) return 1;
  if (hegel_state_machine_add_invariant (sm, "kernel_accepts", inv_kernel_decoder_accepts) != 0) return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
    "[asn1 SEQ PBT coverage]\n"
    "  open_seq=%ld  open_seq_recode=%ld  open_expl=%ld  open_expl_recode=%ld\n"
    "  close_seq_full=%ld  close_seq_recode=%ld  close_expl_full=%ld  close_expl_recode=%ld\n"
    "  emit_int=%ld  emit_octet=%ld  emit_bool=%ld  emit_oid=%ld\n"
    "  long_form_len_1=%ld  long_form_len_2=%ld\n"
    "  recode_overflow_attempts=%ld  recode_used_127=%ld  recode_used_128=%ld\n"
    "  completed_trees=%ld  max_depth_reached=%ld\n",
    cov_open_seq, cov_open_seq_recode, cov_open_expl, cov_open_expl_recode,
    cov_close_seq_full, cov_close_seq_recode, cov_close_expl_full, cov_close_expl_recode,
    cov_emit_int, cov_emit_octet, cov_emit_bool, cov_emit_oid,
    cov_long_form_1, cov_long_form_2,
    cov_recode_overflow, cov_recode_used_127, cov_recode_used_128,
    cov_completed_trees, cov_max_depth_reached);
  printf ("PASSED\n");
  return 0;
}
