/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Run drivers: the host-side loop over libhegel's run API, with two
** test-body transports —
**
**   fork mode (default): each test case runs in a freshly forked
**   child; the parent serves its requests over a pipe pair and converts
**   the child's terminal message (or its crash) into a test-case status
**   for the engine.
**
**   nofork mode: the body runs in-process and calls the server
**   directly; failures longjmp back here.  No crash isolation (outside
**   HEGEL_ISOLATED blocks).
**
** Failure output follows the engine's capture contract: only test cases
** the engine stamps (hegel_test_case_should_capture) record their notes
** and draw traces; a stamped case that fails stores its lines under its
** origin, and the report prints, for each failure the run surfaced, the
** lines of the last stamped execution with that origin (the final
** replay of the minimal example).
*/

#include "hegel_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/personality.h>
#endif


/* ---- Captured failure reports, keyed by origin ---- */

typedef struct {
  char * origin;
  char * msg;
  char * lines;
} captured;

static captured * g_caps = NULL;
static int        g_n_caps = 0;

static void capture_store (const char * origin, const char * msg, char * lines)
{
  for (int i = 0; i < g_n_caps; i++)
    if (strcmp (g_caps[i].origin, origin) == 0) {
      free (g_caps[i].msg);
      free (g_caps[i].lines);
      g_caps[i].msg = strdup (msg);
      g_caps[i].lines = lines;
      return;
    }
  g_caps = realloc (g_caps, (size_t) (g_n_caps + 1) * sizeof *g_caps);
  if (!g_caps) abort ();
  g_caps[g_n_caps].origin = strdup (origin);
  g_caps[g_n_caps].msg = strdup (msg);
  g_caps[g_n_caps].lines = lines;
  g_n_caps++;
}

static const captured * capture_find (const char * origin, size_t len)
{
  for (int i = 0; i < g_n_caps; i++)
    if (strlen (g_caps[i].origin) == len && memcmp (g_caps[i].origin, origin, len) == 0)
      return &g_caps[i];
  return NULL;
}

static void capture_clear (void)
{
  for (int i = 0; i < g_n_caps; i++) {
    free (g_caps[i].origin);
    free (g_caps[i].msg);
    free (g_caps[i].lines);
  }
  free (g_caps);
  g_caps = NULL;
  g_n_caps = 0;
}

/* ---- Fork-mode parent: serve one child's requests ---- */

static void serve_child (hegel_case_srv * srv, int req_fd, int resp_fd, hegel_outcome * out)
{
  hegel_buf payload, resp;
  hegel__buf_init (&payload);
  hegel__buf_init (&resp);

  for (;;) {
    hegel_msg_header h;
    if (hegel__read_full (req_fd, &h, sizeof h) != 0) break;  /* EOF: crash */
    payload.len = 0;
    uint8_t chunk[4096];
    uint32_t left = h.len;
    int eof = 0;
    while (left) {
      uint32_t n = left < sizeof chunk ? left : (uint32_t) sizeof chunk;
      if (hegel__read_full (req_fd, chunk, n) != 0) { eof = 1; break; }
      hegel__buf_put (&payload, chunk, n);
      left -= n;
    }
    if (eof) break;

    resp.len = 0;
    uint8_t st = 0;
    int kind = hegel__serve_op (srv, h.op, payload.data, h.len, &st, &resp, out);
    if (kind == 2) break;          /* terminal */
    if (kind == 1) continue;       /* fire-and-forget */
    hegel_msg_header rh = { st, {0,0,0}, (uint32_t) resp.len };
    if (hegel__write_full (resp_fd, &rh, sizeof rh) != 0
        || (resp.len && hegel__write_full (resp_fd, resp.data, resp.len) != 0))
      break;
  }
  hegel__buf_free (&payload);
  hegel__buf_free (&resp);
}

