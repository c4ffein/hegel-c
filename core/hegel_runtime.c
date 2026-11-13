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
void (*hegel__draws_abandoned)(void) = NULL;

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

/* ---- Value draws (dates, times, uuids, IPs, emails, URLs, domains) ----
**
** Each is one wire op (HEGEL_OP_DRAW_DATE..DRAW_DOMAIN); dates and times
** travel field by field (see hegel_internal.h), never as raw structs. */

static const hegel_date default_min_date = { 1, 1, 1 };
static const hegel_date default_max_date = { 9999, 12, 31 };
static const hegel_time default_min_time = { 0, 0, 0, 0 };
static const hegel_time default_max_time = { 23, 59, 59, 999999999 };

static void put_date (hegel_buf * b, const hegel_date * d)
{
  HEGEL_BUF_PUT (b, d->year);
  HEGEL_BUF_PUT (b, d->month);
  HEGEL_BUF_PUT (b, d->day);
}

static void put_time (hegel_buf * b, const hegel_time * t)
{
  HEGEL_BUF_PUT (b, t->hour);
  HEGEL_BUF_PUT (b, t->minute);
  HEGEL_BUF_PUT (b, t->second);
  HEGEL_BUF_PUT (b, t->nanosecond);
}

/* Read the next `n` response bytes at *off into `out` (zeros when short). */
static void resp_take (const hegel_buf * r, size_t * off, void * out, size_t n)
{
  if (*off + n <= r->len) memcpy (out, r->data + *off, n);
  else memset (out, 0, n);
  *off += n;
}

static hegel_date take_date (const hegel_buf * r, size_t * off)
{
  hegel_date d;
  memset (&d, 0, sizeof d);
  resp_take (r, off, &d.year, sizeof d.year);
  resp_take (r, off, &d.month, sizeof d.month);
  resp_take (r, off, &d.day, sizeof d.day);
  return d;
}

static hegel_time take_time (const hegel_buf * r, size_t * off)
{
  hegel_time t;
  memset (&t, 0, sizeof t);
  resp_take (r, off, &t.hour, sizeof t.hour);
  resp_take (r, off, &t.minute, sizeof t.minute);
  resp_take (r, off, &t.second, sizeof t.second);
  resp_take (r, off, &t.nanosecond, sizeof t.nanosecond);
  return t;
}

/* "YYYY-MM-DD" / "HH:MM:SS[.nnnnnnnnn]", as the Rust frontend formats them. */
static void fmt_date (char * s, size_t n, hegel_date d)
{
  snprintf (s, n, "%04" PRId32 "-%02u-%02u", d.year, d.month, d.day);
}

static void fmt_time (char * s, size_t n, hegel_time t)
{
  if (t.nanosecond) snprintf (s, n, "%02u:%02u:%02u.%09" PRIu32, t.hour, t.minute, t.second,
                              t.nanosecond);
  else snprintf (s, n, "%02u:%02u:%02u", t.hour, t.minute, t.second);
}

hegel_date hegel_draw_date (hegel_testcase * tc, const hegel_date * min,
                            const hegel_date * max)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf p, r;
  hegel__buf_init (&p);
  hegel__buf_init (&r);
  put_date (&p, min ? min : &default_min_date);
  put_date (&p, max ? max : &default_max_date);
  hegel__call (tc, HEGEL_OP_DRAW_DATE, p.data, (uint32_t) p.len, &r, 0);
  size_t off = 0;
  hegel_date v = take_date (&r, &off);
  hegel__buf_free (&p);
  hegel__buf_free (&r);
  char s[32];
  fmt_date (s, sizeof s, v);
  trace (tc, "%s", s);
  return v;
}

hegel_time hegel_draw_time (hegel_testcase * tc, const hegel_time * min,
                            const hegel_time * max)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf p, r;
  hegel__buf_init (&p);
  hegel__buf_init (&r);
  put_time (&p, min ? min : &default_min_time);
  put_time (&p, max ? max : &default_max_time);
  hegel__call (tc, HEGEL_OP_DRAW_TIME, p.data, (uint32_t) p.len, &r, 0);
  size_t off = 0;
  hegel_time v = take_time (&r, &off);
  hegel__buf_free (&p);
  hegel__buf_free (&r);
  char s[32];
  fmt_time (s, sizeof s, v);
  trace (tc, "%s", s);
  return v;
}

