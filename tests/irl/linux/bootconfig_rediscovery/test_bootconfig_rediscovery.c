/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property-based test for the snapshotted lib/bootconfig.c
**
** We exercise the parser along several axes:
**
**   prop_no_crash_random_bytes
**       Random byte buffer (any contents, any size up to XBC_DATA_MAX-1).
**       xbc_init must not crash, must return either >0 (success) or a
**       negative errno.  On error, errmsg must be non-NULL and errpos must
**       either be -1 (non-parser error) or fall in [0, size].
**
**   prop_no_crash_grammar_bytes
**       Same property but the input is sampled from the bootconfig
**       "alphabet" (printable + delimiters: '=' '{' '}' ';' '\n' '#' ','
**       '+' ':' '"' "'" plus identifier chars).  This forces the parser
**       through many more decision paths than purely random bytes (which
**       almost always trip "Non printable value" or "Invalid keyword"
**       very early).
**
**   prop_roundtrip_valid_config
**       Build a small, well-formed key=value tree, serialize to a buffer,
**       parse it, walk back out via xbc_find_value, and check every
**       (key, value) we put in is found with the exact value.
**
**   prop_compose_key_roundtrip
**       After parsing a well-formed config, walk every leaf with
**       xbc_node_find_next_leaf, recompose its key via
**       xbc_node_compose_key, and check xbc_find_node(composed_key)
**       returns the same leaf node.
**
**   prop_idempotent_init_exit
**       xbc_init then xbc_exit, repeated multiple times with different
**       inputs, must keep returning consistent results — in particular the
**       second xbc_init after an xbc_exit must NOT return -EBUSY.
**
**   prop_oversize_input
**       Any size >= XBC_DATA_MAX must return -ERANGE and not parse.
**       Size 0 must return -ERANGE with "empty" message.
*/

/* Include bootconfig FIRST: its userspace stub pulls in ctype/errno/stdint/
** stdio/stdlib/string for us, and pre-shadows the kernel's linux/limits.h
** that would otherwise be picked up via libc's bits/local_lim.h. */
#include <linux/bootconfig.h>

#include "hegel_c.h"

/* The userspace stub copies XBC_DATA_MAX = XBC_VALUE - 1 = 32767. */

/* ------------ helpers ------------ */

/* Fresh-init wrapper: bootconfig has process-wide state, so every call must
** be paired with xbc_exit.  xbc_exit() is safe to call even if xbc_init
** has already cleaned up on error. */
static int
xbc_init_buf (char *buf, size_t size, const char **emsg, int *epos)
{
  /* Make sure no prior test left the parser initialized. */
  xbc_exit ();
  *emsg = NULL;
  *epos = -2;                       /* canary outside the legal range */
  return xbc_init (buf, size, emsg, epos);
}

/* Check the "error position / message well-formedness" invariant. */
static void
check_error_well_formed (int rc, const char *emsg, int epos, size_t size,
                         const char *context)
{
  HEGEL_ASSERT (rc < 0,
                "%s: check_error_well_formed called on success rc=%d", context, rc);
  HEGEL_ASSERT (emsg != NULL,
                "%s: xbc_init returned %d but emsg is NULL", context, rc);
  /* epos == -1 is the documented "not a parser error" sentinel.
  ** epos == -2 means xbc_init never wrote to epos (bug). */
  HEGEL_ASSERT (epos != -2,
                "%s: xbc_init returned %d but never wrote epos (msg=%s)",
                context, rc, emsg);
  if (epos != -1) {
    HEGEL_ASSERT (epos >= 0,
                  "%s: parser error epos=%d < 0 (msg=%s, size=%zu)",
                  context, epos, emsg, size);
    /* epos should be a byte offset into the input buffer.  The parser
    ** copies the buffer into its own arena and appends a NUL — so a
    ** valid in-buffer reference is at most `size` (i.e. it may point to
    ** the appended NUL one past the user-visible last byte). */
    HEGEL_ASSERT ((size_t) epos <= size,
                  "%s: parser error epos=%d > size=%zu (msg=%s)",
                  context, epos, size, emsg);
  }
}

/* ------------ generators ------------ */

/* One byte drawn from the "interesting bootconfig alphabet" — picks
** lexically significant delimiters with much higher probability than
** uniform random bytes. */
static char
draw_grammar_byte (hegel_testcase *tc)
{
  /* The bootconfig.c delimiter set is "{}=+;:\n#" plus ',' for arrays
  ** and quote chars.  Plus whitespace, identifier chars, '.', digits. */
  static const char alphabet[] =
    "abcde"                         /* identifier chars */
    "ABCDE"
    "01"                            /* digits */
    "_-"                            /* identifier punctuation */
    "."                             /* key separator */
    "=+:"                           /* assignment operators */
    "{}"                            /* nesting */
    ";\n"                           /* statement separators */
    "#"                             /* comment */
    ","                             /* array separator */
    "\"'"                           /* quotes */
    " \t"                           /* whitespace */
    "\\"                            /* backslash (no escape, but parser may see) */
    "!@";                           /* misc printable */
  int idx = HEGEL_DRAW_INT (0, (int) sizeof (alphabet) - 2);
  return alphabet[idx];
}

/* ------------ properties ------------ */

/* PROPERTY 1: random bytes never crash the parser. */
static void
prop_no_crash_random_bytes (hegel_testcase *tc)
{
  int size = HEGEL_DRAW_INT (1, 512);
  char *buf = malloc (size);
  if (!buf) { hegel_health_fail ("malloc"); return; }
  for (int i = 0; i < size; i++) {
    /* Uniform 0..255 — includes NULs, control chars, high bytes. */
    buf[i] = (char) HEGEL_DRAW_INT (0, 255);
  }

  const char *emsg;
  int epos;
  int rc = xbc_init_buf (buf, (size_t) size, &emsg, &epos);
  if (rc < 0) check_error_well_formed (rc, emsg, epos, (size_t) size, "random_bytes");
  xbc_exit ();
  free (buf);
}

