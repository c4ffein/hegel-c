/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Test-case runtime: primitive draws, spans, assertions.
**
** Each draw has two paths selected by tc->mode:
**   INPROC — encode a CBOR schema, call libhegel's hegel_generate,
**            decode the CBOR value.
**   CHILD  — send a framed request over the pipe; the fork parent
**            (hegel_runner.c) performs the INPROC path on our behalf
**            and sends the value back.
*/

#include "hegel_internal.h"
#include "hegel_cbor.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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

/* ---- Terminal outcomes ---- */

void hegel__child_send_terminal (hegel_testcase * tc, int op)
{
  hegel_msg_header h = { (uint8_t)op, {0,0,0}, 0 };
  if (op == HEGEL_OP_FAIL) {
    uint32_t olen = (uint32_t)strlen (hegel__fail_origin);
    uint32_t mlen = (uint32_t)strlen (hegel__fail_msg);
    h.len = 4 + olen + 4 + mlen;
    hegel__write_full (tc->req_fd, &h, sizeof h);
    hegel__write_full (tc->req_fd, &olen, 4);
    hegel__write_full (tc->req_fd, hegel__fail_origin, olen);
    hegel__write_full (tc->req_fd, &mlen, 4);
    hegel__write_full (tc->req_fd, hegel__fail_msg, mlen);
  } else if (op == HEGEL_OP_HEALTH) {
    uint32_t mlen = (uint32_t)strlen (hegel__fail_msg);
    h.len = mlen;
    hegel__write_full (tc->req_fd, &h, sizeof h);
    hegel__write_full (tc->req_fd, hegel__fail_msg, mlen);
  } else {
    hegel__write_full (tc->req_fd, &h, sizeof h);
  }
}

/* Escape from the test body with `code`.  In a fork child every code
** except ASSUME/STOP-inside-a-rule means "send terminal message and
** exit"; in nofork it's a longjmp to whichever handler applies. */
void hegel__escape (hegel_testcase * tc, int code)
{
  /* Rule-level catches: ASSUME / STOP inside a stateful rule return
  ** control to the stateful loop in both modes. */
  if (tc->rule_escape && (code == HEGEL_ESC_ASSUME || code == HEGEL_ESC_STOP)) {
    longjmp (*tc->rule_escape, code);
  }

  if (tc->mode == HEGEL_TC_CHILD) {
    int op;
    switch (code) {
      case HEGEL_ESC_FAIL:   op = HEGEL_OP_FAIL;   break;
      case HEGEL_ESC_ASSUME: op = HEGEL_OP_ASSUME; break;
      case HEGEL_ESC_STOP:   op = HEGEL_OP_STOPPED; break;
      case HEGEL_ESC_HEALTH: op = HEGEL_OP_HEALTH; break;
      default:               op = HEGEL_OP_DONE;   break;
    }
    fflush (NULL);  /* _exit discards stdio buffers — draw traces live there */
    hegel__child_send_terminal (tc, op);
    _exit (0);
  }

  if (tc->escape) longjmp (*tc->escape, code);

  /* No driver registered — direct API misuse. */
  fprintf (stderr, "hegel-c: escape (%d) with no active run loop\n", code);
  abort ();
}

/* ---- Engine-call layer: schema encode + generate + value decode ---- */

static void engine_hard_error (const hg_engine * e, const char * what)
{
  fprintf (stderr, "hegel-c: %s failed: %s\n", what, e->last_error_message ());
  abort ();
}

/* Run hegel_generate; returns 0 ok / 1 stop; aborts on hard errors. */
static int do_generate (const hg_engine * e, hg_tc * t,
                        const uint8_t * schema, size_t schema_len,
                        const uint8_t ** out_val, size_t * out_len,
                        const char * what)
{
  int rc = e->generate (t, schema, schema_len, out_val, out_len);
  if (rc == HG_OK) return 0;
  if (rc == HG_E_STOP_TEST) return 1;
  if (rc == HG_E_ASSUME) return 2;
  engine_hard_error (e, what);
  return 1;  /* unreachable */
}