static void crash_outcome (hegel_case_srv * srv, int wstatus, hegel_outcome * out)
{
  const char * site = hegel__srv_iso_site (srv);
  char where[300] = "";
  if (site) snprintf (where, sizeof where, " in HEGEL_ISOLATED at %s", site);
  out->status = HG_STATUS_INTERESTING;
  if (site)   /* the block is the origin, whatever the crash (see hegel_fail_with_origin) */
    snprintf (out->origin, sizeof out->origin, "HEGEL_ISOLATED at %s", site);
  if (WIFSIGNALED (wstatus)) {
    int sig = WTERMSIG (wstatus);
    if (!site)
      snprintf (out->origin, sizeof out->origin, "crash: signal %d (%s)", sig, strsignal (sig));
    snprintf (out->msg, sizeof out->msg, "test case crashed with signal %d (%s)%s",
              sig, strsignal (sig), where);
  } else {
    int code = WIFEXITED (wstatus) ? WEXITSTATUS (wstatus) : -1;
    if (!site)
      snprintf (out->origin, sizeof out->origin, "crash: exit %d", code);
    snprintf (out->msg, sizeof out->msg,
              "test case exited without a verdict (exit code %d)%s", code, where);
  }
  char line[1100];
  int n = snprintf (line, sizeof line, "Property test failed: %s", out->msg);
  hegel__srv_add_line (srv, line, (size_t) n);
}

/* Run one test case in a forked child; fill `out`. */
/* ---- Fork modes: fast (fork) and clean (fork + exec) ----
**
** fast:  the child is a fork of the runner, so it inherits the runner's
**        heap — engine allocations included, different on every case.
**        Code that reads or writes past its buffers then sees different
**        neighbours from case to case, and its outcome can change on
**        replay (the engine reports a "nondeterministic failure" caveat
**        and shrinks worse).
** clean: the child execs the test binary again (/proc/self/exe, same
**        argv, ASLR off), so every case starts from the same pristine
**        process.  The re-exec'd binary runs main() from the top with
**        stdout/stderr muted; each hegel run it reaches before the target
**        returns "passed" at once, and the target run becomes the child,
**        talking over the inherited pipe.  main() must therefore reach
**        the same runs in the same order each time (it does, unless it
**        branches on time, input or randomness before the run).  Costs
**        one exec + main() prologue per case.  Linux only.
**
** Choose with hegel_set_fork_mode() or HEGEL_C_FORK=fast|clean (the
** environment wins). */

#define CLEAN_ENV "HEGEL_C__CLEAN_CHILD"

static hegel_fork_mode g_fork_mode = HEGEL_FORK_FAST;
static uint64_t        g_run_index;           /* runs started by this process */
static char            g_results[4096];       /* '0'/'1' per finished run, in order */

static struct {
  int      active;                            /* this process is a re-exec'd child   */
  uint64_t target;                            /* ... for this run index              */
  int      req_fd, resp_fd, capture;
  int      out_fd, err_fd;                    /* the real stdout/stderr while muted  */
  char     results[4096];                     /* the parent's results of earlier runs */
} g_clean;

void hegel_set_fork_mode (hegel_fork_mode mode)
{
  g_fork_mode = mode;
}

static hegel_fork_mode fork_mode (void)
{
  const char * e = getenv ("HEGEL_C_FORK");
  if (e && strcmp (e, "clean") == 0) return HEGEL_FORK_CLEAN;
  if (e && strcmp (e, "fast") == 0) return HEGEL_FORK_FAST;
  if (e && *e) {
    fprintf (stderr, "hegel-c: HEGEL_C_FORK must be 'fast' or 'clean', got '%s'\n", e);
    exit (1);
  }
  return g_fork_mode;
}

static void clean_child_unreached (void)
{
  if (!g_clean.active) return;
  dup2 (g_clean.err_fd, 2);
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
            "fork-clean: the re-executed test binary exited before reaching hegel run #%llu "
            "(main() must reach the same runs in the same order every time)",
            (unsigned long long) g_clean.target);
  hegel__send_terminal (g_clean.req_fd, HEGEL_OP_HEALTH);
}

__attribute__((constructor))
static void clean_child_init (void)
{
  const char * e = getenv (CLEAN_ENV);
  unsigned long long target;
  int req, resp, cap, used = 0;
  if (!e || sscanf (e, "%llu:%d:%d:%d:%n", &target, &req, &resp, &cap, &used) != 4 || !used) return;
  snprintf (g_clean.results, sizeof g_clean.results, "%s", e + used);
  unsetenv (CLEAN_ENV);                       /* not for the test's own subprocesses */
  g_clean.active = 1;
  g_clean.target = target;
  g_clean.req_fd = req;
  g_clean.resp_fd = resp;
  g_clean.capture = cap;
  g_clean.out_fd = dup (1);
  g_clean.err_fd = dup (2);
  int null = open ("/dev/null", O_WRONLY);
  if (null >= 0) { dup2 (null, 1); dup2 (null, 2); close (null); }
  atexit (clean_child_unreached);
}

