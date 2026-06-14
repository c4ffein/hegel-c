/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Run drivers: the host-side loop over libhegel's run API, with two
** test-body transports —
**
**   fork mode (default): each test case runs in a freshly forked
**   child; the parent serves its draw requests over a pipe pair and
**   converts the child's terminal message (or its crash) into a
**   test-case status for the engine.
**
**   nofork mode: the body runs in-process; failures longjmp back
**   here.  No crash isolation.
**
** Plus the suite API, which is now just a loop — there is no server
** process left to amortize, but the API keeps multi-test binaries
** working unchanged.
*/

#include "hegel_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define HEGEL_DEFAULT_CASES 100

/* ---- Per-case outcome collected by the drivers ---- */

typedef struct {
  int  status;        /* HG_STATUS_*, or -1 = no terminal message (crash?) */
  int  health;        /* hegel_health_fail fired — abort the whole run     */
  char origin[512];
  char msg[1024];
} case_outcome;

/* ---- Fork-mode parent: serve one child's requests ---- */

/* Map an engine-layer draw result (0 ok / 1 stop / 2 assume) to a
** wire status byte. */
static uint8_t rc_status (int rc)
{
  if (rc == 1) return HEGEL_RESP_STOP;
  if (rc == 2) return HEGEL_RESP_ASSUME;
  return HEGEL_RESP_OK;
}

static void serve_reply_scalar (int resp_fd, int rc, uint64_t value)
{
  uint8_t head[8] = {0};
  head[0] = rc_status (rc);
  hegel__write_full (resp_fd, head, sizeof head);
  if (rc == 0) hegel__write_full (resp_fd, &value, 8);
}

static void serve_reply_buffer (int resp_fd, int rc, const uint8_t * p, size_t n)
{
  uint8_t head[8] = {0};
  head[0] = rc_status (rc);
  hegel__write_full (resp_fd, head, sizeof head);
  if (rc == 0) {
    uint32_t len = (uint32_t)n;
    hegel__write_full (resp_fd, &len, 4);
    if (n) hegel__write_full (resp_fd, p, n);
  }
}