hegel_datetime hegel_draw_datetime (hegel_testcase * tc, const hegel_datetime * min,
                                    const hegel_datetime * max)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf p, r;
  hegel__buf_init (&p);
  hegel__buf_init (&r);
  put_date (&p, min ? &min->date : &default_min_date);
  put_time (&p, min ? &min->time : &default_min_time);
  put_date (&p, max ? &max->date : &default_max_date);
  put_time (&p, max ? &max->time : &default_max_time);
  hegel__call (tc, HEGEL_OP_DRAW_DATETIME, p.data, (uint32_t) p.len, &r, 0);
  size_t off = 0;
  hegel_datetime v;
  v.date = take_date (&r, &off);
  v.time = take_time (&r, &off);
  hegel__buf_free (&p);
  hegel__buf_free (&r);
  char ds[32], ts[32];
  fmt_date (ds, sizeof ds, v.date);
  fmt_time (ts, sizeof ts, v.time);
  trace (tc, "%sT%s", ds, ts);
  return v;
}

/* Fixed-size byte draws: uuid (u32 version payload), ipv4, ipv6. */
static void draw_fixed (hegel_testcase * tc, uint8_t op, const void * payload, uint32_t len,
                        uint8_t * out, size_t n)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, op, payload, len, &r, 0);
  size_t off = 0;
  resp_take (&r, &off, out, n);
  hegel__buf_free (&r);
}

hegel_uuid hegel_draw_uuid (hegel_testcase * tc, int version)
{
  hegel_uuid u;
  /* Out-of-range versions go to the server as-is, which rejects them. */
  uint32_t v = version < 0 ? UINT32_MAX : (uint32_t) version;
  draw_fixed (tc, HEGEL_OP_DRAW_UUID, &v, sizeof v, u.bytes, sizeof u.bytes);
  const uint8_t * b = u.bytes;
  trace (tc, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
         b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
         b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return u;
}

hegel_ipv4 hegel_draw_ipv4 (hegel_testcase * tc)
{
  hegel_ipv4 a;
  draw_fixed (tc, HEGEL_OP_DRAW_IPV4, NULL, 0, a.bytes, sizeof a.bytes);
  trace (tc, "%u.%u.%u.%u", a.bytes[0], a.bytes[1], a.bytes[2], a.bytes[3]);
  return a;
}

hegel_ipv6 hegel_draw_ipv6 (hegel_testcase * tc)
{
  hegel_ipv6 a;
  draw_fixed (tc, HEGEL_OP_DRAW_IPV6, NULL, 0, a.bytes, sizeof a.bytes);
  const uint8_t * b = a.bytes;
  trace (tc, "%x:%x:%x:%x:%x:%x:%x:%x",
         (b[0] << 8) | b[1], (b[2] << 8) | b[3], (b[4] << 8) | b[5], (b[6] << 8) | b[7],
         (b[8] << 8) | b[9], (b[10] << 8) | b[11], (b[12] << 8) | b[13], (b[14] << 8) | b[15]);
  return a;
}

static int draw_value_string (hegel_testcase * tc, uint8_t op, const void * payload,
                              uint32_t len, char * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, op, payload, len, &r, 0);
  int n = copy_out (&r, buf, capacity, 1);
  hegel__buf_free (&r);
  if (capacity > 0) trace (tc, "\"%s\"", buf);
  return n;
}

int hegel_draw_email (hegel_testcase * tc, char * buf, int capacity)
{
  return draw_value_string (tc, HEGEL_OP_DRAW_EMAIL, NULL, 0, buf, capacity);
}

int hegel_draw_url (hegel_testcase * tc, char * buf, int capacity)
{
  return draw_value_string (tc, HEGEL_OP_DRAW_URL, NULL, 0, buf, capacity);
}

char * hegel_draw_url_alloc (hegel_testcase * tc)
{
  if (!tc->silent) tc->draw_count++;
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_DRAW_URL, NULL, 0, &r, 0);
  char * s = hegel_alloc (tc, r.len + 1);   /* zeroed: the NUL is there */
  if (r.len) memcpy (s, r.data, r.len);
  hegel__buf_free (&r);
  trace (tc, "\"%s\"", s);
  return s;
}