/* In a re-exec'd child: replay the result of each run before the target
** (as the parent got it, so main() takes the same path), become the child
** at the target. */
static int clean_child_run (uint64_t index, void (*test_fn)(hegel_testcase *))
{
  if (index != g_clean.target)
    return index <= strlen (g_clean.results) && g_clean.results[index - 1] == '1';
  fflush (stdout);
  fflush (stderr);
  dup2 (g_clean.out_fd, 1);
  dup2 (g_clean.err_fd, 2);
  g_clean.active = 0;

  hegel_testcase tc;
  memset (&tc, 0, sizeof tc);
  tc.mode = HEGEL_TC_CHILD;
  tc.req_fd = g_clean.req_fd;
  tc.resp_fd = g_clean.resp_fd;
  tc.capture = g_clean.capture;
  hegel__current_tc = &tc;
  if (hegel__case_setup) hegel__case_setup ();
  test_fn (&tc);
  if (tc.cleanup) tc.cleanup (tc.cleanup_arg);
  fflush (NULL);
  hegel__send_terminal (tc.req_fd, tc.stop_seen ? HEGEL_OP_STOPPED : HEGEL_OP_DONE);
  _exit (0);
}

/* In the forked child (clean mode): exec ourselves.  Never returns. */
static void clean_child_exec (uint64_t index, int req_fd, int resp_fd, int capture)
{
  char env[128 + sizeof g_results];
  snprintf (env, sizeof env, "%llu:%d:%d:%d:%s", (unsigned long long) index,
            req_fd, resp_fd, capture, g_results);
  setenv (CLEAN_ENV, env, 1);

  /* argv from /proc/self/cmdline: NUL-separated. */
  static char buf[65536];
  char * argv[1024];
  int n = 0;
  int fd = open ("/proc/self/cmdline", O_RDONLY);
  ssize_t len = fd >= 0 ? read (fd, buf, sizeof buf - 1) : -1;
  if (fd >= 0) close (fd);
  for (ssize_t i = 0; len > 0 && i < len && n < 1023; i += (ssize_t) strlen (buf + i) + 1)
    argv[n ++] = buf + i;
  argv[n] = NULL;
#ifdef __linux__
  personality (ADDR_NO_RANDOMIZE);            /* same addresses every case; best effort */
#endif
  if (n > 0) execv ("/proc/self/exe", argv);
  snprintf (hegel__fail_msg, sizeof hegel__fail_msg,
            "fork-clean: cannot re-execute the test binary via /proc/self/exe "
            "(Linux only; use HEGEL_C_FORK=fast)");
  hegel__send_terminal (req_fd, HEGEL_OP_HEALTH);
  _exit (127);
}

static void run_case_forked (hegel_case_srv * srv, void (*test_fn)(hegel_testcase *),
                             hegel_outcome * out)
{
  int clean = fork_mode () == HEGEL_FORK_CLEAN;
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
    /* Child: run the body, requests go over the pipe. */
    close (req[0]);
    close (resp[1]);
    if (clean) clean_child_exec (g_run_index, req[1], resp[0], hegel__srv_capture (srv));
    hegel_testcase tc;
    memset (&tc, 0, sizeof tc);
    tc.mode = HEGEL_TC_CHILD;
    tc.req_fd = req[1];
    tc.resp_fd = resp[0];
    tc.capture = hegel__srv_capture (srv);
    hegel__current_tc = &tc;
    if (hegel__case_setup) hegel__case_setup ();
    test_fn (&tc);
    if (tc.cleanup) tc.cleanup (tc.cleanup_arg);
    fflush (NULL);
    hegel__send_terminal (tc.req_fd, tc.stop_seen ? HEGEL_OP_STOPPED : HEGEL_OP_DONE);
    _exit (0);
  }

  /* Parent */
  close (req[1]);
  close (resp[0]);
  serve_child (srv, req[0], resp[1], out);
  close (req[0]);
  close (resp[1]);

  int wstatus = 0;
  while (waitpid (pid, &wstatus, 0) < 0) { /* EINTR */ }

  if (out->status == -1) crash_outcome (srv, wstatus, out);
}