/* PROPERTY 2: grammar-flavored bytes never crash, and error positions are
** still well-formed. */
static void
prop_no_crash_grammar_bytes (hegel_testcase *tc)
{
  int size = HEGEL_DRAW_INT (1, 1024);
  char *buf = malloc (size);
  if (!buf) { hegel_health_fail ("malloc"); return; }
  for (int i = 0; i < size; i++) buf[i] = draw_grammar_byte (tc);

  const char *emsg;
  int epos;
  int rc = xbc_init_buf (buf, (size_t) size, &emsg, &epos);
  if (rc < 0) check_error_well_formed (rc, emsg, epos, (size_t) size, "grammar_bytes");
  xbc_exit ();
  free (buf);
}

/* PROPERTY 3: well-formed key=value tree round-trips.
**
** We build a small set of distinct keys and their values, serialize as
** "k1 = v1\nk2 = v2\n...", parse, and check every (k, v) pair is
** findable.  We use the trivial identifier alphabet so the keys are
** always valid (alnum + '_').
*/
#define MAX_KV 8

typedef struct {
  char key[16];
  char value[24];
} kv_t;

/* Draw a valid identifier: 1..maxlen chars from alnum + '_'.
** First char must not be a digit (kernel allows it actually, but we
** avoid the ambiguity).
*/
static void
draw_identifier (hegel_testcase *tc, char *out, int maxlen)
{
  static const char first[] = "abcdefghij_";
  static const char rest[]  = "abcdefghij_-012";
  int n = HEGEL_DRAW_INT (1, maxlen - 1);
  out[0] = first[HEGEL_DRAW_INT (0, (int) sizeof (first) - 2)];
  for (int i = 1; i < n; i++) {
    out[i] = rest[HEGEL_DRAW_INT (0, (int) sizeof (rest) - 2)];
  }
  out[n] = '\0';
}

/* Draw a "safe" value: printable ASCII without bootconfig delimiters,
** whitespace, quotes, '#' (comment), or backslash.  Length 0..maxlen-1. */
static void
draw_safe_value (hegel_testcase *tc, char *out, int maxlen)
{
  static const char chars[] = "abcdefghij0123ABCDE";
  int n = HEGEL_DRAW_INT (0, maxlen - 1);
  for (int i = 0; i < n; i++) {
    out[i] = chars[HEGEL_DRAW_INT (0, (int) sizeof (chars) - 2)];
  }
  out[n] = '\0';
}

static void
prop_roundtrip_valid_config (hegel_testcase *tc)
{
  int n = HEGEL_DRAW_INT (1, MAX_KV);
  kv_t kvs[MAX_KV];
  for (int i = 0; i < n; i++) {
    draw_identifier (tc, kvs[i].key, (int) sizeof kvs[i].key);
    draw_safe_value (tc, kvs[i].value, (int) sizeof kvs[i].value);
    /* Avoid duplicate keys: would cause "Value is redefined" on '='. */
    for (int j = 0; j < i; j++) {
      if (strcmp (kvs[i].key, kvs[j].key) == 0) {
        /* Suffix with index to make unique. */
        size_t klen = strlen (kvs[i].key);
        if (klen + 2 < sizeof kvs[i].key) {
          kvs[i].key[klen] = (char) ('0' + i);
          kvs[i].key[klen + 1] = '\0';
        }
        j = -1;                     /* re-check */
      }
    }
  }

  /* Serialize. */
  char buf[4096];
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    int w = snprintf (buf + off, sizeof buf - off, "%s = %s\n",
                      kvs[i].key, kvs[i].value);
    if (w < 0 || (size_t) w >= sizeof buf - off) {
      hegel_health_fail ("buf overflow in roundtrip");
      return;
    }
    off += (size_t) w;
  }

  const char *emsg;
  int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "valid config rejected: rc=%d msg=%s pos=%d input=<<%.*s>>",
                rc, emsg ? emsg : "(null)", epos, (int) off, buf);

  /* Look up each kv. */
  for (int i = 0; i < n; i++) {
    struct xbc_node *vnode = NULL;
    const char *got = xbc_find_value (kvs[i].key, &vnode);
    HEGEL_ASSERT (got != NULL,
                  "key %s not found after parse (input=<<%.*s>>)",
                  kvs[i].key, (int) off, buf);
    HEGEL_ASSERT (strcmp (got, kvs[i].value) == 0,
                  "key %s: got value %s, expected %s (input=<<%.*s>>)",
                  kvs[i].key, got, kvs[i].value, (int) off, buf);
  }
  xbc_exit ();
}

