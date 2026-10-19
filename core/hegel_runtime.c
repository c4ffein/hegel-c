/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Test-body runtime: the public primitives (draws, spans, collections,
** recursion, pools, notes, assume/fail, HEGEL_ISOLATED).  Every engine
** operation is a wire request (hegel_internal.h) handed to hegel__call,
** which picks the transport from tc->mode — a direct call into the
** server (INPROC), or the fork parent's pipe (CHILD).  This file never
** touches the engine itself.
*/

#include "hegel_internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

/* ---- Globals (single active test case per process by design) ---- */

hegel_testcase * hegel__current_tc = NULL;
char hegel__fail_msg[1024];
char hegel__fail_origin[512];
void (*hegel__case_setup)(void) = NULL;

void hegel_set_case_setup (void (*setup_fn)(void))
{
  hegel__case_setup = setup_fn;
}

/* ---- Pipe IO ---- */

int hegel__read_full (int fd, void * buf, size_t n)
{
  uint8_t * p = buf;
  while (n > 0) {
    ssize_t r = read (fd, p, n);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) return -1;  /* EOF */
    p += r;
    n -= (size_t)r;
  }
  return 0;
}

int hegel__write_full (int fd, const void * buf, size_t n)
{
  const uint8_t * p = buf;
  while (n > 0) {
    ssize_t r = write (fd, p, n);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += r;
    n -= (size_t)r;
  }
  return 0;
}

static void write_frame (int fd, uint8_t op, const void * payload, uint32_t len)
{
  hegel_msg_header h = { op, {0,0,0}, len };
  if (hegel__write_full (fd, &h, sizeof h) != 0 ||
      (len && hegel__write_full (fd, payload, len) != 0)) {
    /* Parent vanished — nothing useful left to do. */
    _exit (1);
  }
}

/* ---- Terminal outcomes ---- */

void hegel__send_terminal (int fd, int op)
{
  hegel_buf b;
  hegel__buf_init (&b);
  if (op == HEGEL_OP_FAIL) {
    uint32_t olen = (uint32_t) strlen (hegel__fail_origin);
    HEGEL_BUF_PUT (&b, olen);
    hegel__buf_put (&b, hegel__fail_origin, olen);
    hegel__buf_put (&b, hegel__fail_msg, strlen (hegel__fail_msg));
  } else if (op == HEGEL_OP_HEALTH) {
    hegel__buf_put (&b, hegel__fail_msg, strlen (hegel__fail_msg));
  }
  write_frame (fd, (uint8_t) op, b.data, (uint32_t) b.len);
  hegel__buf_free (&b);
}

static int terminal_op (int code)
{
  switch (code) {
    case HEGEL_ESC_FAIL:   return HEGEL_OP_FAIL;
    case HEGEL_ESC_ASSUME: return HEGEL_OP_ASSUME;
    case HEGEL_ESC_STOP:   return HEGEL_OP_STOPPED;
    case HEGEL_ESC_HEALTH: return HEGEL_OP_HEALTH;
    default:               return HEGEL_OP_DONE;
  }
}

/* Escape from the test body with `code`.  In a forked body every code
** except ASSUME-inside-a-rule means "send terminal message and exit";
** in nofork it's a longjmp to whichever handler applies. */
void hegel__escape (hegel_testcase * tc, int code)
{
  /* Rule-level catch: ASSUME inside a stateful rule returns control to
  ** the stateful loop in both modes. */
  if (tc->rule_escape && code == HEGEL_ESC_ASSUME) longjmp (*tc->rule_escape, code);

  if (tc->mode == HEGEL_TC_CHILD || tc->mode == HEGEL_TC_ISO) {
    fflush (NULL);  /* _exit discards stdio buffers */
    hegel__send_terminal (tc->req_fd, terminal_op (code));
    _exit (0);
  }

  if (tc->escape) longjmp (*tc->escape, code);

  /* No driver registered — direct API misuse. */
  fprintf (stderr, "hegel-c: escape (%d) with no active run loop\n", code);
  abort ();
}

static void escape_health (hegel_testcase * tc, const char * msg)
{
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", msg);
  hegel__escape (tc, HEGEL_ESC_HEALTH);
}