static void serve_child (const hg_engine * eng, hg_tc * htc,
                         int req_fd, int resp_fd, case_outcome * out)
{
  out->status = -1;
  out->health = 0;
  out->origin[0] = '\0';
  out->msg[0] = '\0';
  int served_stop = 0;

  uint8_t small[4096];

  for (;;) {
    hegel_msg_header h;
    if (hegel__read_full (req_fd, &h, sizeof h) != 0) return;  /* EOF */

    uint8_t * payload = small;
    if (h.len > sizeof small) {
      payload = malloc (h.len);
      if (!payload) return;
    }
    if (h.len && hegel__read_full (req_fd, payload, h.len) != 0) {
      if (payload != small) free (payload);
      return;
    }

    switch (h.op) {
      case HEGEL_OP_DRAW_INT: {
        int64_t mm[2];
        memcpy (mm, payload, sizeof mm);
        int64_t v = 0;
        int rc = hegel__eng_draw_int (eng, htc, mm[0], mm[1], &v);
        if (rc == 1) served_stop = 1;
        serve_reply_scalar (resp_fd, rc, (uint64_t)v);
        break;
      }
      case HEGEL_OP_DRAW_U64: {
        uint64_t mm[2];
        memcpy (mm, payload, sizeof mm);
        uint64_t v = 0;
        int rc = hegel__eng_draw_u64 (eng, htc, mm[0], mm[1], &v);
        if (rc == 1) served_stop = 1;
        serve_reply_scalar (resp_fd, rc, v);
        break;
      }
      case HEGEL_OP_DRAW_DOUBLE: {
        struct { double min, max; uint64_t width; } p;
        memcpy (&p, payload, sizeof p);
        double v = 0;
        int rc = hegel__eng_draw_double (eng, htc, p.min, p.max, (int)p.width, &v);
        if (rc == 1) served_stop = 1;
        uint64_t bits;
        memcpy (&bits, &v, 8);
        serve_reply_scalar (resp_fd, rc, bits);
        break;
      }
      case HEGEL_OP_DRAW_BOOL: {
        int v = 0;
        int rc = hegel__eng_draw_bool (eng, htc, &v);
        if (rc == 1) served_stop = 1;
        serve_reply_scalar (resp_fd, rc, (uint64_t)v);
        break;
      }
      case HEGEL_OP_DRAW_TEXT: {
        int64_t mm[2];
        memcpy (mm, payload, sizeof mm);
        const uint8_t * s = NULL; size_t slen = 0;
        int rc = hegel__eng_draw_text_raw (eng, htc, mm[0], mm[1], &s, &slen);
        if (rc == 1) served_stop = 1;
        serve_reply_buffer (resp_fd, rc, s, slen);
        break;
      }
      case HEGEL_OP_DRAW_BYTES: {
        int64_t mm[2];
        memcpy (mm, payload, sizeof mm);
        const uint8_t * s = NULL; size_t slen = 0;
        int rc = hegel__eng_draw_bytes_raw (eng, htc, mm[0], mm[1], &s, &slen);
        if (rc == 1) served_stop = 1;
        serve_reply_buffer (resp_fd, rc, s, slen);
        break;
      }
      case HEGEL_OP_DRAW_REGEX: {
        /* Payload is the (non-NUL-terminated) pattern. */
        char * pattern = malloc (h.len + 1);
        if (!pattern) { if (payload != small) free (payload); return; }
        memcpy (pattern, payload, h.len);
        pattern[h.len] = '\0';
        const uint8_t * s = NULL; size_t slen = 0;
        int rc = hegel__eng_draw_regex_raw (eng, htc, pattern, &s, &slen);
        free (pattern);
        if (rc == 1) served_stop = 1;
        serve_reply_buffer (resp_fd, rc, s, slen);
        break;
      }
      case HEGEL_OP_SPAN_START: {
        uint64_t label;
        memcpy (&label, payload, 8);
        eng->start_span (htc, label);
        serve_reply_scalar (resp_fd, 0, 0);
        break;
      }
      case HEGEL_OP_SPAN_STOP: {
        uint64_t discard;
        memcpy (&discard, payload, 8);
        eng->stop_span (htc, discard != 0);
        serve_reply_scalar (resp_fd, 0, 0);
        break;
      }
      case HEGEL_OP_TARGET: {
        double value;
        memcpy (&value, payload, 8);
        char * label = malloc (h.len - 8 + 1);
        if (label) {
          memcpy (label, payload + 8, h.len - 8);
          label[h.len - 8] = '\0';
          eng->target (htc, value, label);
          free (label);
        }
        serve_reply_scalar (resp_fd, 0, 0);
        break;
      }

      /* Terminal messages */
      case HEGEL_OP_DONE:
        out->status = served_stop ? HG_STATUS_OVERRUN : HG_STATUS_VALID;
        if (payload != small) free (payload);
        return;
      case HEGEL_OP_STOPPED:
        out->status = HG_STATUS_OVERRUN;
        if (payload != small) free (payload);
        return;
      case HEGEL_OP_ASSUME:
        out->status = HG_STATUS_INVALID;
        if (payload != small) free (payload);
        return;
      case HEGEL_OP_FAIL: {
        uint32_t olen, mlen;
        memcpy (&olen, payload, 4);
        size_t ocopy = olen < sizeof out->origin - 1 ? olen : sizeof out->origin - 1;
        memcpy (out->origin, payload + 4, ocopy);
        out->origin[ocopy] = '\0';
        memcpy (&mlen, payload + 4 + olen, 4);
        size_t mcopy = mlen < sizeof out->msg - 1 ? mlen : sizeof out->msg - 1;
        memcpy (out->msg, payload + 4 + olen + 4, mcopy);
        out->msg[mcopy] = '\0';
        out->status = HG_STATUS_INTERESTING;
        if (payload != small) free (payload);
        return;
      }
      case HEGEL_OP_HEALTH: {
        size_t mcopy = h.len < sizeof out->msg - 1 ? h.len : sizeof out->msg - 1;
        memcpy (out->msg, payload, mcopy);
        out->msg[mcopy] = '\0';
        out->health = 1;
        out->status = HG_STATUS_INVALID;
        if (payload != small) free (payload);
        return;
      }

      default:
        fprintf (stderr, "hegel-c: parent received unknown op %u\n", h.op);
        if (payload != small) free (payload);
        return;
    }
    if (payload != small) free (payload);
  }
}