/* PROPERTY 4: compose-key / find-node round-trip on every leaf. */
static void
prop_compose_key_roundtrip (hegel_testcase *tc)
{
  /* Generate a small nested group:
  **   k1 = v1
  **   grp { sub = sv }
  */
  int n = HEGEL_DRAW_INT (1, 4);
  kv_t kvs[8];
  for (int i = 0; i < n; i++) {
    draw_identifier (tc, kvs[i].key, (int) sizeof kvs[i].key);
    draw_safe_value (tc, kvs[i].value, (int) sizeof kvs[i].value);
    for (int j = 0; j < i; j++) {
      if (strcmp (kvs[i].key, kvs[j].key) == 0) {
        size_t klen = strlen (kvs[i].key);
        if (klen + 2 < sizeof kvs[i].key) {
          kvs[i].key[klen] = (char) ('0' + i);
          kvs[i].key[klen + 1] = '\0';
        }
        j = -1;
      }
    }
  }

  char buf[2048];
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    int w = snprintf (buf + off, sizeof buf - off, "%s = %s\n",
                      kvs[i].key, kvs[i].value);
    if (w < 0 || (size_t) w >= sizeof buf - off) {
      hegel_health_fail ("buf overflow"); return;
    }
    off += (size_t) w;
  }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "input rejected: rc=%d msg=%s pos=%d input=<<%.*s>>",
                rc, emsg ? emsg : "(null)", epos, (int) off, buf);

  /* Walk every leaf, compose its key, look it back up. */
  struct xbc_node *leaf = NULL;
  int safety = 0;
  while ((leaf = xbc_node_find_next_leaf (NULL, leaf)) != NULL) {
    char keybuf[512];
    int klen = xbc_node_compose_key (leaf, keybuf, sizeof keybuf);
    HEGEL_ASSERT (klen >= 0,
                  "compose_key returned %d", klen);
    HEGEL_ASSERT ((size_t) klen < sizeof keybuf,
                  "compose_key returned %d, would overflow", klen);
    struct xbc_node *found = xbc_find_node (keybuf);
    HEGEL_ASSERT (found == leaf,
                  "find_node(\"%s\") = %p, expected leaf %p (input=<<%.*s>>)",
                  keybuf, (void *) found, (void *) leaf, (int) off, buf);

    if (++safety > 1024) {
      hegel_health_fail ("leaf walk runaway");
      break;
    }
  }
  xbc_exit ();
}

/* PROPERTY 5: idempotent init/exit. */
static void
prop_idempotent_init_exit (hegel_testcase *tc)
{
  int rounds = HEGEL_DRAW_INT (1, 4);
  for (int r = 0; r < rounds; r++) {
    int size = HEGEL_DRAW_INT (1, 64);
    char buf[64];
    for (int i = 0; i < size; i++) buf[i] = draw_grammar_byte (tc);
    const char *emsg; int epos;
    int rc = xbc_init_buf (buf, (size_t) size, &emsg, &epos);
    if (rc < 0) check_error_well_formed (rc, emsg, epos, (size_t) size,
                                         "idempotent");
    /* Whether success or fail, xbc_exit must allow re-init. */
    xbc_exit ();

    /* Re-init with a known-good buffer and verify it succeeds. */
    const char *good = "x = 1\n";
    rc = xbc_init (good, strlen (good), &emsg, &epos);
    HEGEL_ASSERT (rc > 0,
                  "re-init after exit failed: rc=%d msg=%s",
                  rc, emsg ? emsg : "(null)");
    xbc_exit ();
  }
}

/* PROPERTY 7: deeply nested braces.  Each level pushes onto the
** brace_index stack — at depth >= XBC_DEPTH_MAX (16) the parser must
** reject with "Exceed max depth of braces" and not crash. */
static void
prop_deep_nesting (hegel_testcase *tc)
{
  int depth = HEGEL_DRAW_INT (0, 40);
  /* Generate "g0 { g1 { ... gN { v = x } ... } }" */
  char buf[2048];
  size_t off = 0;
  for (int i = 0; i < depth; i++) {
    int w = snprintf (buf + off, sizeof buf - off, "g%d{", i);
    if (w < 0 || (size_t) w >= sizeof buf - off) { hegel_health_fail ("buf"); return; }
    off += (size_t) w;
  }
  if (off + 16 >= sizeof buf) { hegel_health_fail ("buf"); return; }
  off += (size_t) snprintf (buf + off, sizeof buf - off, "v=x");
  for (int i = 0; i < depth; i++) {
    if (off + 2 >= sizeof buf) { hegel_health_fail ("buf"); return; }
    buf[off++] = '}';
  }
  buf[off++] = '\n';

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  if (rc < 0) {
    check_error_well_formed (rc, emsg, epos, off, "deep_nesting");
  }
  /* Property: if depth <= XBC_DEPTH_MAX-1 it should parse; if much
  ** larger it must reject (we don't know the exact pivot, but the
  ** crash-freedom is the main point). */
  xbc_exit ();
}

/* PROPERTY 7b: unclosed braces.  Some closed sibling blocks followed by
** a chain of nested blocks that are never closed.  The parser must reject
** it, and the reported error position must name one of the keys whose
** brace is actually left open. */
static void
prop_unclosed_brace (hegel_testcase *tc)
{
  int n_closed = HEGEL_DRAW_INT (0, 3);
  int n_open = HEGEL_DRAW_INT (1, 4);
  char buf[512];
  size_t off = 0;
  int open_pos[4];
  for (int i = 0; i < n_closed; i++)
    off += (size_t) snprintf (buf + off, sizeof buf - off, "c%d{v=x}\n", i);
  for (int i = 0; i < n_open; i++) {
    open_pos[i] = (int) off;
    off += (size_t) snprintf (buf + off, sizeof buf - off, "u%d{", i);
  }
  if (HEGEL_DRAW_BOOL ())
    off += (size_t) snprintf (buf + off, sizeof buf - off, "v=x");
  buf[off++] = '\n';

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc < 0, "unclosed braces accepted, rc=%d (input=<<%.*s>>)",
                rc, (int) off, buf);
  check_error_well_formed (rc, emsg, epos, off, "unclosed_brace");
  int names_open = 0;
  for (int i = 0; i < n_open; i++)
    if (epos == open_pos[i]) names_open = 1;
  HEGEL_ASSERT (names_open,
                "unclosed brace error at epos=%d (msg=%s) is not an open key "
                "(input=<<%.*s>>)", epos, emsg, (int) off, buf);
  xbc_exit ();
}