/* ---- Transport ---- */

void hegel__send (hegel_testcase * tc, uint8_t op, const void * payload, uint32_t len)
{
  if (tc->mode == HEGEL_TC_INPROC) {
    uint8_t st;
    hegel_outcome dummy;
    hegel__serve_op (tc->srv, op, payload, len, &st, NULL, &dummy);
    return;
  }
  write_frame (tc->req_fd, op, payload, len);
}

int hegel__call (hegel_testcase * tc, uint8_t op, const void * payload, uint32_t len,
                 hegel_buf * resp, int raw)
{
  if (tc->iso_site || tc->mode == HEGEL_TC_ISO) {
    char msg[512];
    snprintf (msg, sizeof msg,
              "hegel-c: engine call (op %u) inside HEGEL_ISOLATED at %s — draw everything "
              "before the block (or control left the block via break/return)",
              op, tc->iso_site ? tc->iso_site : "?");
    escape_health (tc, msg);
  }

  hegel_buf scratch;
  hegel__buf_init (&scratch);
  hegel_buf * out = resp ? resp : &scratch;
  out->len = 0;
  uint8_t st;

  if (tc->mode == HEGEL_TC_INPROC) {
    hegel_outcome dummy;
    hegel__serve_op (tc->srv, op, payload, len, &st, out, &dummy);
  } else {
    write_frame (tc->req_fd, op, payload, len);
    hegel_msg_header h;
    if (hegel__read_full (tc->resp_fd, &h, sizeof h) != 0) _exit (1);
    st = h.op;
    uint8_t chunk[256];
    uint32_t left = h.len;
    while (left) {
      uint32_t n = left < sizeof chunk ? left : (uint32_t) sizeof chunk;
      if (hegel__read_full (tc->resp_fd, chunk, n) != 0) _exit (1);
      hegel__buf_put (out, chunk, n);
      left -= n;
    }
  }

  if (!raw) {
    if (st == HEGEL_RESP_STOP) {
      hegel__buf_free (&scratch);
      tc->stop_seen = 1;
      hegel__escape (tc, HEGEL_ESC_STOP);
    }
    if (st == HEGEL_RESP_ASSUME) {
      hegel__buf_free (&scratch);
      hegel__escape (tc, HEGEL_ESC_ASSUME);
    }
    if (st == HEGEL_RESP_ERROR) {
      size_t n = out->len < sizeof hegel__fail_msg - 1 ? out->len : sizeof hegel__fail_msg - 1;
      memcpy (hegel__fail_msg, out->data, n);
      hegel__fail_msg[n] = '\0';
      hegel__buf_free (&scratch);
      hegel__escape (tc, HEGEL_ESC_HEALTH);
    }
  }
  hegel__buf_free (&scratch);
  return st;
}

/* Typed response readers. */
static int64_t resp_i64 (const hegel_buf * b)
{
  int64_t v = 0;
  if (b->len >= sizeof v) memcpy (&v, b->data, sizeof v);
  return v;
}

static int resp_u8 (const hegel_buf * b)
{
  return b->len >= 1 ? b->data[0] : 0;
}

/* Call an op whose payload is `n` int64-sized fields. */
static int call_ids (hegel_testcase * tc, uint8_t op, hegel_buf * resp, int n,
                     int64_t a, int64_t b)
{
  int64_t p[2] = { a, b };
  return hegel__call (tc, op, p, (uint32_t) (n * sizeof (int64_t)), resp, 0);
}

/* ---- Captured output: notes + draw traces ----
**
** One funnel for every line the runtime emits for a stamped (capture)
** test case.  The line goes to the server as a NOTE frame — in fork
** mode that frame is durable in the pipe the moment write() returns, so
** the line survives even if the child segfaults right after.  The
** runner prints a failure's captured lines when it reports it. */

void hegel__emit_line (hegel_testcase * tc, const char * s)
{
  if (!tc->capture) return;
  hegel__send (tc, HEGEL_OP_NOTE, s, (uint32_t) strlen (s));
}

static void trace (hegel_testcase * tc, const char * fmt, ...)
  __attribute__ ((format (printf, 2, 3)));