/* Run one test case in a forked child; fill `out`. */
static void run_case_forked (const hg_engine * eng, hg_tc * htc,
                             void (*test_fn)(hegel_testcase *),
                             int final_replay, case_outcome * out)
{
  int req[2], resp[2];
  if (pipe (req) != 0 || pipe (resp) != 0) {
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
    /* Child: run the body, draws go over the pipe. */
    close (req[0]);
    close (resp[1]);
    hegel_testcase tc;
    memset (&tc, 0, sizeof tc);
    tc.mode = HEGEL_TC_CHILD;
    tc.req_fd = req[1];
    tc.resp_fd = resp[0];
    tc.final_replay = final_replay;
    hegel__current_tc = &tc;
    if (hegel__case_setup) hegel__case_setup ();
    test_fn (&tc);
    fflush (stdout);
    hegel__child_send_terminal (&tc, tc.stop_seen ? HEGEL_OP_STOPPED : HEGEL_OP_DONE);
    _exit (0);
  }

  /* Parent */
  close (req[1]);
  close (resp[0]);
  serve_child (eng, htc, req[0], resp[1], out);
  close (req[0]);
  close (resp[1]);

  int wstatus = 0;
  while (waitpid (pid, &wstatus, 0) < 0) { /* EINTR */ }

  if (out->status == -1) {
    /* No terminal message — the child died mid-body. */
    out->status = HG_STATUS_INTERESTING;
    if (WIFSIGNALED (wstatus)) {
      int sig = WTERMSIG (wstatus);
      snprintf (out->origin, sizeof out->origin, "crash: signal %d (%s)",
                sig, strsignal (sig));
      snprintf (out->msg, sizeof out->msg, "test case crashed with signal %d (%s)",
                sig, strsignal (sig));
    } else {
      snprintf (out->origin, sizeof out->origin, "crash: exit %d",
                WIFEXITED (wstatus) ? WEXITSTATUS (wstatus) : -1);
      snprintf (out->msg, sizeof out->msg,
                "test case exited without a verdict (exit code %d)",
                WIFEXITED (wstatus) ? WEXITSTATUS (wstatus) : -1);
    }
    if (final_replay)
      printf ("Property test failed: %s\n", out->msg);
  }
}

/* Run one test case in-process; fill `out`. */
static void run_case_inproc (const hg_engine * eng, hg_tc * htc,
                             void (*test_fn)(hegel_testcase *),
                             int final_replay, case_outcome * out)
{
  out->health = 0;
  out->origin[0] = '\0';
  out->msg[0] = '\0';

  hegel_testcase tc;
  memset (&tc, 0, sizeof tc);
  tc.mode = HEGEL_TC_INPROC;
  tc.eng = eng;
  tc.htc = htc;
  tc.final_replay = final_replay;

  jmp_buf env;
  tc.escape = &env;
  hegel__current_tc = &tc;

  int code = setjmp (env);
  if (code == 0) {
    if (hegel__case_setup) hegel__case_setup ();
    test_fn (&tc);
    out->status = tc.stop_seen ? HG_STATUS_OVERRUN : HG_STATUS_VALID;
  }

  /* Per-case cleanup runs on every exit path (normal or escape) —
  ** the C analogue of stateful.rs's RAII teardown guard. */
  if (tc.cleanup) {
    void (*fn)(void *) = tc.cleanup;
    tc.cleanup = NULL;
    fn (tc.cleanup_arg);
  }

  if (code != 0) {
    switch (code) {
      case HEGEL_ESC_FAIL:
        out->status = HG_STATUS_INTERESTING;
        snprintf (out->origin, sizeof out->origin, "%s", hegel__fail_origin);
        snprintf (out->msg, sizeof out->msg, "%s", hegel__fail_msg);
        break;
      case HEGEL_ESC_ASSUME:
        out->status = HG_STATUS_INVALID;
        break;
      case HEGEL_ESC_STOP:
        out->status = HG_STATUS_OVERRUN;
        break;
      case HEGEL_ESC_HEALTH:
        out->status = HG_STATUS_INVALID;
        out->health = 1;
        snprintf (out->msg, sizeof out->msg, "%s", hegel__fail_msg);
        break;
    }
  }
  hegel__current_tc = NULL;
}

/* ---- The run loop ---- */

static int report_failures (const hg_engine * eng, const hg_result * result)
{
  size_t nf = eng->result_failure_count (result);
  for (size_t i = 0; i < nf; i++) {
    const hg_failure * f = eng->result_failure (result, i);
    const char * panic = eng->failure_panic_message (f);
    const char * diag  = eng->failure_diagnostic (f);
    if (panic && strncmp (panic, "FailedHealthCheck", 17) == 0) {
      fprintf (stderr, "Health check failure: %s\n", panic);
    } else {
      fprintf (stderr, "\n=== hegel-c: property test FAILED ===\n");
      const char * origin = eng->failure_origin (f);
      if (origin) fprintf (stderr, "origin: %s\n", origin);
      if (diag)   fprintf (stderr, "%s\n", diag);
      const char * blob = eng->failure_reproduction_blob (f);
      if (blob)   fprintf (stderr, "reproduce blob: %s\n", blob);
    }
  }
  return nf > 0 ? 1 : 0;
}