int hegel__eng_draw_int (const hg_engine * e, hg_tc * t,
                         int64_t min, int64_t max, int64_t * out)
{
  uint8_t schema[64];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  hegel_cbor_write_map_header (&w, 3);
  hegel_cbor_write_text_z (&w, "type");      hegel_cbor_write_text_z (&w, "integer");
  hegel_cbor_write_text_z (&w, "min_value"); hegel_cbor_write_int (&w, min);
  hegel_cbor_write_text_z (&w, "max_value"); hegel_cbor_write_int (&w, max);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "integer draw");
  if (g) return g;

  hegel_cbor_reader r;
  hegel_cbor_reader_init (&r, val, vlen);
  if (hegel_cbor_read_int (&r, out) != HEGEL_CBOR_OK) {
    fprintf (stderr, "hegel-c: integer draw returned non-integer CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_u64 (const hg_engine * e, hg_tc * t,
                         uint64_t min, uint64_t max, uint64_t * out)
{
  uint8_t schema[64];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  hegel_cbor_write_map_header (&w, 3);
  hegel_cbor_write_text_z (&w, "type");      hegel_cbor_write_text_z (&w, "integer");
  hegel_cbor_write_text_z (&w, "min_value"); hegel_cbor_write_uint (&w, min);
  hegel_cbor_write_text_z (&w, "max_value"); hegel_cbor_write_uint (&w, max);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "u64 draw");
  if (g) return g;

  hegel_cbor_reader r;
  hegel_cbor_reader_init (&r, val, vlen);
  if (hegel_cbor_read_uint (&r, out) != HEGEL_CBOR_OK) {
    fprintf (stderr, "hegel-c: u64 draw returned non-uint CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_double (const hg_engine * e, hg_tc * t,
                            double min, double max, int width, double * out)
{
  /* The engine defaults allow_nan / allow_infinity to TRUE — the
  ** Hypothesis convention of "bounds imply no NaN" is the schema
  ** author's job here.  A bounded draw must never return NaN. */
  int allow_inf = (min == -1.0 / 0.0) || (max == 1.0 / 0.0)
               || (min == 1.0 / 0.0)  || (max == -1.0 / 0.0);
  uint8_t schema[160];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  hegel_cbor_write_map_header (&w, 6);
  hegel_cbor_write_text_z (&w, "type");      hegel_cbor_write_text_z (&w, "float");
  hegel_cbor_write_text_z (&w, "min_value"); hegel_cbor_write_double (&w, min);
  hegel_cbor_write_text_z (&w, "max_value"); hegel_cbor_write_double (&w, max);
  hegel_cbor_write_text_z (&w, "width");     hegel_cbor_write_uint (&w, (uint64_t)width);
  hegel_cbor_write_text_z (&w, "allow_nan");      hegel_cbor_write_bool (&w, false);
  hegel_cbor_write_text_z (&w, "allow_infinity"); hegel_cbor_write_bool (&w, allow_inf);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "float draw");
  if (g) return g;

  hegel_cbor_reader r;
  hegel_cbor_reader_init (&r, val, vlen);
  if (hegel_cbor_read_double (&r, out) != HEGEL_CBOR_OK) {
    /* Bounded float ranges that contain integers may come back as
    ** CBOR integers (the engine biases toward simple values). */
    int64_t iv;
    if (hegel_cbor_read_int (&r, &iv) == HEGEL_CBOR_OK) {
      *out = (double)iv;
      return 0;
    }
    fprintf (stderr, "hegel-c: float draw returned non-float CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_bool (const hg_engine * e, hg_tc * t, int * out)
{
  uint8_t schema[32];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  hegel_cbor_write_map_header (&w, 1);
  hegel_cbor_write_text_z (&w, "type"); hegel_cbor_write_text_z (&w, "boolean");
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "bool draw");
  if (g) return g;

  hegel_cbor_reader r;
  hegel_cbor_reader_init (&r, val, vlen);
  bool b;
  if (hegel_cbor_read_bool (&r, &b) != HEGEL_CBOR_OK) {
    fprintf (stderr, "hegel-c: bool draw returned non-bool CBOR\n");
    abort ();
  }
  *out = b ? 1 : 0;
  return 0;
}

/* Decode a string value: the engine wraps strings in CBOR tag 91
** (its WTF-8 marker) around a byte string; tolerate a plain text
** string as well. */
static int decode_string_value (const uint8_t * val, size_t vlen,
                                const uint8_t ** out_ptr, size_t * out_len)
{
  hegel_cbor_reader r;
  hegel_cbor_reader_init (&r, val, vlen);
  hegel_cbor_type ty;
  if (hegel_cbor_peek_type (&r, &ty) != HEGEL_CBOR_OK) return -1;
  if (ty == HEGEL_CBOR_TYPE_TAG) {
    uint64_t tag;
    if (hegel_cbor_read_tag (&r, &tag) != HEGEL_CBOR_OK) return -1;
    return hegel_cbor_read_bytes (&r, out_ptr, out_len) == HEGEL_CBOR_OK ? 0 : -1;
  }
  if (ty == HEGEL_CBOR_TYPE_TEXT) {
    const char * p; size_t n;
    if (hegel_cbor_read_text (&r, &p, &n) != HEGEL_CBOR_OK) return -1;
    *out_ptr = (const uint8_t *)p;
    *out_len = n;
    return 0;
  }
  if (ty == HEGEL_CBOR_TYPE_BYTES)
    return hegel_cbor_read_bytes (&r, out_ptr, out_len) == HEGEL_CBOR_OK ? 0 : -1;
  return -1;
}

static int copy_capped_string (const uint8_t * src, size_t srclen,
                               char * buf, int cap)
{
  if (cap < 1) return 0;
  size_t n = srclen;
  if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
  memcpy (buf, src, n);
  buf[n] = '\0';
  return (int)n;
}

int hegel__eng_draw_text_raw (const hg_engine * e, hg_tc * t,
                              int64_t min, int64_t max,
                              const uint8_t ** out, size_t * out_len)
{
  uint8_t schema[96];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  /* min_codepoint=1 keeps NUL out of the alphabet — drawn strings are
  ** C strings; an embedded '\0' would silently truncate them. */
  hegel_cbor_write_map_header (&w, 4);
  hegel_cbor_write_text_z (&w, "type");          hegel_cbor_write_text_z (&w, "string");
  hegel_cbor_write_text_z (&w, "min_size");      hegel_cbor_write_int (&w, min);
  hegel_cbor_write_text_z (&w, "max_size");      hegel_cbor_write_int (&w, max);
  hegel_cbor_write_text_z (&w, "min_codepoint"); hegel_cbor_write_uint (&w, 1);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "text draw");
  if (g) return g;

  if (decode_string_value (val, vlen, out, out_len) != 0) {
    fprintf (stderr, "hegel-c: text draw returned undecodable CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_text (const hg_engine * e, hg_tc * t,
                          int64_t min, int64_t max,
                          char * buf, int cap, int * out_len)
{
  const uint8_t * s; size_t slen;
  if (hegel__eng_draw_text_raw (e, t, min, max, &s, &slen)) return 1;
  *out_len = copy_capped_string (s, slen, buf, cap);
  return 0;
}

int hegel__eng_draw_bytes_raw (const hg_engine * e, hg_tc * t,
                               int64_t min, int64_t max,
                               const uint8_t ** out, size_t * out_len)
{
  uint8_t schema[96];
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, sizeof schema);
  hegel_cbor_write_map_header (&w, 3);
  hegel_cbor_write_text_z (&w, "type");     hegel_cbor_write_text_z (&w, "binary");
  hegel_cbor_write_text_z (&w, "min_size"); hegel_cbor_write_int (&w, min);
  hegel_cbor_write_text_z (&w, "max_size"); hegel_cbor_write_int (&w, max);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "bytes draw");
  if (g) return g;

  if (decode_string_value (val, vlen, out, out_len) != 0) {
    fprintf (stderr, "hegel-c: bytes draw returned undecodable CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_bytes (const hg_engine * e, hg_tc * t,
                           int64_t min, int64_t max,
                           uint8_t * buf, int cap, int * out_len)
{
  const uint8_t * s; size_t slen;
  if (hegel__eng_draw_bytes_raw (e, t, min, max, &s, &slen)) return 1;
  size_t out = slen;
  if (cap >= 0 && out > (size_t)cap) out = (size_t)cap;
  if (out) memcpy (buf, s, out);
  *out_len = (int)out;
  return 0;
}

int hegel__eng_draw_regex_raw (const hg_engine * e, hg_tc * t, const char * pattern,
                               const uint8_t ** out, size_t * out_len)
{
  size_t patlen = strlen (pattern);
  size_t schema_cap = 64 + patlen;
  uint8_t * schema = malloc (schema_cap);
  if (!schema) abort ();
  hegel_cbor_writer w;
  hegel_cbor_writer_init (&w, schema, schema_cap);
  hegel_cbor_write_map_header (&w, 2);
  hegel_cbor_write_text_z (&w, "type");    hegel_cbor_write_text_z (&w, "regex");
  hegel_cbor_write_text_z (&w, "pattern"); hegel_cbor_write_text (&w, pattern, patlen);
  int n = hegel_cbor_writer_finalize (&w);

  const uint8_t * val; size_t vlen;
  int g = do_generate (e, t, schema, (size_t)n, &val, &vlen, "regex draw");
  free (schema);
  if (g) return g;

  if (decode_string_value (val, vlen, out, out_len) != 0) {
    fprintf (stderr, "hegel-c: regex draw returned undecodable CBOR\n");
    abort ();
  }
  return 0;
}

int hegel__eng_draw_regex (const hg_engine * e, hg_tc * t, const char * pattern,
                           char * buf, int cap, int * out_len)
{
  const uint8_t * s; size_t slen;
  if (hegel__eng_draw_regex_raw (e, t, pattern, &s, &slen)) return 1;
  *out_len = copy_capped_string (s, slen, buf, cap);
  return 0;
}

/* ---- Child-side request/response ---- */

static void child_send_req (hegel_testcase * tc, uint8_t op,
                            const void * payload, uint32_t len)
{
  hegel_msg_header h = { op, {0,0,0}, len };
  if (hegel__write_full (tc->req_fd, &h, sizeof h) != 0 ||
      (len && hegel__write_full (tc->req_fd, payload, len) != 0)) {
    /* Parent vanished — nothing useful left to do. */
    _exit (1);
  }
}

/* Read a response header; returns 0 ok, escapes on STOP. */
static void child_read_status (hegel_testcase * tc)
{
  uint8_t status[8];
  if (hegel__read_full (tc->resp_fd, status, sizeof status) != 0) _exit (1);
  if (status[0] == HEGEL_RESP_STOP) {
    tc->stop_seen = 1;
    hegel__escape (tc, HEGEL_ESC_STOP);
  }
  if (status[0] == HEGEL_RESP_ASSUME)
    hegel__escape (tc, HEGEL_ESC_ASSUME);
}

static uint64_t child_req_scalar (hegel_testcase * tc, uint8_t op,
                                  const void * payload, uint32_t len)
{
  child_send_req (tc, op, payload, len);
  child_read_status (tc);
  uint64_t v;
  if (hegel__read_full (tc->resp_fd, &v, 8) != 0) _exit (1);
  return v;
}

/* Returns the (possibly capped) string/bytes length. */
static int child_req_buffer (hegel_testcase * tc, uint8_t op,
                             const void * payload, uint32_t plen,
                             void * buf, int cap, int nul_terminate)
{
  child_send_req (tc, op, payload, plen);
  child_read_status (tc);
  uint32_t len;
  if (hegel__read_full (tc->resp_fd, &len, 4) != 0) _exit (1);

  /* Read the full payload, keeping only what fits. */
  int keep_cap = nul_terminate ? (cap > 0 ? cap - 1 : 0) : (cap > 0 ? cap : 0);
  uint32_t keep = len < (uint32_t)keep_cap ? len : (uint32_t)keep_cap;
  if (keep && hegel__read_full (tc->resp_fd, buf, keep) != 0) _exit (1);
  uint32_t rest = len - keep;
  uint8_t sink[256];
  while (rest > 0) {
    uint32_t chunk = rest < sizeof sink ? rest : (uint32_t)sizeof sink;
    if (hegel__read_full (tc->resp_fd, sink, chunk) != 0) _exit (1);
    rest -= chunk;
  }
  if (nul_terminate && cap > 0) ((char *)buf)[keep] = '\0';
  return (int)keep;
}

/* ---- Draw trace ---- */

static void trace_i64 (hegel_testcase * tc, int64_t v)
{
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: %" PRId64 "\n", tc->draw_count, v);
}

static void trace_u64 (hegel_testcase * tc, uint64_t v)
{
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: %" PRIu64 "\n", tc->draw_count, v);
}

static void trace_double (hegel_testcase * tc, double v)
{
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: %g\n", tc->draw_count, v);
}

static void trace_str (hegel_testcase * tc, const char * s)
{
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: \"%s\"\n", tc->draw_count, s);
}

/* ---- Public primitive draws ---- */

static int64_t draw_integer (hegel_testcase * tc, int64_t min, int64_t max)
{
  if (!tc->silent) tc->draw_count++;
  int64_t v;
  if (tc->mode == HEGEL_TC_CHILD) {
    int64_t payload[2] = { min, max };
    v = (int64_t)child_req_scalar (tc, HEGEL_OP_DRAW_INT, payload, sizeof payload);
  } else {
    int _rc = hegel__eng_draw_int (tc->eng, tc->htc, min, max, &v);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  trace_i64 (tc, v);
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
  uint64_t v;
  if (tc->mode == HEGEL_TC_CHILD) {
    uint64_t payload[2] = { min_val, max_val };
    v = child_req_scalar (tc, HEGEL_OP_DRAW_U64, payload, sizeof payload);
  } else {
    int _rc = hegel__eng_draw_u64 (tc->eng, tc->htc, min_val, max_val, &v);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  trace_u64 (tc, v);
  return v;
}

size_t hegel_draw_usize (hegel_testcase * tc, size_t min_val, size_t max_val)
{
  return (size_t)hegel_draw_u64 (tc, min_val, max_val);
}

static double draw_floating (hegel_testcase * tc, double min, double max, int width)
{
  if (!tc->silent) tc->draw_count++;
  double v;
  if (tc->mode == HEGEL_TC_CHILD) {
    struct { double min, max; uint64_t width; } payload = { min, max, (uint64_t)width };
    uint64_t bits = child_req_scalar (tc, HEGEL_OP_DRAW_DOUBLE, &payload, sizeof payload);
    memcpy (&v, &bits, sizeof v);
  } else {
    int _rc = hegel__eng_draw_double (tc->eng, tc->htc, min, max, width, &v);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  trace_double (tc, v);
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

int hegel_draw_text (hegel_testcase * tc, int min_size, int max_size,
                     char * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  int len;
  if (tc->mode == HEGEL_TC_CHILD) {
    int64_t payload[2] = { min_size, max_size };
    len = child_req_buffer (tc, HEGEL_OP_DRAW_TEXT, payload, sizeof payload,
                            buf, capacity, 1);
  } else {
    int _rc = hegel__eng_draw_text (tc->eng, tc->htc, min_size, max_size,
                              buf, capacity, &len);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  if (capacity > 0) trace_str (tc, buf);
  return len;
}

int hegel_draw_bytes (hegel_testcase * tc, int min_size, int max_size,
                      uint8_t * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  int len;
  if (tc->mode == HEGEL_TC_CHILD) {
    int64_t payload[2] = { min_size, max_size };
    len = child_req_buffer (tc, HEGEL_OP_DRAW_BYTES, payload, sizeof payload,
                            buf, capacity, 0);
  } else {
    int _rc = hegel__eng_draw_bytes (tc->eng, tc->htc, min_size, max_size,
                               buf, capacity, &len);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: <%d bytes>\n", tc->draw_count, len);
  return len;
}

int hegel_draw_regex (hegel_testcase * tc, const char * pattern,
                      char * buf, int capacity)
{
  if (!tc->silent) tc->draw_count++;
  int len;
  if (tc->mode == HEGEL_TC_CHILD) {
    len = child_req_buffer (tc, HEGEL_OP_DRAW_REGEX,
                            pattern, (uint32_t)strlen (pattern),
                            buf, capacity, 1);
  } else {
    int _rc = hegel__eng_draw_regex (tc->eng, tc->htc, pattern,
                               buf, capacity, &len);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  if (capacity > 0) trace_str (tc, buf);
  return len;
}

/* Internal bool draw (no public hegel_draw_bool in the v0 API; used
** by the legacy combinator generators and HEGEL_OPTIONAL). */
int hegel__draw_bool (hegel_testcase * tc)
{
  if (!tc->silent) tc->draw_count++;
  int v;
  if (tc->mode == HEGEL_TC_CHILD) {
    v = (int)child_req_scalar (tc, HEGEL_OP_DRAW_BOOL, NULL, 0);
  } else {
    int _rc = hegel__eng_draw_bool (tc->eng, tc->htc, &v);
    if (_rc) {
      if (_rc == 1) tc->stop_seen = 1;
      hegel__escape (tc, _rc == 1 ? HEGEL_ESC_STOP : HEGEL_ESC_ASSUME);
    }
  }
  if (tc->final_replay && !tc->silent)
    printf ("Draw %ld: %s\n", tc->draw_count, v ? "true" : "false");
  return v;
}

/* ---- Spans ---- */

void hegel_start_span (hegel_testcase * tc, uint64_t label)
{
  if (tc->mode == HEGEL_TC_CHILD) {
    child_send_req (tc, HEGEL_OP_SPAN_START, &label, sizeof label);
    child_read_status (tc);
    uint64_t ack;
    if (hegel__read_full (tc->resp_fd, &ack, 8) != 0) _exit (1);
  } else {
    tc->eng->start_span (tc->htc, label);
  }
}

void hegel_stop_span (hegel_testcase * tc, int discard)
{
  if (tc->mode == HEGEL_TC_CHILD) {
    uint64_t d = (uint64_t)(discard != 0);
    child_send_req (tc, HEGEL_OP_SPAN_STOP, &d, sizeof d);
    child_read_status (tc);
    uint64_t ack;
    if (hegel__read_full (tc->resp_fd, &ack, 8) != 0) _exit (1);
  } else {
    tc->eng->stop_span (tc->htc, discard != 0);
  }
}

/* ---- Notes, assume, fail ---- */

void hegel_note (hegel_testcase * tc, const char * msg)
{
  if (tc->final_replay) {
    printf ("%s\n", msg);
    /* Flush now: crash-mode failures (a note's main audience) die by
    ** SIGSEGV without unwinding, which would discard a buffered note. */
    fflush (stdout);
  }
}

void hegel_assume (hegel_testcase * tc, int condition)
{
  if (condition) return;
  hegel__escape (tc, HEGEL_ESC_ASSUME);
}

/* Fail with an explicit origin (used by HEGEL_ASSERT with file:line).
** Not yet in the public header — hegel_fail covers the public ABI. */
void hegel_fail_with_origin (const char * origin, const char * msg)
{
  hegel_testcase * tc = hegel__current_tc;
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg, "%s", msg ? msg : "(no message)");
  snprintf (hegel__fail_origin, sizeof hegel__fail_origin, "%s",
            origin ? origin : "hegel_fail");
  if (!tc) {
    fprintf (stderr, "hegel-c: hegel_fail outside a test case: %s\n", hegel__fail_msg);
    abort ();
  }
  if (tc->final_replay)
    printf ("Property test failed: %s\n", hegel__fail_msg);
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
  if (tc->mode == HEGEL_TC_CHILD) {
    size_t llen = strlen (label);
    size_t plen = 8 + llen;
    uint8_t * payload = malloc (plen);
    if (!payload) abort ();
    memcpy (payload, &value, 8);
    memcpy (payload + 8, label, llen);
    child_send_req (tc, HEGEL_OP_TARGET, payload, (uint32_t)plen);
    free (payload);
    child_read_status (tc);
    uint64_t ack;
    if (hegel__read_full (tc->resp_fd, &ack, 8) != 0) _exit (1);
  } else {
    tc->eng->target (tc->htc, value, label);
  }
}