/* PROPERTY 8: very long keys near XBC_KEYLEN_MAX boundary. */
static void
prop_long_keys (hegel_testcase *tc)
{
  /* The verify_tree pass enforces XBC_KEYLEN_MAX = 256 cumulative
  ** along the dotted path.  Draw a single component near the limit. */
  int klen = HEGEL_DRAW_INT (1, 400);
  char buf[1024];
  if (klen + 16 >= (int) sizeof buf) { hegel_health_fail ("buf"); return; }
  for (int i = 0; i < klen; i++) buf[i] = 'a';
  int w = snprintf (buf + klen, sizeof buf - klen, " = v\n");
  size_t total = (size_t) klen + (size_t) w;

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, total, &emsg, &epos);
  if (rc < 0) {
    check_error_well_formed (rc, emsg, epos, total, "long_keys");
  }
  xbc_exit ();
}

/* PROPERTY 9: quoted values containing delimiters. */
static void
prop_quoted_values (hegel_testcase *tc)
{
  /* Value is "<arbitrary bytes>" — should be accepted verbatim provided
  ** content is printable/space and doesn't contain the chosen quote. */
  static const char inside[] = "abc=123,;{}#+: \t.\\!@";
  int n = HEGEL_DRAW_INT (0, (int) sizeof inside - 2);
  char content[64] = {0};
  for (int i = 0; i < n; i++) {
    content[i] = inside[HEGEL_DRAW_INT (0, (int) sizeof inside - 2)];
  }
  /* Choose single or double quote. */
  char q = HEGEL_DRAW_BOOL () ? '"' : '\'';
  /* Ensure content doesn't contain our chosen quote (would close early). */
  for (int i = 0; i < n; i++) if (content[i] == q) content[i] = '_';

  char buf[256];
  int w = snprintf (buf, sizeof buf, "k = %c%s%c\n", q, content, q);
  if (w < 0 || (size_t) w >= sizeof buf) { hegel_health_fail ("buf"); return; }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, (size_t) w, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "quoted-value rejected: rc=%d msg=%s pos=%d input=<<%.*s>>",
                rc, emsg ? emsg : "(null)", epos, w, buf);
  /* Round-trip the value. */
  struct xbc_node *vn = NULL;
  const char *got = xbc_find_value ("k", &vn);
  HEGEL_ASSERT (got != NULL,
                "quoted: key not found, input=<<%.*s>>", w, buf);
  HEGEL_ASSERT (strcmp (got, content) == 0,
                "quoted: got <<%s>>, expected <<%s>> (input=<<%.*s>>)",
                got, content, w, buf);
  xbc_exit ();
}

/* PROPERTY 10: arrays of values. */
static void
prop_arrays (hegel_testcase *tc)
{
  /* "k = a, b, c, ..." */
  int n = HEGEL_DRAW_INT (1, 8);
  char buf[512];
  int off = snprintf (buf, sizeof buf, "k = ");
  char vals[8][8];
  for (int i = 0; i < n; i++) {
    draw_safe_value (tc, vals[i], (int) sizeof vals[i]);
    /* Must be non-empty (the parser accepts empty array elements but
    ** that's a separate property). */
    if (vals[i][0] == '\0') { vals[i][0] = 'x'; vals[i][1] = '\0'; }
    int w = snprintf (buf + off, sizeof buf - off, "%s%s",
                      vals[i], i + 1 < n ? ", " : "\n");
    if (w < 0 || (size_t) w >= sizeof buf - off) { hegel_health_fail ("buf"); return; }
    off += w;
  }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, (size_t) off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "array rejected: rc=%d msg=%s pos=%d input=<<%.*s>>",
                rc, emsg ? emsg : "(null)", epos, off, buf);
  /* Iterate the array and check each value. */
  struct xbc_node *anode = NULL;
  const char *val = xbc_node_find_value (NULL, "k", &anode);
  HEGEL_ASSERT (val != NULL && anode != NULL,
                "array: k not found (input=<<%.*s>>)", off, buf);
  int idx = 0;
  while (anode != NULL) {
    HEGEL_ASSERT (idx < n, "array: too many values (input=<<%.*s>>)", off, buf);
    const char *got = xbc_node_get_data (anode);
    HEGEL_ASSERT (strcmp (got, vals[idx]) == 0,
                  "array[%d]: got <<%s>>, expected <<%s>> (input=<<%.*s>>)",
                  idx, got, vals[idx], off, buf);
    idx++;
    anode = xbc_node_get_child (anode);
  }
  HEGEL_ASSERT (idx == n,
                "array: only got %d values, expected %d (input=<<%.*s>>)",
                idx, n, off, buf);
  xbc_exit ();
}