static void trace (hegel_testcase * tc, const char * fmt, ...)
{
  if (!tc->capture || tc->silent) return;
  char small[256];
  int pre = snprintf (small, sizeof small, "Draw %ld: ", tc->draw_count);
  va_list ap;
  va_start (ap, fmt);
  int n = vsnprintf (small + pre, sizeof small - (size_t) pre, fmt, ap);
  va_end (ap);
  if (pre + n < (int) sizeof small) {
    hegel__emit_line (tc, small);
    return;
  }
  char * big = malloc ((size_t) (pre + n + 1));
  if (!big) return;
  memcpy (big, small, (size_t) pre);
  va_start (ap, fmt);
  vsnprintf (big + pre, (size_t) n + 1, fmt, ap);
  va_end (ap);
  hegel__emit_line (tc, big);
  free (big);
}

/* ---- Public primitive draws ---- */

static int64_t draw_integer (hegel_testcase * tc, int64_t min, int64_t max)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_DRAW_INT, &r, 2, min, max);
  int64_t v = resp_i64 (&r);
  hegel__buf_free (&r);
  trace (tc, "%" PRId64, v);
  return v;
}

int hegel_draw_int (hegel_testcase * tc, int min_val, int max_val)
{
  return (int)draw_integer (tc, min_val, max_val);
}

int64_t hegel_draw_i64 (hegel_testcase * tc, int64_t min_val, int64_t max_val)
{
  return draw_integer (tc, min_val, max_val);
}

uint64_t hegel_draw_u64 (hegel_testcase * tc, uint64_t min_val, uint64_t max_val)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_DRAW_U64, &r, 2, (int64_t) min_val, (int64_t) max_val);
  uint64_t v = (uint64_t) resp_i64 (&r);
  hegel__buf_free (&r);
  trace (tc, "%" PRIu64, v);
  return v;
}

size_t hegel_draw_usize (hegel_testcase * tc, size_t min_val, size_t max_val)
{
  return (size_t)hegel_draw_u64 (tc, min_val, max_val);
}

static double draw_floating (hegel_testcase * tc, double min, double max, uint32_t width)
{
  if (!tc->silent) tc->draw_count++;
  struct __attribute__ ((packed)) { double min, max; uint32_t width; } p = { min, max, width };
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_DRAW_FLOAT, &p, sizeof p, &r, 0);
  double v = 0;
  if (r.len >= sizeof v) memcpy (&v, r.data, sizeof v);
  hegel__buf_free (&r);
  trace (tc, "%g", v);
  return v;
}

float hegel_draw_float (hegel_testcase * tc, float min_val, float max_val)
{
  return (float)draw_floating (tc, (double)min_val, (double)max_val, 32);
}

double hegel_draw_double (hegel_testcase * tc, double min_val, double max_val)
{
  return draw_floating (tc, min_val, max_val, 64);
}

bool hegel_draw_bool (hegel_testcase * tc)
{
  if (!tc->silent) tc->draw_count++;
  double p = 0.5;
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_DRAW_BOOL, &p, sizeof p, &r, 0);
  bool v = resp_u8 (&r) != 0;
  hegel__buf_free (&r);
  trace (tc, "%s", v ? "true" : "false");
  return v;
}

/* Copy a drawn string into the caller's buffer, capped. */
static int copy_out (const hegel_buf * r, void * buf, int cap, int nul_terminate)
{
  int room = nul_terminate ? (cap > 0 ? cap - 1 : 0) : (cap > 0 ? cap : 0);
  size_t n = r->len < (size_t) room ? r->len : (size_t) room;
  if (n) memcpy (buf, r->data, n);
  if (nul_terminate && cap > 0) ((char *) buf)[n] = '\0';
  return (int) n;
}

int hegel_draw_text (hegel_testcase * tc, int min_size, int max_size,
                     char * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_DRAW_TEXT, &r, 2, min_size, max_size);
  int len = copy_out (&r, buf, capacity, 1);
  hegel__buf_free (&r);
  if (capacity > 0) trace (tc, "\"%s\"", buf);
  return len;
}