/* Run one test case in-process; fill `out`. */
static void run_case_inproc (hegel_case_srv * srv, void (*test_fn)(hegel_testcase *),
                             hegel_outcome * out)
{
  hegel_testcase tc;
  memset (&tc, 0, sizeof tc);
  tc.mode = HEGEL_TC_INPROC;
  tc.srv = srv;
  tc.capture = hegel__srv_capture (srv);

  jmp_buf env;
  tc.escape = &env;
  hegel__current_tc = &tc;

  int code = setjmp (env);
  if (code == 0) {
    if (hegel__case_setup) hegel__case_setup ();
    test_fn (&tc);
    if (tc.mode == HEGEL_TC_ISO) {
      /* A HEGEL_ISOLATED child returned out of the test function. */
      fflush (NULL);
      hegel__send_terminal (tc.req_fd, HEGEL_OP_DONE);
      _exit (0);
    }
    out->status = tc.stop_seen ? HG_STATUS_OVERRUN : HG_STATUS_VALID;
  }

  /* Per-case cleanup runs on every exit path (normal or escape). */
  if (hegel__draws_abandoned) hegel__draws_abandoned ();
  if (tc.cleanup) {
    void (*fn)(void *) = tc.cleanup;
    tc.cleanup = NULL;
    fn (tc.cleanup_arg);
  }

  switch (code) {
    case 0: break;
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
  hegel__current_tc = NULL;
}

/* ---- The run loop ---- */

static void print_str (FILE * f, const char * label, const hg_str * s)
{
  if (s && s->len) fprintf (f, "%s%.*s\n", label, (int) s->len, s->data);
}

/* The line a failure report ends with: paste it in front of the same
** binary to replay that failure (see HEGEL_C_REPRODUCE in hegel_c.h). */
static void print_reproduce (const hg_str * blob, uint64_t run_index)
{
  if (blob && blob->len)
    fprintf (stderr, "reproduce: HEGEL_C_REPRODUCE='%llu:%.*s'\n",
             (unsigned long long) run_index, (int) blob->len, blob->data);
}

static int report (const hg_engine * e, const hg_result * result, uint64_t run_index)
{
  uint32_t status = HG_RUN_PASSED;
  e->result_status (e->ctx, result, &status);
  if (status == HG_RUN_PASSED) return 0;

  if (status == HG_RUN_ERROR) {
    uint32_t kind = 0, hc = 0;
    const hg_str * msg = NULL;
    e->result_error_kind (e->ctx, result, &kind, &hc);
    e->result_error (e->ctx, result, &msg);
    const char * prefix =
        kind == HG_RUN_ERROR_HEALTH_CHECK     ? "Health check failure: "
      : kind == HG_RUN_ERROR_FLAKY            ? "hegel-c: flaky test: "
      : kind == HG_RUN_ERROR_NONDETERMINISTIC ? "hegel-c: nondeterministic generation: "
      : kind == HG_RUN_ERROR_UNSATISFIABLE    ? "Health check failure: unsatisfiable: "
      : kind == HG_RUN_ERROR_USAGE            ? "hegel-c: engine usage error: "
      :                                         "hegel-c: engine internal error: ";
    fprintf (stderr, "%s%.*s\n", prefix, msg ? (int) msg->len : 0, msg ? msg->data : "");
    return 1;
  }

  size_t nf = 0;
  e->result_failure_count (e->ctx, result, &nf);
  for (size_t i = 0; i < nf; i++) {
    hg_failure * f = NULL;
    if (e->result_failure (e->ctx, result, i, &f) != HG_OK) continue;
    hg_str origin = { "", 0 };
    const hg_str * blob = NULL, * caveat = NULL;
    e->failure_origin (e->ctx, f, &origin);
    e->failure_blob (e->ctx, f, &blob);
    e->failure_caveat (e->ctx, f, &caveat);

    const captured * cap = capture_find (origin.data, origin.len);
    if (cap && cap->lines[0]) {
      fputs (cap->lines, stdout);
      fflush (stdout);
    }
    fprintf (stderr, "\n=== hegel-c: property test FAILED ===\n");
    fprintf (stderr, "origin: %.*s\n", (int) origin.len, origin.data);
    if (cap && cap->msg[0]) fprintf (stderr, "message: %s\n", cap->msg);
    print_str (stderr, "caveat: ", caveat);
    print_reproduce (blob, run_index);
    e->failure_free (e->ctx, f);
  }
  return 1;
}

static int run_property_inner (void (*test_fn)(hegel_testcase *),
                               const hegel_run_options * o, int use_fork, uint64_t run_index,
                               const char * blob, size_t blob_len);

/* HEGEL_C_REPRODUCE="N:blob" (or a bare blob = run 1): which run replays,
** and the blob.  Returns 0 when the variable is unset. */
static int reproduce_target (uint64_t * run, const char ** blob, size_t * len)
{
  const char * v = getenv ("HEGEL_C_REPRODUCE");
  if (!v || !*v) return 0;
  const char * colon = strchr (v, ':');
  *run = 1;
  *blob = v;
  if (colon) {
    char * end = NULL;
    unsigned long long n = strtoull (v, &end, 10);
    if (end == colon && n > 0) {
      *run = n;
      *blob = colon + 1;
    }
  }
  *len = strlen (*blob);
  return 1;
}

/* Every run goes through here: numbered, and its result recorded so a
** fork-clean child can replay main() exactly (see clean_child_run). */
static int run_property (void (*test_fn)(hegel_testcase *), const hegel_run_options * o,
                         int use_fork)
{
  static const hegel_run_options defaults;
  uint64_t run_index = ++ g_run_index;
  if (g_clean.active) return clean_child_run (run_index, test_fn);
  if (!o) o = &defaults;

  uint64_t target = 0;
  const char * blob = NULL;
  size_t blob_len = 0;
  int rc;
  if (reproduce_target (&target, &blob, &blob_len) && target != run_index) {
    fprintf (stderr, "hegel-c: run %llu skipped (HEGEL_C_REPRODUCE replays run %llu)\n",
             (unsigned long long) run_index, (unsigned long long) target);
    rc = 0;
  } else {
    rc = run_property_inner (test_fn, o, use_fork, run_index, blob, blob_len);
  }
  size_t n = strlen (g_results);
  if (n + 1 < sizeof g_results) { g_results[n] = rc ? '1' : '0'; g_results[n + 1] = 0; }
  return rc;
}

/* Database key for HEGEL_DATABASE runs: the binary and the run number,
** stable as long as main() starts its runs in the same order. */
static void database_key (char * buf, size_t n, uint64_t run_index)
{
  char exe[512];
  ssize_t len = readlink ("/proc/self/exe", exe, sizeof exe - 1);
  const char * name = "hegel-c";
  if (len > 0) {
    exe[len] = 0;
    const char * slash = strrchr (exe, '/');
    name = slash ? slash + 1 : exe;
  }
  snprintf (buf, n, "%s#%llu", name, (unsigned long long) run_index);
}

static int run_property_inner (void (*test_fn)(hegel_testcase *),
                               const hegel_run_options * o, int use_fork, uint64_t run_index,
                               const char * blob, size_t blob_len)
{
  const hg_engine * e = hg_engine_get ();
  if (!e) return 1;
  hg_context * x = e->ctx;
  const char * merge_env = getenv ("HEGEL_C_MERGE_ORIGINS");
  int merge_origins = merge_env && *merge_env && strcmp (merge_env, "0") != 0;

  hg_settings * s = NULL;
  if (e->settings_new (x, &s) != HG_OK) {
    fprintf (stderr, "hegel-c: settings: %s\n", hg_last_error (e));
    return 1;
  }
  /* Only what the caller set overrides the engine's profile / env. */
  if (o->test_cases) e->settings_set_test_cases (x, s, o->test_cases);
  if (o->has_seed) e->settings_set_seed (x, s, o->seed, true);
  if (o->suppress_health) e->settings_set_suppress_health_check (x, s, o->suppress_health);
  if (o->skip_phases) e->settings_set_phases (x, s, HG_PHASE_ALL & ~o->skip_phases);
  e->settings_set_verbosity (x, s, HG_VERBOSITY_QUIET);
  e->settings_set_report_multiple_failures (x, s, false);
  /* The example database is opt-in (HEGEL_DATABASE): replaying stored
  ** failures first would make runs depend on what earlier runs saw. */
  const char * db_env = getenv ("HEGEL_DATABASE");
  if (db_env && *db_env && strcmp (db_env, "disabled") != 0) {
    char key[600];
    database_key (key, sizeof key, run_index);
    hg_str k = HG_STR_Z (key);
    e->settings_set_database_key (x, s, &k);
  } else {
    hg_str no_db = { "", 0 };
    e->settings_set_database (x, s, &no_db);
  }

  hg_run * run = NULL;
  int started;
  if (blob) {
    hg_str b = { blob, blob_len };
    started = e->run_start_blob (x, s, b, NULL, NULL, &run);
  } else {
    started = e->run_start (x, s, NULL, NULL, &run);
  }
  if (started != HG_OK) {
    fprintf (stderr, "hegel-c: %s failed: %s\n", blob ? "replaying HEGEL_C_REPRODUCE" : "hegel_run_start",
             hg_last_error (e));
    e->settings_free (x, s);
    return 1;
  }

  int rc = 0;
  int health_failed = 0;
  for (;;) {
    hg_tc * htc = NULL;
    if (e->next_test_case (x, run, &htc) != HG_OK) {
      fprintf (stderr, "hegel-c: run loop error: %s\n", hg_last_error (e));
      rc = 1;
      break;
    }
    if (!htc) break;

    bool capture = false;
    e->test_case_should_capture (x, htc, &capture);
    hegel_case_srv * srv = hegel__srv_case_new (e, htc, capture);

    hegel_outcome out;
    memset (&out, 0, sizeof out);
    out.status = -1;
    if (use_fork)
      run_case_forked (srv, test_fn, &out);
    else
      run_case_inproc (srv, test_fn, &out);

    /* HEGEL_C_MERGE_ORIGINS=1: every failure is the same bug to the engine,
    ** so a test whose one bug shows up as a crash OR a bad result (heap
    ** garbage decides) shrinks deterministically.  Distinct bugs merge too:
    ** the report shows only the one the engine settled on. */
    if (merge_origins && out.status == HG_STATUS_INTERESTING)
      snprintf (out.origin, sizeof out.origin, "%s", "any failure (HEGEL_C_MERGE_ORIGINS)");
    if (capture && out.status == HG_STATUS_INTERESTING)
      capture_store (out.origin, out.msg, hegel__srv_take_lines (srv));
    hegel__srv_case_free (srv);

    hg_str origin = HG_STR_Z (out.origin);
    e->mark_complete (x, htc, (uint32_t) out.status,
                      out.status == HG_STATUS_INTERESTING ? &origin : NULL);
    e->test_case_free (x, htc);

    if (out.health) {
      fprintf (stderr, "Health check failure: %s\n", out.msg);
      health_failed = 1;
      break;
    }
  }

  if (health_failed) {
    rc = 1;
  } else if (rc == 0) {
    hg_result * result = NULL;
    if (e->run_result (x, run, &result) != HG_OK) {
      fprintf (stderr, "hegel-c: hegel_run_result failed: %s\n", hg_last_error (e));
      rc = 1;
    } else {
      rc = report (e, result, run_index);
      if (blob && rc == 0)
        fprintf (stderr, "hegel-c: HEGEL_C_REPRODUCE: run %llu did not reproduce the failure "
                         "(passes)\n", (unsigned long long) run_index);
      e->run_result_free (x, result);
    }
  }

  capture_clear ();
  hegel__srv_run_end ();
  e->run_free (x, run);
  e->settings_free (x, s);
  return rc;
}

/* ---- Public runners ---- */

static int run_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases, int use_fork)
{
  hegel_run_options o = { .test_cases = n_cases };
  return run_property (test_fn, &o, use_fork);
}

void hegel_run_test (void (*test_fn)(hegel_testcase *))
{
  if (run_property (test_fn, NULL, 1)) exit (1);
}

void hegel_run_test_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  if (run_n (test_fn, n_cases, 1)) exit (1);
}

int hegel_run_test_result (void (*test_fn)(hegel_testcase *))
{
  return run_property (test_fn, NULL, 1);
}

int hegel_run_test_result_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  return run_n (test_fn, n_cases, 1);
}

void hegel_run_test_nofork (void (*test_fn)(hegel_testcase *))
{
  if (run_property (test_fn, NULL, 0)) exit (1);
}

void hegel_run_test_nofork_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases)
{
  if (run_n (test_fn, n_cases, 0)) exit (1);
}

int hegel_run_test_with (void (*test_fn)(hegel_testcase *), const hegel_run_options * opts)
{
  return run_property (test_fn, opts, 1);
}

int hegel_run_test_nofork_with (void (*test_fn)(hegel_testcase *), const hegel_run_options * opts)
{
  return run_property (test_fn, opts, 0);
}

int hegel__run_property (void (*test_fn)(hegel_testcase *), uint64_t n_cases, int use_fork)
{
  return run_n (test_fn, n_cases, use_fork);
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