/* PROPERTY 11: dotted key vs nested-group equivalence.
**   "a.b.c = v\n"   should produce the same tree as
**   "a { b { c = v } }\n"
*/
static void
prop_dotted_vs_nested_equiv (hegel_testcase *tc)
{
  int depth = HEGEL_DRAW_INT (2, 6);
  char names[8][8];
  for (int i = 0; i < depth; i++) {
    /* Use single-letter names to keep things uniform. */
    names[i][0] = (char) ('a' + i);
    names[i][1] = '\0';
  }
  char value[8];
  draw_safe_value (tc, value, (int) sizeof value);
  if (value[0] == '\0') { value[0] = 'x'; value[1] = '\0'; }

  /* Build dotted form. */
  char dotted[256] = {0};
  size_t doff = 0;
  for (int i = 0; i < depth; i++) {
    doff += (size_t) snprintf (dotted + doff, sizeof dotted - doff,
                               i == 0 ? "%s" : ".%s", names[i]);
  }
  doff += (size_t) snprintf (dotted + doff, sizeof dotted - doff,
                             " = %s\n", value);

  /* Parse dotted form, get composed key for the value's parent. */
  const char *emsg; int epos;
  int rc = xbc_init_buf (dotted, doff, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "dotted form rejected: %s pos=%d input=<<%.*s>>",
                emsg ? emsg : "(null)", epos, (int) doff, dotted);

  /* Compose the full key by joining names with '.'. */
  char fullkey[64];
  size_t kpos = 0;
  for (int i = 0; i < depth; i++) {
    kpos += (size_t) snprintf (fullkey + kpos, sizeof fullkey - kpos,
                               i == 0 ? "%s" : ".%s", names[i]);
  }

  struct xbc_node *vn = NULL;
  const char *got = xbc_find_value (fullkey, &vn);
  HEGEL_ASSERT (got != NULL,
                "dotted: key '%s' not found (input=<<%.*s>>)",
                fullkey, (int) doff, dotted);
  HEGEL_ASSERT (strcmp (got, value) == 0,
                "dotted: got '%s', expected '%s' (input=<<%.*s>>)",
                got, value, (int) doff, dotted);
  xbc_exit ();

  /* Now nested form: "a { b { c = v } }\n" */
  char nested[256] = {0};
  size_t noff = 0;
  for (int i = 0; i < depth - 1; i++) {
    noff += (size_t) snprintf (nested + noff, sizeof nested - noff,
                               "%s { ", names[i]);
  }
  noff += (size_t) snprintf (nested + noff, sizeof nested - noff,
                             "%s = %s", names[depth - 1], value);
  for (int i = 0; i < depth - 1; i++) {
    if (noff + 2 >= sizeof nested) { hegel_health_fail ("buf"); return; }
    nested[noff++] = ' '; nested[noff++] = '}';
  }
  nested[noff++] = '\n';

  rc = xbc_init_buf (nested, noff, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "nested form rejected: %s pos=%d input=<<%.*s>>",
                emsg ? emsg : "(null)", epos, (int) noff, nested);

  got = xbc_find_value (fullkey, &vn);
  HEGEL_ASSERT (got != NULL,
                "nested: key '%s' not found (input=<<%.*s>>)",
                fullkey, (int) noff, nested);
  HEGEL_ASSERT (strcmp (got, value) == 0,
                "nested: got '%s', expected '%s' (input=<<%.*s>>)",
                got, value, (int) noff, nested);
  xbc_exit ();
}

/* PROPERTY 12: key-only entries (no '=') terminated by ';' or '\n'.
** "k1\nk2\n" should parse and produce two key nodes with no value. */
static void
prop_key_only (hegel_testcase *tc)
{
  int n = HEGEL_DRAW_INT (1, 6);
  char keys[6][8];
  for (int i = 0; i < n; i++) {
    draw_identifier (tc, keys[i], (int) sizeof keys[i]);
    for (int j = 0; j < i; j++) {
      if (strcmp (keys[i], keys[j]) == 0) {
        size_t kl = strlen (keys[i]);
        if (kl + 2 < sizeof keys[i]) {
          keys[i][kl] = (char) ('0' + i);
          keys[i][kl + 1] = '\0';
        }
        j = -1;
      }
    }
  }
  char buf[256];
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    char sep = HEGEL_DRAW_BOOL () ? ';' : '\n';
    int w = snprintf (buf + off, sizeof buf - off, "%s%c", keys[i], sep);
    if (w < 0 || (size_t) w >= sizeof buf - off) { hegel_health_fail ("buf"); return; }
    off += (size_t) w;
  }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "key-only rejected: %s pos=%d input=<<%.*s>>",
                emsg ? emsg : "(null)", epos, (int) off, buf);
  for (int i = 0; i < n; i++) {
    struct xbc_node *kn = xbc_find_node (keys[i]);
    HEGEL_ASSERT (kn != NULL,
                  "key-only: '%s' not found (input=<<%.*s>>)",
                  keys[i], (int) off, buf);
  }
  xbc_exit ();
}

/* PROPERTY 13: comments are stripped.
**   "# comment\nk = v # trailing\n"
** should parse and k=v should be findable. */
static void
prop_comments (hegel_testcase *tc)
{
  int variant = HEGEL_DRAW_INT (0, 3);
  const char *bufs[] = {
    "# pure comment line\nk = v\n",
    "k = v # trailing\n",
    "k = v\n# trailing only-comment line\n",
    "# c1\n# c2\nk = v\n# c3\n",
  };
  const char *buf = bufs[variant];
  size_t off = strlen (buf);
  char *tmp = strdup (buf);
  if (!tmp) { hegel_health_fail ("strdup"); return; }

  const char *emsg; int epos;
  int rc = xbc_init_buf (tmp, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "comment-variant %d rejected: %s pos=%d input=<<%s>>",
                variant, emsg ? emsg : "(null)", epos, buf);
  struct xbc_node *vn = NULL;
  const char *got = xbc_find_value ("k", &vn);
  HEGEL_ASSERT (got != NULL,
                "comments: k not found (variant=%d input=<<%s>>)", variant, buf);
  HEGEL_ASSERT (strcmp (got, "v") == 0,
                "comments: got '%s', expected 'v' (variant=%d)", got, variant);
  xbc_exit ();
  free (tmp);
}