int hegel_draw_bytes (hegel_testcase * tc, int min_size, int max_size,
                      uint8_t * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_DRAW_BYTES, &r, 2, min_size, max_size);
  int len = copy_out (&r, buf, capacity, 0);
  hegel__buf_free (&r);
  trace (tc, "<%d bytes>", len);
  return len;
}

int hegel_draw_regex (hegel_testcase * tc, const char * pattern,
                      char * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_DRAW_REGEX, pattern, (uint32_t) strlen (pattern), &r, 0);
  int len = copy_out (&r, buf, capacity, 1);
  hegel__buf_free (&r);
  if (capacity > 0) trace (tc, "\"%s\"", buf);
  return len;
}

/* ---- Spans ---- */

uint64_t hegel_label (const char * name)
{
  uint64_t h = 0xcbf29ce484222325ULL;   /* FNV-1a, as hegel_label_from_name */
  for (const unsigned char * p = (const unsigned char *) name; *p; p++) {
    h ^= *p;
    h *= 0x100000001b3ULL;
  }
  return h;
}

void hegel_start_span (hegel_testcase * tc, uint64_t label)
{
  hegel__call (tc, HEGEL_OP_SPAN_START, &label, sizeof label, NULL, 0);
}

void hegel_stop_span (hegel_testcase * tc, int discard)
{
  uint8_t d = discard != 0;
  hegel__call (tc, HEGEL_OP_SPAN_STOP, &d, sizeof d, NULL, 0);
}

/* ---- Collections ---- */

hegel_collection hegel_collection_new (hegel_testcase * tc, uint64_t min_size,
                                       uint64_t max_size)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_COLL_NEW, &r, 2, (int64_t) min_size, (int64_t) max_size);
  hegel_collection c = { resp_i64 (&r) };
  hegel__buf_free (&r);
  return c;
}

bool hegel_collection_more (hegel_testcase * tc, hegel_collection c)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_COLL_MORE, &r, 1, c.id, 0);
  bool more = resp_u8 (&r) != 0;
  hegel__buf_free (&r);
  return more;
}

void hegel_collection_reject (hegel_testcase * tc, hegel_collection c)
{
  call_ids (tc, HEGEL_OP_COLL_REJECT, NULL, 1, c.id, 0);
}

void hegel_collection_free (hegel_testcase * tc, hegel_collection c)
{
  call_ids (tc, HEGEL_OP_COLL_FREE, NULL, 1, c.id, 0);
}

/* ---- Recursion ---- */

hegel_recursion hegel_recursion_new (hegel_testcase * tc, uint64_t max_depth,
                                     uint64_t max_leaves)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_REC_NEW, &r, 2, (int64_t) max_depth, (int64_t) max_leaves);
  hegel_recursion rec = { resp_i64 (&r) };
  hegel__buf_free (&r);
  return rec;
}

bool hegel_recursion_branch (hegel_testcase * tc, hegel_recursion rec, uint64_t depth)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_REC_BRANCH, &r, 2, rec.id, (int64_t) depth);
  bool b = resp_u8 (&r) != 0;
  hegel__buf_free (&r);
  return b;
}

int hegel_recursion_leaf (hegel_testcase * tc, hegel_recursion rec)
{
  return call_ids (tc, HEGEL_OP_REC_LEAF, NULL, 1, rec.id, 0) == HEGEL_RESP_RETRY;
}

void hegel_recursion_retry (hegel_testcase * tc, hegel_recursion rec)
{
  call_ids (tc, HEGEL_OP_REC_RETRY, NULL, 1, rec.id, 0);
}

int hegel_recursion_finish (hegel_testcase * tc, hegel_recursion rec)
{
  return call_ids (tc, HEGEL_OP_REC_FINISH, NULL, 1, rec.id, 0) == HEGEL_RESP_RETRY;
}

void hegel_recursion_free (hegel_testcase * tc, hegel_recursion rec)
{
  call_ids (tc, HEGEL_OP_REC_FREE, NULL, 1, rec.id, 0);
}

/* ---- Pools ---- */