static int run_property (void (*test_fn)(hegel_testcase *),
                         uint64_t n_cases, int use_fork)
{
  const hg_engine * eng = hg_engine_get ();
  if (!eng) return 1;

  hg_settings * s = eng->settings_new ();
  eng->settings_test_cases (s, n_cases);
  eng->settings_verbosity (s, HG_VERBOSITY_QUIET);
  eng->settings_database (s, "");           /* no example DB — match old behavior */
  eng->settings_report_multiple_failures (s, false);

  hg_run * run = eng->run_start (s);
  if (!run) {
    fprintf (stderr, "hegel-c: hegel_run_start failed: %s\n",
             eng->last_error_message ());
    eng->settings_free (s);
    return 1;
  }

  int health_failed = 0;
  hg_tc * htc;
  while ((htc = eng->next_test_case (run)) != NULL) {
    int final_replay = eng->tc_is_final_replay (htc);
    case_outcome out;

    if (use_fork)
      run_case_forked (eng, htc, test_fn, final_replay, &out);
    else
      run_case_inproc (eng, htc, test_fn, final_replay, &out);

    eng->mark_complete (htc, out.status,
                        out.status == HG_STATUS_INTERESTING ? out.origin : NULL);

    if (out.health) {
      fprintf (stderr, "Health check failure: %s\n", out.msg);
      health_failed = 1;
      break;
    }
  }

  int rc;
  if (health_failed) {
    rc = 1;
  } else {
    const char * err = eng->last_error_message ();
    if (err && err[0]) {
      fprintf (stderr, "hegel-c: run loop error: %s\n", err);
      rc = 1;
    } else {
      const hg_result * result = eng->run_result (run);
      if (!result) {
        fprintf (stderr, "hegel-c: hegel_run_result failed: %s\n",
                 eng->last_error_message ());
        rc = 1;
      } else {
        rc = report_failures (eng, result);
      }
    }
  }

  eng->run_free (run);
  eng->settings_free (s);
  return rc;
}

/* ---- Public runners ---- */

void hegel_run_test (void (*test_fn)(hegel_testcase *))
{
  if (run_property (test_fn, HEGEL_DEFAULT_CASES, 1)) exit (1);
}

void hegel_run_test_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  if (run_property (test_fn, n_cases, 1)) exit (1);
}

int hegel_run_test_result (void (*test_fn)(hegel_testcase *))
{
  return run_property (test_fn, HEGEL_DEFAULT_CASES, 1);
}

int hegel_run_test_result_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  return run_property (test_fn, n_cases, 1);
}

void hegel_run_test_nofork (void (*test_fn)(hegel_testcase *))
{
  if (run_property (test_fn, HEGEL_DEFAULT_CASES, 0)) exit (1);
}

void hegel_run_test_nofork_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  if (run_property (test_fn, n_cases, 0)) exit (1);
}

/* Internal entry for the stateful runner (hegel_stateful.c): nofork
** driver with a custom body. */
int hegel__run_property_nofork (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  return run_property (test_fn, n_cases, 0);
}

/* ---- Suite ---- */

struct HegelSuite {
  const char * names[256];
  void (*fns[256])(hegel_testcase *);
  int count;
};

hegel_suite * hegel_suite_new (void)
{
  hegel_suite * s = calloc (1, sizeof *s);
  if (!s) abort ();
  return s;
}

void hegel_suite_add (hegel_suite * suite, const char * name,
                      void (*test_fn)(hegel_testcase *))
{
  if (suite->count >= 256) {
    fprintf (stderr, "hegel-c: suite is full (256 tests)\n");
    abort ();
  }
  suite->names[suite->count] = name;
  suite->fns[suite->count] = test_fn;
  suite->count++;
}

int hegel_suite_run (hegel_suite * suite)
{
  int failures = 0;
  for (int i = 0; i < suite->count; i++) {
    printf ("[suite] %s\n", suite->names[i]);
    fflush (stdout);
    int rc = hegel_run_test_result (suite->fns[i]);
    printf ("[suite] %s: %s\n", suite->names[i], rc == 0 ? "PASS" : "FAIL");
    fflush (stdout);
    if (rc != 0) failures++;
  }
  if (failures)
    printf ("[suite] %d/%d tests failed\n", failures, suite->count);
  return failures ? 1 : 0;
}

void hegel_suite_free (hegel_suite * suite)
{
  free (suite);
}