/* PROPERTY 14: operator variants += and :=
**   "k = 1\nk += 2" should give an array.
**   "k = 1\nk := 2" should override the value.
*/
static void
prop_operators (hegel_testcase *tc)
{
  /* Variant: '+' append, ':' override. */
  bool append = HEGEL_DRAW_BOOL ();
  const char *buf = append ? "k = a\nk += b\n" : "k = a\nk := b\n";
  size_t len = strlen (buf);
  char *tmp = strdup (buf);
  if (!tmp) { hegel_health_fail ("strdup"); return; }
  const char *emsg; int epos;
  int rc = xbc_init_buf (tmp, len, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "op '%c': rejected: %s (input=<<%s>>)",
                append ? '+' : ':', emsg ? emsg : "(null)", buf);
  struct xbc_node *vn = NULL;
  const char *got = xbc_find_value ("k", &vn);
  HEGEL_ASSERT (got != NULL,
                "op '%c': k not found (input=<<%s>>)", append ? '+' : ':', buf);
  if (append) {
    /* Should be an array [a, b]. */
    HEGEL_ASSERT (vn != NULL && xbc_node_is_array (vn),
                  "op '+': expected array, got <<%s>>", got);
    HEGEL_ASSERT (strcmp (got, "a") == 0,
                  "op '+': first value '%s', expected 'a'", got);
    struct xbc_node *next = xbc_node_get_child (vn);
    HEGEL_ASSERT (next != NULL,
                  "op '+': missing second array entry");
    const char *second = xbc_node_get_data (next);
    HEGEL_ASSERT (strcmp (second, "b") == 0,
                  "op '+': second value '%s', expected 'b'", second);
  } else {
    HEGEL_ASSERT (strcmp (got, "b") == 0,
                  "op ':': value '%s', expected 'b' (override)", got);
  }
  xbc_exit ();
  free (tmp);
}

/* PROPERTY 15: key with embedded whitespace around delimiters.
** Bootconfig allows arbitrary whitespace around `=`, `{`, etc.
** "  k  =  v  \n" should parse to k=v. */
static void
prop_whitespace_tolerance (hegel_testcase *tc)
{
  /* Build a config with extra spaces and tabs sprinkled around. */
  int n_spaces[6];
  for (int i = 0; i < 6; i++) n_spaces[i] = HEGEL_DRAW_INT (0, 4);
  char ws[6][8];
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < n_spaces[i]; j++) {
      ws[i][j] = HEGEL_DRAW_BOOL () ? ' ' : '\t';
    }
    ws[i][n_spaces[i]] = '\0';
  }
  char buf[128];
  int w = snprintf (buf, sizeof buf, "%sk%s=%sv%s\n",
                    ws[0], ws[1], ws[2], ws[3]);
  if (w < 0 || (size_t) w >= sizeof buf) { hegel_health_fail ("buf"); return; }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, (size_t) w, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "whitespace rejected: %s pos=%d input=<<%s>>",
                emsg ? emsg : "(null)", epos, buf);
  struct xbc_node *vn = NULL;
  const char *got = xbc_find_value ("k", &vn);
  HEGEL_ASSERT (got != NULL,
                "whitespace: k not found (input=<<%s>>)", buf);
  HEGEL_ASSERT (strcmp (got, "v") == 0,
                "whitespace: got '%s', expected 'v' (input=<<%s>>)", got, buf);
  xbc_exit ();
}

/* PROPERTY 16: same key declared multiple ways, ensure no corruption.
**   "k = 1\nk.sub = 2"  — k has value AND subkey
** Then look both up and check both exist. */
static void
prop_value_and_subkey (hegel_testcase *tc)
{
  /* Variants of "key has value + subkey at same time": */
  int variant = HEGEL_DRAW_INT (0, 3);
  const char *bufs[] = {
    "k = v1\nk.sub = v2\n",
    "k.sub = v2\nk = v1\n",
    "k = v1\nk { sub = v2 }\n",
    "k { sub = v2 }\nk = v1\n",
  };
  const char *buf = bufs[variant];
  size_t len = strlen (buf);
  char *tmp = strdup (buf);
  if (!tmp) { hegel_health_fail ("strdup"); return; }

  const char *emsg; int epos;
  int rc = xbc_init_buf (tmp, len, &emsg, &epos);
  if (rc < 0) {
    /* Some variants might be rejected; if so, check error form. */
    check_error_well_formed (rc, emsg, epos, len, "value+subkey");
    xbc_exit ();
    free (tmp);
    return;
  }
  /* Both should be findable. */
  struct xbc_node *vn = NULL;
  const char *v1 = xbc_find_value ("k", &vn);
  HEGEL_ASSERT (v1 != NULL,
                "variant %d: k value not found (input=<<%s>>)", variant, buf);
  HEGEL_ASSERT (strcmp (v1, "v1") == 0,
                "variant %d: k value '%s', expected 'v1' (input=<<%s>>)",
                variant, v1, buf);
  const char *v2 = xbc_find_value ("k.sub", &vn);
  HEGEL_ASSERT (v2 != NULL,
                "variant %d: k.sub value not found (input=<<%s>>)", variant, buf);
  HEGEL_ASSERT (strcmp (v2, "v2") == 0,
                "variant %d: k.sub value '%s', expected 'v2' (input=<<%s>>)",
                variant, v2, buf);
  xbc_exit ();
  free (tmp);
}