hegel_pool hegel_pool_new (hegel_testcase * tc)
{
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_POOL_NEW, NULL, 0, &r, 0);
  hegel_pool p = { resp_i64 (&r) };
  hegel__buf_free (&r);
  return p;
}

int64_t hegel_pool_add (hegel_testcase * tc, hegel_pool p)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_POOL_ADD, &r, 1, p.id, 0);
  int64_t v = resp_i64 (&r);
  hegel__buf_free (&r);
  return v;
}

int64_t hegel_pool_draw (hegel_testcase * tc, hegel_pool p, bool consume)
{
  struct __attribute__ ((packed)) { int64_t id; uint8_t consume; } req = { p.id, consume };
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_POOL_DRAW, &req, sizeof req, &r, 0);
  int64_t v = resp_i64 (&r);
  hegel__buf_free (&r);
  return v;
}

void hegel_pool_restore (hegel_testcase * tc, hegel_pool p, int64_t var)
{
  call_ids (tc, HEGEL_OP_POOL_RESTORE, NULL, 2, p.id, var);
}

void hegel_pool_free (hegel_testcase * tc, hegel_pool p)
{
  call_ids (tc, HEGEL_OP_POOL_FREE, NULL, 1, p.id, 0);
}

/* ---- Notes, assume, fail ---- */

void hegel_note (hegel_testcase * tc, const char * msg)
{
  hegel__emit_line (tc, msg);
}

void hegel_assume (hegel_testcase * tc, int condition)
{
  if (condition) return;
  hegel__escape (tc, HEGEL_ESC_ASSUME);
}

void hegel_fail_with_origin (const char * origin, const char * msg)
{
  hegel_testcase * tc = hegel__current_tc;
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", msg ? msg : "(no message)");
  /* Inside a HEGEL_ISOLATED block the block is the origin: an assert there
  ** and a crash there are the same bug, so they shrink as one. */
  if (tc && tc->iso_site)
    snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "HEGEL_ISOLATED at %s",
              tc->iso_site);
  else
    snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "%s",
              origin ? origin : "hegel_fail");
  if (!tc) {
    fprintf (stderr, "hegel-c: hegel_fail outside a test case: %s\n", hegel__fail_msg);
    abort ();
  }
  if (tc->capture) {
    char line[1100];
    snprintf (line, sizeof line, "Property test failed: %s", hegel__fail_msg);
    hegel__emit_line (tc, line);
  }
  hegel__escape (tc, HEGEL_ESC_FAIL);
}

void hegel_fail (const char * msg)
{
  hegel_fail_with_origin ("hegel_fail", msg);
}

void hegel_assert (int condition, const char * msg)
{
  if (condition) return;
  hegel_fail (msg);
}

void hegel_health_fail (const char * msg)
{
  hegel_testcase * tc = hegel__current_tc;
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", msg ? msg : "(no message)");
  if (!tc) {
    fprintf (stderr, "Health check failure: %s\n", hegel__fail_msg);
    exit (1);
  }
  hegel__escape (tc, HEGEL_ESC_HEALTH);
}

/* ---- Targeting ---- */

void hegel_target (hegel_testcase * tc, double value, const char * label)
{
  hegel_buf p;
  hegel__buf_init (&p);
  HEGEL_BUF_PUT (&p, value);
  hegel__buf_put (&p, label, strlen (label));
  hegel__call (tc, HEGEL_OP_TARGET, p.data, (uint32_t) p.len, NULL, 0);
  hegel__buf_free (&p);
}

/* ---- HEGEL_ISOLATED ----
**
** The block's body runs where a crash can't take the run down with it:
**
**   nofork (INPROC) — fork here.  The child runs the body (mode ISO:
**     notes and a verdict only) and the parent waits, relaying the
**     child's notes into the capture and re-raising its verdict; a
**     crash becomes a failure whose origin names the block.  The child
**     inherits the whole address space copy-on-write, so the values
**     drawn before the block are simply there — nothing is serialized.
**   fork (CHILD) — the body already runs in a disposable child; run it
**     inline, after telling the parent which block we're in so a crash
**     is still attributed to it.
**
** In both modes an engine call inside the block is an authoring error
** (health failure): the parent can't see draws made in a process that
** may not survive. */