int hegel_draw_domain (hegel_testcase * tc, int max_length, char * buf, int capacity)
{
  /* 0 = the engine default, 255; out-of-range lengths go to the server
  ** as-is, which rejects them. */
  uint64_t m = max_length == 0 ? 255 : max_length < 0 ? 0 : (uint64_t) max_length;
  return draw_value_string (tc, HEGEL_OP_DRAW_DOMAIN, &m, sizeof m, buf, capacity);
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

uint64_t hegel_label_combine (const uint64_t * labels, size_t n)
{
  uint64_t h = 0xcbf29ce484222325ULL;   /* FNV-1a over LE bytes, as hegel_label_combine */
  for (size_t i = 0; i < n; i++)
    for (int k = 0; k < 8; k++) {
      h ^= (labels[i] >> (8 * k)) & 0xff;
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

/* ---- Repeat loops ----
**
** The server remembers which loops are mid-iteration (their last
** hegel_repeat_more said true) and calls hegel_repeat_failed on them if
** the case ends as a failure, so neither the assert path nor a crashed
** fork child has to. */

hegel_repeat hegel_repeat_new (hegel_testcase * tc)
{
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_REPEAT_NEW, NULL, 0, &r, 0);
  hegel_repeat rp = { resp_i64 (&r) };
  hegel__buf_free (&r);
  return rp;
}

bool hegel_repeat_more (hegel_testcase * tc, hegel_repeat rp)
{
  hegel_buf r;
  hegel__buf_init (&r);
  call_ids (tc, HEGEL_OP_REPEAT_MORE, &r, 1, rp.id, 0);
  bool more = resp_u8 (&r) != 0;
  hegel__buf_free (&r);
  return more;
}

void hegel_repeat_failed (hegel_testcase * tc, hegel_repeat rp)
{
  call_ids (tc, HEGEL_OP_REPEAT_FAILED, NULL, 1, rp.id, 0);
}

void hegel_repeat_free (hegel_testcase * tc, hegel_repeat rp)
{
  call_ids (tc, HEGEL_OP_REPEAT_FREE, NULL, 1, rp.id, 0);
}

static void repeat_heading (hegel_testcase * tc, uint64_t n)
{
  if (!tc->capture) return;
  char line[64];
  snprintf (line, sizeof line, "Repetition #%" PRIu64, n);
  hegel__emit_line (tc, line);
}

hegel__repeat_loop hegel__repeat_begin (hegel_testcase * tc)
{
  hegel__repeat_loop l = { hegel_repeat_new (tc), 0 };
  return l;
}

bool hegel__repeat_step (hegel_testcase * tc, hegel__repeat_loop * l)
{
  if (!hegel_repeat_more (tc, l->r)) {
    hegel_repeat_free (tc, l->r);
    return false;
  }
  repeat_heading (tc, ++l->n);
  return true;
}

/* One iteration under an ASSUME catch, as a stateful rule runs (see
** run_rule in hegel_stateful.c): a rejected iteration is over, the loop
** goes on.  FAIL / STOP / HEALTH still end the case. */
static void repeat_iteration (hegel_testcase * tc, hegel_repeat_fn body, void * ctx)
{
  jmp_buf env;
  jmp_buf * saved = tc->rule_escape;
  struct hegel__recursive * saved_rec = tc->recursive;
  tc->rule_escape = &env;
  if (setjmp (env) != 0) {
    tc->rule_escape = saved;
    tc->recursive = saved_rec;
    if (hegel__draws_abandoned) hegel__draws_abandoned ();
    return;
  }
  body (tc, ctx);
  tc->rule_escape = saved;
}

void hegel_repeat_run (hegel_testcase * tc, hegel_repeat_fn body, void * ctx)
{
  hegel_repeat rp = hegel_repeat_new (tc);
  uint64_t n = 0;
  while (hegel_repeat_more (tc, rp)) {
    repeat_heading (tc, ++n);
    repeat_iteration (tc, body, ctx);
  }
  hegel_repeat_free (tc, rp);
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

/* ---- Grammars ----
**
** The grammar lives in the server (compiled once per run, cached by
** the spec's bytes); the body sends the spec with each draw.  With
** explicit terminals the draw is a loop of requests: NEXT answers with
** the terminal whose value is due (the body draws it through `fill`,
** with ordinary requests, and PROVIDEs it) or with the finished string.
** An escape mid-loop leaves the server's draw handle to the case-end
** sweep. */

static void put_lstr (hegel_buf * b, const char * s)
{
  uint32_t l = s ? (uint32_t) strlen (s) : 0;
  HEGEL_BUF_PUT (b, l);
  hegel__buf_put (b, s, l);
}

/* The drawn string as a case-owned C string; health failure on a NUL. */
static char * grammar_string (hegel_testcase * tc, const uint8_t * p, size_t n)
{
  if (memchr (p, '\0', n))
    escape_health (tc, "hegel-c: grammar draw produced a string containing NUL");
  char * s = hegel_alloc (tc, n + 1);
  if (n) memcpy (s, p, n);
  return s;
}

char * hegel_draw_grammar_with (hegel_testcase * tc, const char * grammar,
                                const hegel_grammar_options * opts)
{
  hegel_grammar_options none;
  if (!opts) {
    memset (&none, 0, sizeof none);
    opts = &none;
  }
  if (!grammar) escape_health (tc, "hegel-c: hegel_draw_grammar with a NULL grammar");
  if (opts->n_explicit && !opts->fill)
    escape_health (tc, "hegel-c: hegel_draw_grammar_with: explicit terminals need a fill "
                       "callback");
  if (opts->n_explicit && !opts->explicit_names)
    escape_health (tc, "hegel-c: hegel_draw_grammar_with: n_explicit without explicit_names");

  hegel_buf spec;
  hegel__buf_init (&spec);
  uint32_t nexp = (uint32_t) opts->n_explicit;
  HEGEL_BUF_PUT (&spec, opts->max_size);
  HEGEL_BUF_PUT (&spec, opts->max_depth);
  HEGEL_BUF_PUT (&spec, nexp);
  put_lstr (&spec, grammar);
  put_lstr (&spec, opts->start);
  put_lstr (&spec, opts->import_dir);
  for (uint32_t i = 0; i < nexp; i++) put_lstr (&spec, opts->explicit_names[i]);

  hegel_buf r;
  hegel__buf_init (&r);
  char * s;
  if (!nexp) {
    hegel__call (tc, HEGEL_OP_GRAMMAR_DRAW, spec.data, (uint32_t) spec.len, &r, 0);
    hegel__buf_free (&spec);
    s = grammar_string (tc, r.data, r.len);
  } else {
    /* One span over the whole draw (labelled from the grammar's shape, as
    ** hegel-rust's from_lark), one per explicit value. */
    uint64_t parts[3] = { hegel_label ("hegel_c.from_lark"), hegel_label (grammar),
                          hegel_label (opts->start ? opts->start : "start") };
    hegel_start_span (tc, hegel_label_combine (parts, 3));
    hegel__call (tc, HEGEL_OP_GRAMMAR_NEW, spec.data, (uint32_t) spec.len, &r, 0);
    hegel__buf_free (&spec);
    int64_t id = resp_i64 (&r);
    for (;;) {
      hegel__call (tc, HEGEL_OP_GRAMMAR_NEXT, &id, sizeof id, &r, 0);
      if (resp_u8 (&r)) {
        s = grammar_string (tc, r.data + 1, r.len - 1);
        break;
      }
      uint64_t term = 0;
      if (r.len >= 1 + sizeof term) memcpy (&term, r.data + 1, sizeof term);
      if (term >= nexp) escape_health (tc, "hegel-c: engine asked for an unknown terminal");
      uint64_t tparts[2] = { hegel_label ("hegel_c.from_lark.explicit"),
                             hegel_label (opts->explicit_names[term]) };
      hegel_start_span (tc, hegel_label_combine (tparts, 2));
      const char * v = opts->fill (tc, (size_t) term, opts->fill_ctx);
      hegel_stop_span (tc, 0);
      if (!v) {
        char msg[300];
        snprintf (msg, sizeof msg, "hegel-c: grammar fill callback returned NULL for "
                  "terminal %.200s", opts->explicit_names[term]);
        escape_health (tc, msg);
      }
      hegel_buf p;
      hegel__buf_init (&p);
      HEGEL_BUF_PUT (&p, id);
      hegel__buf_put (&p, v, strlen (v));
      hegel__call (tc, HEGEL_OP_GRAMMAR_PROVIDE, p.data, (uint32_t) p.len, NULL, 0);
      hegel__buf_free (&p);
    }
    hegel_stop_span (tc, 0);
  }
  hegel__buf_free (&r);
  if (!tc->silent) tc->draw_count++;
  trace (tc, "\"%s\"", s);
  return s;
}

char * hegel_draw_grammar (hegel_testcase * tc, const char * grammar, const char * start)
{
  hegel_grammar_options o;
  memset (&o, 0, sizeof o);
  o.start = start;
  return hegel_draw_grammar_with (tc, grammar, &o);
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

/* ---- Events (statistics observations) ---- */

static void event_label_check (hegel_testcase * tc, const char * fn, const char * label)
{
  if (label) return;
  char msg[96];
  snprintf (msg, sizeof msg, "hegel-c: %s with a NULL label", fn);
  escape_health (tc, msg);
}

void hegel_event (hegel_testcase * tc, const char * label)
{
  event_label_check (tc, "hegel_event", label);
  hegel__call (tc, HEGEL_OP_EVENT, label, (uint32_t) strlen (label), NULL, 0);
}

void hegel_event_value (hegel_testcase * tc, const char * label, double value)
{
  event_label_check (tc, "hegel_event_value", label);
  hegel_buf p;
  hegel__buf_init (&p);
  HEGEL_BUF_PUT (&p, value);
  hegel__buf_put (&p, label, strlen (label));
  hegel__call (tc, HEGEL_OP_EVENT_VALUE, p.data, (uint32_t) p.len, NULL, 0);
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
    /* Only notes and the terminal verdicts (DONE .. HEALTH) come from an
    ** isolated child; ops numbered after HEALTH are ordinary requests. */
    if (h.op == HEGEL_OP_NOTE || (h.op >= HEGEL_OP_DONE && h.op <= HEGEL_OP_HEALTH)) {
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