/* PROPERTY 17: "; as delimiter" — multiple statements on one line.
**   "a = 1; b = 2; c = 3"  should give three key=value pairs. */
static void
prop_semicolon_delimited (hegel_testcase *tc)
{
  int n = HEGEL_DRAW_INT (1, 5);
  char buf[256];
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    int w = snprintf (buf + off, sizeof buf - off,
                      i == 0 ? "k%d = v%d" : "; k%d = v%d", i, i);
    if (w < 0 || (size_t) w >= sizeof buf - off) { hegel_health_fail ("buf"); return; }
    off += (size_t) w;
  }
  buf[off++] = '\n';

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "semicolon rejected: %s pos=%d input=<<%.*s>>",
                emsg ? emsg : "(null)", epos, (int) off, buf);
  for (int i = 0; i < n; i++) {
    char k[16], expected[16];
    snprintf (k, sizeof k, "k%d", i);
    snprintf (expected, sizeof expected, "v%d", i);
    struct xbc_node *vn = NULL;
    const char *got = xbc_find_value (k, &vn);
    HEGEL_ASSERT (got != NULL,
                  "semicolon: %s not found (input=<<%.*s>>)", k, (int) off, buf);
    HEGEL_ASSERT (strcmp (got, expected) == 0,
                  "semicolon: %s = '%s', expected '%s'", k, got, expected);
  }
  xbc_exit ();
}

/* PROPERTY 18: structured fuzzer — randomly chooses among
** valid constructs to produce a plausible bootconfig file.
** This is the "grammar-aware bug-finder": its outputs always look
** legit, so any rejection is suspect. */
static void
prop_structured_fuzz (hegel_testcase *tc)
{
  int n_stmts = HEGEL_DRAW_INT (1, 12);
  char buf[4096];
  size_t off = 0;
  /* Track keys we've defined as having a *value* (not subkey) so we
  ** don't redefine them with '='. */
  char defined_value_keys[16][16];
  int n_defined = 0;
  /* Track keys defined only as group (no value). */
  char defined_group_keys[16][16];
  int n_group = 0;

  for (int s = 0; s < n_stmts; s++) {
    char k[16];
    /* Sometimes reuse an existing key (to exercise merging). */
    bool reuse_group = n_group > 0 && HEGEL_DRAW_INT (0, 3) == 0;
    if (reuse_group) {
      int pick = HEGEL_DRAW_INT (0, n_group - 1);
      strcpy (k, defined_group_keys[pick]);
    } else {
      draw_identifier (tc, k, (int) sizeof k);
      /* Ensure uniqueness vs value keys. */
      for (int j = 0; j < n_defined; j++) {
        if (strcmp (k, defined_value_keys[j]) == 0) {
          k[0] = (char) ('z' - s); k[1] = '\0';
          j = -1;
        }
      }
    }

    int kind = HEGEL_DRAW_INT (0, 5);
    char v[16];
    switch (kind) {
    case 0: /* k = v */
      draw_safe_value (tc, v, (int) sizeof v);
      if (v[0] == '\0') { v[0] = 'x'; v[1] = '\0'; }
      off += (size_t) snprintf (buf + off, sizeof buf - off, "%s = %s\n", k, v);
      if (n_defined < 16) {
        strcpy (defined_value_keys[n_defined++], k);
      }
      break;
    case 1: /* k { sub = v } */
      draw_safe_value (tc, v, (int) sizeof v);
      if (v[0] == '\0') { v[0] = 'x'; v[1] = '\0'; }
      off += (size_t) snprintf (buf + off, sizeof buf - off,
                                "%s { sub = %s }\n", k, v);
      if (n_group < 16) {
        strcpy (defined_group_keys[n_group++], k);
      }
      break;
    case 2: /* k # comment */
      off += (size_t) snprintf (buf + off, sizeof buf - off,
                                "%s # some comment\n", k);
      break;
    case 3: /* k.dotted = v */
      draw_safe_value (tc, v, (int) sizeof v);
      if (v[0] == '\0') { v[0] = 'x'; v[1] = '\0'; }
      off += (size_t) snprintf (buf + off, sizeof buf - off,
                                "%s.dotted = %s\n", k, v);
      if (n_group < 16) {
        strcpy (defined_group_keys[n_group++], k);
      }
      break;
    case 4: /* k = a, b, c (array) */
      off += (size_t) snprintf (buf + off, sizeof buf - off,
                                "%s = a, b, c\n", k);
      if (n_defined < 16) {
        strcpy (defined_value_keys[n_defined++], k);
      }
      break;
    case 5: /* # comment line */
      off += (size_t) snprintf (buf + off, sizeof buf - off,
                                "# comment line for stmt %d\n", s);
      break;
    }
    if (off + 32 >= sizeof buf) break;
  }
  if (off == 0) { buf[0] = 'a'; buf[1] = '=';buf[2]='1';buf[3]='\n';off=4; }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  /* This generator can produce semantically-invalid configs (duplicate
  ** value definitions across reuse).  We only require: error reports
  ** are well-formed, parser doesn't crash. */
  if (rc < 0) {
    check_error_well_formed (rc, emsg, epos, off, "structured_fuzz");
  }
  xbc_exit ();
}