static void report_iso_crash (hegel_testcase * tc, const char * site, int wstatus)
{
  if (WIFSIGNALED (wstatus)) {
    int sig = WTERMSIG (wstatus);
    snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "HEGEL_ISOLATED at %s", site);
    snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
              "HEGEL_ISOLATED block at %s crashed with signal %d (%s)",
              site, sig, strsignal (sig));
  } else {
    int code = WIFEXITED (wstatus) ? WEXITSTATUS (wstatus) : -1;
    snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "HEGEL_ISOLATED at %s", site);
    snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
              "HEGEL_ISOLATED block at %s exited without a verdict (exit code %d)",
              site, code);
  }
  if (tc->capture) {
    char line[1100];
    snprintf (line, sizeof line, "Property test failed: %s", hegel__fail_msg);
    hegel__emit_line (tc, line);
  }
  hegel__escape (tc, HEGEL_ESC_FAIL);
}

int hegel__isolate_enter (hegel_testcase * tc, const char * site)
{
  if (tc->iso_site || tc->mode == HEGEL_TC_ISO) {
    char msg[512];
    snprintf (msg, sizeof msg, "hegel-c: nested HEGEL_ISOLATED at %s (outer block: %s)",
              site, tc->iso_site ? tc->iso_site : "?");
    escape_health (tc, msg);
  }

  if (tc->mode == HEGEL_TC_CHILD) {
    hegel__send (tc, HEGEL_OP_ISO_SITE, site, (uint32_t) strlen (site));
    tc->iso_site = site;
    return 1;
  }

  int fds[2];
  if (pipe (fds) != 0) {
    fprintf (stderr, "hegel-c: pipe() failed\n");
    exit (1);
  }
  fflush (stdout);
  fflush (stderr);
  pid_t pid = fork ();
  if (pid < 0) {
    fprintf (stderr, "hegel-c: fork() failed\n");
    exit (1);
  }
  if (pid == 0) {
    close (fds[0]);
    tc->mode = HEGEL_TC_ISO;
    tc->req_fd = fds[1];
    tc->iso_site = site;
    tc->escape = NULL;
    tc->rule_escape = NULL;
    tc->cleanup = NULL;
    return 1;
  }

  /* Parent: relay notes, collect the verdict. */
  close (fds[1]);
  hegel_outcome out;
  memset (&out, 0, sizeof out);
  out.status = -1;
  for (;;) {
    hegel_msg_header h;
    if (hegel__read_full (fds[0], &h, sizeof h) != 0) break;
    uint8_t * payload = malloc (h.len ? h.len : 1);
    if (!payload) abort ();
    if (h.len && hegel__read_full (fds[0], payload, h.len) != 0) { free (payload); break; }
    int done = 0;
    if (h.op == HEGEL_OP_NOTE || h.op >= HEGEL_OP_DONE) {
      uint8_t st;
      done = hegel__serve_op (tc->srv, h.op, payload, h.len, &st, NULL, &out) == 2;
    }
    free (payload);
    if (done) break;
  }
  close (fds[0]);
  int wstatus = 0;
  while (waitpid (pid, &wstatus, 0) < 0) { /* EINTR */ }

  switch (out.status) {
    case -1:
      report_iso_crash (tc, site, wstatus);
      break;
    case HG_STATUS_VALID:
      return 0;
    case HG_STATUS_INTERESTING:
      /* The child already captured its "Property test failed" line. */
      snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "%s", out.origin);
      snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", out.msg);
      hegel__escape (tc, HEGEL_ESC_FAIL);
      break;
    default:
      if (out.health) escape_health (tc, out.msg);
      hegel__escape (tc, HEGEL_ESC_ASSUME);
  }
  return 0;
}

int hegel__isolate_leave (hegel_testcase * tc)
{
  if (tc->mode == HEGEL_TC_ISO) {
    fflush (NULL);
    hegel__send_terminal (tc->req_fd, HEGEL_OP_DONE);
    _exit (0);
  }
  tc->iso_site = NULL;
  hegel__send (tc, HEGEL_OP_ISO_SITE, "", 0);
  return 0;
}