/* PROPERTY 19: parse-then-recompose-then-reparse round-trip.
** Reads tree, composes every key, builds a new buffer of "key = value\n"
** lines from the (key, value) pairs found, parses that, checks results
** match. */
static void
prop_serialize_roundtrip (hegel_testcase *tc)
{
  /* Generate a small flat config. */
  int n = HEGEL_DRAW_INT (1, 4);
  char buf[1024];
  size_t off = 0;
  char keys[4][16], vals[4][16];
  for (int i = 0; i < n; i++) {
    draw_identifier (tc, keys[i], (int) sizeof keys[i]);
    draw_safe_value (tc, vals[i], (int) sizeof vals[i]);
    if (vals[i][0] == '\0') { vals[i][0] = 'x'; vals[i][1] = '\0'; }
    for (int j = 0; j < i; j++) {
      if (strcmp (keys[i], keys[j]) == 0) {
        size_t kl = strlen (keys[i]);
        if (kl + 2 < sizeof keys[i]) {
          keys[i][kl] = (char) ('0' + i);
          keys[i][kl + 1] = '\0';
        }
        j = -1;
      }
    }
    off += (size_t) snprintf (buf + off, sizeof buf - off,
                              "%s = %s\n", keys[i], vals[i]);
  }

  const char *emsg; int epos;
  int rc = xbc_init_buf (buf, off, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "first parse rejected: %s (input=<<%.*s>>)",
                emsg ? emsg : "(null)", (int) off, buf);

  /* Use xbc_for_each_key_value to enumerate and rebuild. */
  char rebuilt[2048];
  size_t roff = 0;
  struct xbc_node *knode;
  const char *value;
  int found = 0;
  xbc_for_each_key_value (knode, value) {
    char key[128];
    int kl = xbc_node_compose_key (knode, key, sizeof key);
    HEGEL_ASSERT (kl >= 0,
                  "compose_key returned %d", kl);
    roff += (size_t) snprintf (rebuilt + roff, sizeof rebuilt - roff,
                               "%s = %s\n", key, value);
    found++;
    if (found > 32) { hegel_health_fail ("enumeration runaway"); break; }
  }
  HEGEL_ASSERT (found == n,
                "enumeration: got %d entries, expected %d (input=<<%.*s>>)",
                found, n, (int) off, buf);
  xbc_exit ();

  /* Reparse the rebuilt buffer. */
  rc = xbc_init_buf (rebuilt, roff, &emsg, &epos);
  HEGEL_ASSERT (rc >= 0,
                "reparse rejected: %s (rebuilt=<<%.*s>>)",
                emsg ? emsg : "(null)", (int) roff, rebuilt);
  for (int i = 0; i < n; i++) {
    struct xbc_node *vn = NULL;
    const char *got = xbc_find_value (keys[i], &vn);
    HEGEL_ASSERT (got != NULL,
                  "reparse: '%s' not found (rebuilt=<<%.*s>>)",
                  keys[i], (int) roff, rebuilt);
    HEGEL_ASSERT (strcmp (got, vals[i]) == 0,
                  "reparse: '%s' = '%s', expected '%s' (rebuilt=<<%.*s>>)",
                  keys[i], got, vals[i], (int) roff, rebuilt);
  }
  xbc_exit ();
}

/* PROPERTY 6: oversize/empty inputs are uniformly rejected with -ERANGE. */
static void
prop_oversize_input (hegel_testcase *tc)
{
  /* Pick: size == 0, or size > XBC_DATA_MAX. */
  int variant = HEGEL_DRAW_INT (0, 1);
  size_t size;
  if (variant == 0) {
    size = 0;
  } else {
    /* XBC_DATA_MAX is 32767 in the header. */
    size = (size_t) (XBC_DATA_MAX + 1 + HEGEL_DRAW_INT (0, 1024));
  }
  char *buf = NULL;
  if (size > 0) {
    buf = malloc (size);
    if (!buf) { hegel_health_fail ("malloc"); return; }
    memset (buf, 'a', size);
  }
  const char *emsg; int epos;
  xbc_exit ();
  int rc = xbc_init (buf ? buf : "", size, &emsg, &epos);
  HEGEL_ASSERT (rc == -ERANGE,
                "oversize/empty input rc=%d expected -ERANGE (size=%zu, msg=%s)",
                rc, size, emsg ? emsg : "(null)");
  HEGEL_ASSERT (emsg != NULL,
                "oversize: emsg is NULL");
  xbc_exit ();
  free (buf);
}

/* ------------ main ------------ */

int
main (void)
{
  printf ("bootconfig PBT (rediscovery)...\n");

  int failed = 0;
#define RUN(name_, fn_, n_) do { \
    printf (">>> %s (n=%d)\n", name_, n_); \
    fflush (stdout); \
    int r = hegel_run_test_result_n (fn_, n_); \
    if (r) { printf ("FAILED: %s\n", name_); failed++; } \
    else   { printf ("ok:     %s\n", name_); } \
  } while (0)

  RUN ("no_crash_random_bytes",  prop_no_crash_random_bytes,  500);
  RUN ("no_crash_grammar_bytes", prop_no_crash_grammar_bytes, 1000);
  RUN ("roundtrip_valid_config", prop_roundtrip_valid_config, 300);
  RUN ("compose_key_roundtrip",  prop_compose_key_roundtrip,  300);
  RUN ("idempotent_init_exit",   prop_idempotent_init_exit,   200);
  RUN ("oversize_input",         prop_oversize_input,         100);
  RUN ("deep_nesting",           prop_deep_nesting,           200);
  RUN ("unclosed_brace",         prop_unclosed_brace,         200);
  RUN ("long_keys",              prop_long_keys,              300);
  RUN ("quoted_values",          prop_quoted_values,          300);
  RUN ("arrays",                 prop_arrays,                 300);
  RUN ("dotted_vs_nested_equiv", prop_dotted_vs_nested_equiv, 200);
  RUN ("key_only",               prop_key_only,               200);
  RUN ("comments",               prop_comments,               100);
  RUN ("operators",              prop_operators,              200);
  RUN ("whitespace_tolerance",   prop_whitespace_tolerance,   200);
  RUN ("value_and_subkey",       prop_value_and_subkey,       200);
  RUN ("semicolon_delimited",    prop_semicolon_delimited,    200);
  RUN ("structured_fuzz",        prop_structured_fuzz,        500);
  RUN ("serialize_roundtrip",    prop_serialize_roundtrip,    300);

  printf ("====== %d test(s) failed ======\n", failed);
  return failed ? 1 : 0;
}
