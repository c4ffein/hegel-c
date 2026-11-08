/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef HEGEL_C_H
#define HEGEL_C_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEGEL__STR2(x) #x
#define HEGEL__STR(x) HEGEL__STR2(x)

/* ---- Test case handle ---- */

/* Opaque handle — do not dereference in C */
typedef struct HegelTestCase hegel_testcase;

/* ---- Per-case setup ---- */

/* Register a function called before each test case (fork and nofork).
** Use this to reset global state in the library under test (e.g., RNG
** seeds, caches).  Pass NULL to clear.
**
** Example (Scotch):
**   static void mySetup(void) {
**     SCOTCH_randomSeed (42);
**     SCOTCH_randomReset ();
**   }
**   hegel_set_case_setup (mySetup);
*/
void hegel_set_case_setup (void (*setup_fn)(void));

/* Fork mode for the fork runners (hegel_run_test*, suites, stateful):
**   HEGEL_FORK_FAST  (default) each case is a fork of the runner.  Cheap,
**                    but the child inherits the runner's heap, which differs
**                    case to case: code that touches memory out of bounds
**                    may then fail differently on replay (a
**                    "nondeterministic failure" caveat, worse shrinking).
**   HEGEL_FORK_CLEAN each case re-executes the test binary (fresh process,
**                    ASLR off), so the same input sees the same memory.
**                    main() is re-run with output muted up to the run being
**                    tested, so it must reach the same runs in the same
**                    order every time.  Linux only; ~1 exec per case.
** HEGEL_C_FORK=fast|clean in the environment overrides this. */
typedef enum { HEGEL_FORK_FAST = 0, HEGEL_FORK_CLEAN = 1 } hegel_fork_mode;
void hegel_set_fork_mode (hegel_fork_mode mode);

/* ---- Test runners ---- */

/* Run a property test: test_fn is called once per generated test case.
** Each test case runs in a forked child process (crash-safe).
** On failure, prints the counterexample and calls exit(1). */
void hegel_run_test (void (*test_fn)(hegel_testcase *));
void hegel_run_test_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* Same semantics but returns 0 on success, 1 on failure instead of
** calling exit().  Allows running multiple tests in one binary. */
int hegel_run_test_result (void (*test_fn)(hegel_testcase *));
int hegel_run_test_result_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* Same as above but WITHOUT per-case fork isolation: the body runs in
** the runner's own process, so generation is a plain function call into
** the engine.  A crash outside a HEGEL_ISOLATED block kills the run with
** no shrinking — wrap the code under test in HEGEL_ISOLATED (below) to
** get crash isolation exactly where it matters. */
void hegel_run_test_nofork (void (*test_fn)(hegel_testcase *));
void hegel_run_test_nofork_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* ---- Run options ---- */

/* Per-run settings for the _with runners.  All-zero means the defaults,
** so set only what you need:
**
**   hegel_run_options o = { .test_cases = 1000,
**                           .suppress_health = HEGEL_SUPPRESS_FILTER_TOO_MUCH };
**   int rc = hegel_run_test_with (test_foo, &o);
**
** Settings the code leaves at zero come from the engine: its profile
** (`ci` on a CI server: derandomized), hegel.toml, and the HEGEL_*
** environment variables — HEGEL_TEST_CASES, HEGEL_SEED,
** HEGEL_DERANDOMIZE, HEGEL_DATABASE, ...  A field set here wins over
** those.  (The plain runners behave like all-zero options; the _n ones
** set test_cases.)
**
** The example database (failures remembered across runs, replayed
** first) is OFF unless HEGEL_DATABASE names a directory; each run then
** gets its own key, "<binary>#<run number>". */
typedef struct {
  uint64_t            test_cases;       /* 0: engine default (100) / HEGEL_TEST_CASES */
  uint32_t            suppress_health;  /* HEGEL_SUPPRESS_* bits */
  uint32_t            skip_phases;      /* HEGEL_SKIP_* bits */
  int                 has_seed;         /* nonzero: use seed below */
  uint64_t            seed;
  int                 report_multiple;  /* nonzero: report every distinct failure */
  /* ---- engine settings (see "Engine settings and profiles" below) ---- */
  const char *        profile;          /* NULL: the default profile */
  const char *        settings;         /* "key=value key=value ...", NULL: none */
  int                 derandomize;      /* HEGEL_ON / HEGEL_OFF, 0: profile / env */
  int                 stop_on_first_failure;  /* HEGEL_ON / HEGEL_OFF */
  int                 show_statistics;  /* HEGEL_ON / HEGEL_OFF */
  int                 print_blob;       /* HEGEL_ON / HEGEL_OFF: the reproduce line */
  int                 unbounded_choices;  /* HEGEL_ON / HEGEL_OFF */
  uint32_t            backend;          /* HEGEL_BACKEND_*, 0: profile / env */
  uint32_t            nondeterminism;   /* HEGEL_NONDETERMINISM_*, 0: profile / env */
  const char *        test_file;        /* test location (Antithesis reports), */
  uint32_t            test_line;        /*   NULL test_file: "<binary>", line 0 */
  const char *        test_function;    /* NULL: suite test name, else "run<N>" */
} hegel_run_options;

/* report_multiple: by default a run stops at its first failure (one bug
** per run: a second one stays hidden until the first is fixed).  With it,
** the engine keeps looking and shrinks every failure with a distinct
** origin (each HEGEL_ASSERT's file:line, each crash signal, each
** HEGEL_ISOLATED block); each is reported with its own reproduce line.
** HEGEL_REPORT_MULTIPLE_FAILURES=1 in the environment does the same
** without a code change.  Costs shrinking time per extra bug, and a crash
** whose signal varies counts as several unless HEGEL_C_MERGE_ORIGINS=1. */

/* Health checks a run may suppress (bits, as the engine's). */
#define HEGEL_SUPPRESS_FILTER_TOO_MUCH          (1u << 0)
#define HEGEL_SUPPRESS_TOO_SLOW                 (1u << 1)
#define HEGEL_SUPPRESS_TEST_CASES_TOO_LARGE     (1u << 2)
#define HEGEL_SUPPRESS_LARGE_INITIAL_TEST_CASE  (1u << 3)
#define HEGEL_SUPPRESS_ALL_HEALTH               (0xfu)

/* Engine phases a run may skip (bits, as the engine's). */
#define HEGEL_SKIP_EXPLICIT   (1u << 0)
#define HEGEL_SKIP_REUSE      (1u << 1)         /* database replay */
#define HEGEL_SKIP_GENERATE   (1u << 2)
#define HEGEL_SKIP_TARGET     (1u << 3)         /* hegel_target hill-climbing */
#define HEGEL_SKIP_SHRINK     (1u << 4)         /* report failures unshrunk */

/* ---- Engine settings and profiles ----
**
** The remaining engine settings, as hegel_run_options fields.  Boolean
** ones are tri-state: 0 keeps what the profile / hegel.toml / HEGEL_*
** environment resolved, HEGEL_ON or HEGEL_OFF sets it.
**
**   derandomize            seed from a stable hash instead of fresh
**                          randomness (when no seed is set): every run
**                          of the binary sees the same cases.  The `ci`
**                          profile turns it on.
**   stop_on_first_failure  default ON: generation stops at the first
**                          failure.  OFF spends the whole test_cases
**                          budget first (every failure found is kept;
**                          report_multiple decides how many are shown).
**   show_statistics        the engine prints its statistics block at the
**                          end of the run, on stderr: per hegel_event
**                          label, the fraction of generated cases it
**                          occurred in; per hegel_event_value label, a
**                          distribution summary.  Turns the engine's
**                          notices on for the run (see below).
**   print_blob             default ON: each failure report ends with its
**                          `reproduce: HEGEL_C_REPRODUCE=...` line; OFF
**                          leaves it out (replay still works from the
**                          database).  The engine never acts on it.
**   unbounded_choices      lift the 2^20-choices-per-case limit.
**   backend                HEGEL_BACKEND_DEFAULT (a seeded PRNG) or
**                          HEGEL_BACKEND_URANDOM (fresh entropy per draw,
**                          for Antithesis; no reproducible runs).
**   nondeterminism         what a test whose replay diverges gets:
**                          _QUIET (confirm failures by replay, silently;
**                          the default), _WARN (the same with a one-line
**                          notice, which turns the engine's notices on)
**                          or _ERROR (the run errors out).
**
** `settings` passes engine settings as text, the way a hegel.toml profile
** names them: "key=value" pairs separated by spaces, tabs or ';' (a value
** holds no space: "phases=generate,shrink").  Keys: test_cases, seed,
** derandomize, database, verbosity, phases, suppress_health_check,
** report_multiple_failures, stop_on_first_failure, show_statistics,
** print_blob, backend, nondeterminism_strictness, unbounded_choices,
** database_key.  A bad key or value fails the run (returns 1) with the
** engine's message.
**
** `profile` starts the run from a named profile instead of the default
** one: `base`, `development`, `ci`, `workload`, a [profile] section of
** hegel.toml, or one registered with hegel_register_profile.  An unknown
** name fails the run.
**
** Precedence, lowest to highest:
**   1. the profile: `profile` if set, else the default profile — the one
**      hegel_set_default_profile named, else HEGEL_DEFAULT_PROFILE, else
**      hegel.toml's `default =`, else `ci` on a CI server, else
**      `development` (a named profile layers over the default one,
**      except `base`); hegel.toml's section for it merges over it;
**   2. the HEGEL_* environment variables (HEGEL_TEST_CASES, HEGEL_SEED,
**      HEGEL_DERANDOMIZE, HEGEL_STATISTICS, HEGEL_PRINT_BLOB, ...);
**   3. the `settings` string, pair by pair, left to right;
**   4. the typed fields of hegel_run_options;
**   5. what the runner owns, whatever came before: the verbosity (QUIET,
**      or NORMAL when show_statistics or nondeterminism WARN resolved
**      on), the database (off unless HEGEL_DATABASE names a directory;
**      the key is "<binary>#<run>"), report_multiple_failures (off
**      unless report_multiple or HEGEL_REPORT_MULTIPLE_FAILURES), and
**      the test location.  So `verbosity`, `database`, `database_key`
**      and `report_multiple_failures` from a profile or the settings
**      string are overridden.
**
** Test location: where the test lives, for the engine's Antithesis
** reporting (outside Antithesis it is unused): the property is reported
** as "<binary>::<test_function> passes properties" at test_file:test_line.
** HEGEL_TEST_LOCATION (fn) fills the three fields at the call site:
**
**   hegel_run_options o = { HEGEL_TEST_LOCATION (test_foo), .test_cases = 500 };
*/
#define HEGEL_ON    1
#define HEGEL_OFF (-1)

#define HEGEL_BACKEND_DEFAULT  1u
#define HEGEL_BACKEND_URANDOM  2u

#define HEGEL_NONDETERMINISM_QUIET  1u
#define HEGEL_NONDETERMINISM_WARN   2u
#define HEGEL_NONDETERMINISM_ERROR  3u

#define HEGEL_TEST_LOCATION(fn) \
  .test_file = __FILE__, .test_line = __LINE__, .test_function = #fn

/* Register `opts` as the engine profile `name` (ASCII letters, digits,
** '-' and '_'; not `base` or `default`), process-wide, replacing an
** earlier registration.  The snapshot starts from opts->profile (NULL:
** the default profile) with the environment applied, then opts->settings
** and the typed fields, as a run would; runner-owned fields
** (report_multiple, the test location) are not part of a profile.
** Registering does not change runs by itself: name the profile in
** hegel_run_options.profile, or make it the default.  Returns 0, or 1
** with a message on stderr (bad name, bad settings).  Call it before the
** runs that use it (in main). */
int hegel_register_profile (const char * name, const hegel_run_options * opts);

/* Make `name` the default profile for every later run in the process,
** over HEGEL_DEFAULT_PROFILE, hegel.toml's `default =` and CI detection.
** The name need not exist yet (it must when a run starts).  NULL or ""
** clears an earlier call.  Returns 0, or 1 with a message on stderr. */
int hegel_set_default_profile (const char * name);

/* Like hegel_run_test_result / the nofork runner, with options (NULL =
** defaults).  Return 0 if the property held, 1 if not. */
int hegel_run_test_with        (void (*test_fn)(hegel_testcase *),
                                const hegel_run_options * opts);
int hegel_run_test_nofork_with (void (*test_fn)(hegel_testcase *),
                                const hegel_run_options * opts);

/* ---- Replaying a failure ----
**
** Every failure report ends with a line like
**
**   reproduce: HEGEL_C_REPRODUCE='2:AAEAAAAACgEAAABl'
**
** Run the same binary with that variable set and run 2 (the second
** property run the binary starts) replays exactly that failing case,
** shrunk, with its notes; the binary's other runs are skipped.  A blob
** that no longer fails reports "did not reproduce" and passes — which is
** how to check a fix.  A bare blob (no "N:") targets run 1. */

/* ---- Primitive draw functions ---- */

/* Draw random values within [min_val, max_val] */
int      hegel_draw_int    (hegel_testcase * tc, int min_val, int max_val);
int64_t  hegel_draw_i64    (hegel_testcase * tc, int64_t min_val, int64_t max_val);
uint64_t hegel_draw_u64    (hegel_testcase * tc, uint64_t min_val, uint64_t max_val);
size_t   hegel_draw_usize  (hegel_testcase * tc, size_t min_val, size_t max_val);
float    hegel_draw_float  (hegel_testcase * tc, float min_val, float max_val);
double   hegel_draw_double (hegel_testcase * tc, double min_val, double max_val);
bool     hegel_draw_bool   (hegel_testcase * tc);

/* Draw a random text string with length in [min_size, max_size].
** Writes a null-terminated string to buf.  Returns the string length
** (not counting null terminator), or 0 if capacity < 1. */
int hegel_draw_text  (hegel_testcase * tc, int min_size, int max_size,
                      char * buf, int capacity);

/* Draw an array of arbitrary bytes (each uniform in [0, 255]) of
** length in [min_size, max_size].  Writes raw bytes to buf — NOT
** null-terminated, treats buf as binary.  Returns the actual length
** written (capped at capacity).
**
** Unlike a per-byte `for` loop of `hegel_draw_int(tc, 0, 255)`, the
** whole array is drawn as a single logical span — the shrinker can
** drop the entire array as a unit, and the byte stream's signal
** isn't fragmented across N anonymous draws.  Use this for
** binary-safe byte buffers (test inputs that may include 0x00). */
int hegel_draw_bytes (hegel_testcase * tc, int min_size, int max_size,
                      uint8_t * buf, int capacity);

/* Draw a string the whole of which matches a regex pattern (Python `re`
** syntax, full match; never contains NUL).  Same return semantics as
** hegel_draw_text. */
int hegel_draw_regex (hegel_testcase * tc, const char * pattern,
                      char * buf, int capacity);

/* ---- Draw shorthands ----
**
** The primitives above, for a test case named `tc` in scope, with the
** range as arguments or none for the type's full range:
**
**     int     x = HEGEL_DRAW_INT    (0, 10);
**     int     y = HEGEL_DRAW_INT    ();           /. INT_MIN..INT_MAX ./
**     int64_t a = HEGEL_DRAW_I64    (-100, 100);
**     double  d = HEGEL_DRAW_DOUBLE (0.0, 1.0);
**     bool    b = HEGEL_DRAW_BOOL   ();           /. 0 or 1 ./
**
** Same choices as the call they expand to, so they shrink the same. */

/* Arg-count dispatch: HEGEL__DISPATCH (NAME, a, b) calls NAME_2 (a, b),
** HEGEL__DISPATCH (NAME) calls NAME_0 ().  Uses __VA_OPT__. */
#define HEGEL__NARG(...) \
  HEGEL__NARG_(_0 __VA_OPT__(,) __VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define HEGEL__NARG_(_0, _1, _2, _3, _4, _5, N, ...) N
#define HEGEL__GLUE(a, b)  HEGEL__GLUE_(a, b)
#define HEGEL__GLUE_(a, b) a##b
#define HEGEL__DISPATCH(name, ...) \
  HEGEL__GLUE(name##_, HEGEL__NARG(__VA_ARGS__))(__VA_ARGS__)

#define HEGEL__DRAW_INT_0()       hegel_draw_int    ((tc), INT_MIN,    INT_MAX)
#define HEGEL__DRAW_INT_2(lo, hi) hegel_draw_int    ((tc), (lo),       (hi))
#define HEGEL_DRAW_INT(...)       HEGEL__DISPATCH (HEGEL__DRAW_INT,    __VA_ARGS__)

#define HEGEL__DRAW_I64_0()       hegel_draw_i64    ((tc), INT64_MIN,  INT64_MAX)
#define HEGEL__DRAW_I64_2(lo, hi) hegel_draw_i64    ((tc), (lo),       (hi))
#define HEGEL_DRAW_I64(...)       HEGEL__DISPATCH (HEGEL__DRAW_I64,    __VA_ARGS__)

#define HEGEL__DRAW_U64_0()       hegel_draw_u64    ((tc), 0,          UINT64_MAX)
#define HEGEL__DRAW_U64_2(lo, hi) hegel_draw_u64    ((tc), (lo),       (hi))
#define HEGEL_DRAW_U64(...)       HEGEL__DISPATCH (HEGEL__DRAW_U64,    __VA_ARGS__)

#define HEGEL__DRAW_FLOAT_0()       hegel_draw_float  ((tc), -FLT_MAX, FLT_MAX)
#define HEGEL__DRAW_FLOAT_2(lo, hi) hegel_draw_float  ((tc), (lo),     (hi))
#define HEGEL_DRAW_FLOAT(...)       HEGEL__DISPATCH (HEGEL__DRAW_FLOAT, __VA_ARGS__)

#define HEGEL__DRAW_DOUBLE_0()       hegel_draw_double ((tc), -DBL_MAX, DBL_MAX)
#define HEGEL__DRAW_DOUBLE_2(lo, hi) hegel_draw_double ((tc), (lo),     (hi))
#define HEGEL_DRAW_DOUBLE(...)       HEGEL__DISPATCH (HEGEL__DRAW_DOUBLE, __VA_ARGS__)

/* Bool takes no range.  Draws hegel_draw_int (0, 1), so it shrinks to
** false like any 0/1 draw. */
#define HEGEL__DRAW_BOOL_0() ((bool)(hegel_draw_int ((tc), 0, 1)))
#define HEGEL_DRAW_BOOL(...) HEGEL__DISPATCH (HEGEL__DRAW_BOOL, __VA_ARGS__)

/* ---- Value draws: dates, times, UUIDs, IP addresses, emails, URLs, domains ----
**
** The engine's own generators for common value types: each is ONE draw
** the engine knows the shape of, so it shrinks toward a simple value of
** that type (a date toward 2000-01-01, a time toward midnight, ...)
** instead of toward whatever a hand-rolled generator's parts shrink to.
** Same fork / nofork behaviour as every other draw. */

/* A proleptic Gregorian calendar date: month 1..12, day 1..days-in-month. */
typedef struct {
  int32_t  year;
  uint8_t  month;
  uint8_t  day;
} hegel_date;

/* A time of day: hour 0..23, minute and second 0..59 (no leap second),
** nanosecond 0..999999999. */
typedef struct {
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
  uint32_t nanosecond;
} hegel_time;

/* A naive datetime (no timezone). */
typedef struct {
  hegel_date date;
  hegel_time time;
} hegel_datetime;

/* 16 bytes, big-endian (bytes[0] is the first hex pair of the canonical
** form; the version is bytes[6] >> 4, the variant the top bits of bytes[8]). */
typedef struct { uint8_t bytes[16]; } hegel_uuid;
/* Network byte order: 192.168.0.1 is { 192, 168, 0, 1 }. */
typedef struct { uint8_t bytes[4]; }  hegel_ipv4;
typedef struct { uint8_t bytes[16]; } hegel_ipv6;

/* Draw a date in [*min, *max] (inclusive).  NULL bounds are the engine
** defaults: min 0001-01-01, max 9999-12-31; given bounds may use years
** -999999..999999.  Shrinks toward 2000-01-01, or the nearest bound when
** that is out of range.  Bounds that are not real dates, or min > max,
** abort the run as a health failure. */
hegel_date hegel_draw_date (hegel_testcase * tc, const hegel_date * min,
                            const hegel_date * max);

/* Draw a time of day in [*min, *max].  NULL bounds: 00:00:00.000000000
** and 23:59:59.999999999.  Shrinks toward min. */
hegel_time hegel_draw_time (hegel_testcase * tc, const hegel_time * min,
                            const hegel_time * max);

/* Draw a datetime in [*min, *max].  NULL bounds: 0001-01-01T00:00:00 and
** 9999-12-31T23:59:59.999999999.  Shrinks toward 2000-01-01T00:00:00, or
** the nearest bound. */
hegel_datetime hegel_draw_datetime (hegel_testcase * tc, const hegel_datetime * min,
                                    const hegel_datetime * max);

/* Draw a UUID.  version 0: any 128 bits except the nil UUID.  version
** 1..15: the RFC 4122 version nibble is forced to it and the variant to
** RFC 4122 (bytes[8] is 10xxxxxx); conventionally 1..5.  Anything else
** is a health failure. */
hegel_uuid hegel_draw_uuid (hegel_testcase * tc, int version);

/* Draw an IPv4 / IPv6 address (any address, special ranges included). */
hegel_ipv4 hegel_draw_ipv4 (hegel_testcase * tc);
hegel_ipv6 hegel_draw_ipv6 (hegel_testcase * tc);

/* Draw an RFC 5321/5322 email address (`local@domain`), an RFC 3986
** http/https URL, or a fully-qualified domain name of at most max_length
** characters (4..255; 0 means 255).  Same return semantics as
** hegel_draw_text: a NUL-terminated string in buf, truncated to
** capacity - 1 bytes, the length written returned (0 if capacity < 1).
** All three are ASCII and never contain NUL.  Truncation cuts a valid
** value into an invalid one, so size buf for the longest value: an email
** is at most 254 characters, a domain max_length; a URL has no bound
** (its path has as many components as the engine draws, each up to ~300
** characters percent-encoded) — 4096 bytes covers what it generates in
** practice, and a return of capacity - 1 means the URL may be cut. */
int hegel_draw_email  (hegel_testcase * tc, char * buf, int capacity);
int hegel_draw_url    (hegel_testcase * tc, char * buf, int capacity);
int hegel_draw_domain (hegel_testcase * tc, int max_length, char * buf, int capacity);

/* ---- Test suite ---- */

/* A test suite runs multiple tests in one binary; each test is its own
** engine run.
**
** Example:
**   hegel_suite * s = hegel_suite_new ();
**   hegel_suite_add (s, "test_foo", test_foo);
**   hegel_suite_add (s, "test_bar", test_bar);
**   int rc = hegel_suite_run (s);
**   hegel_suite_free (s);
**   return rc;
*/
typedef struct HegelSuite hegel_suite;

hegel_suite * hegel_suite_new  (void);
void          hegel_suite_add  (hegel_suite * suite, const char * name,
                                void (*test_fn)(hegel_testcase *));
int           hegel_suite_run  (hegel_suite * suite);
void          hegel_suite_free (hegel_suite * suite);

/* ---- Stateful PBT (command-sequence testing) ---- */

/* Stateful tests model the system under test as a state machine.  The
** user supplies:
**   - setup() / teardown(): build and tear down a fresh state per case
**   - rules: actions that can be applied to the state machine.  Each
**     rule may call hegel_draw_int / etc. to draw operands.
**   - invariants: assertions checked after every successful rule
**     application.  Use HEGEL_ASSERT inside an invariant to fail.
**
** The engine's state machine picks the rules (swarm testing included)
** and applies up to ~50 of them per case.  Sequence shrinking (dropping
** rules, simplifying operands) is what makes this kind of test catch
** interesting state-machine bugs that single-shot property tests miss.
** A rule that calls hegel_assume(false), or whose precondition is false,
** is rejected: the step doesn't count and another rule is picked.
**
** Limits: up to 64 rules and 64 invariants per state machine.
**
** Example: integer stack
**   typedef struct { int data[100]; int len; } stk_t;
**   void *setup    (void)        { return calloc (1, sizeof (stk_t)); }
**   void  teardown (void *s)     { free (s); }
**   void  rule_push (void *s, hegel_testcase *tc) {
**     stk_t *st = s;
**     if (st->len < 100) st->data[st->len++] = hegel_draw_int (tc, 0, 99);
**   }
**   void  rule_pop  (void *s, hegel_testcase *tc) {
**     (void) tc; stk_t *st = s; if (st->len > 0) st->len--;
**   }
**   void  inv_len_in_range (const void *s, hegel_testcase *tc) {
**     (void) tc; const stk_t *st = s;
**     HEGEL_ASSERT (st->len >= 0 && st->len <= 100, "len OOB: %d", st->len);
**   }
**
**   hegel_state_machine *sm = hegel_state_machine_new (setup, teardown);
**   hegel_state_machine_add_rule      (sm, "push", rule_push);
**   hegel_state_machine_add_rule      (sm, "pop",  rule_pop);
**   hegel_state_machine_add_invariant (sm, "len",  inv_len_in_range);
**   hegel_state_machine_run_n         (sm, 200);
**   hegel_state_machine_free          (sm);
*/
typedef struct CStateMachine hegel_state_machine;

hegel_state_machine * hegel_state_machine_new (
    void * (*setup)    (void),
    void   (*teardown) (void * state));

void hegel_state_machine_free (hegel_state_machine * sm);

/* Returns 0 on success, -1 if the rule/invariant table is full (64 slots). */
int hegel_state_machine_add_rule (
    hegel_state_machine * sm, const char * name,
    void (*rule_fn) (void * state, hegel_testcase * tc));

/* Add a rule with a precondition.  When the engine picks a rule whose
** precondition is false, the rule is not run and the pick is rejected
** (it doesn't count as a step, and isn't printed in the
** counterexample).  Use this when a rule's applicability is
** state-dependent (e.g. close_seq only when a sequence is open; pop only
** when stack non-empty).  The precondition MUST NOT mutate the state and
** MUST NOT call any hegel_draw_* function — it's a pure observation. */
int hegel_state_machine_add_rule_with_precondition (
    hegel_state_machine * sm, const char * name,
    bool (*precondition_fn) (const void * state),
    void (*rule_fn)         (void * state, hegel_testcase * tc));

int hegel_state_machine_add_invariant (
    hegel_state_machine * sm, const char * name,
    void (*invariant_fn) (const void * state, hegel_testcase * tc));

/* Run the state machine for n_cases, each case in a forked child (a
** crash in a rule body is a shrinkable failure); exit(1) on failure. */
void hegel_state_machine_run_n (hegel_state_machine * sm, uint64_t n_cases);

/* Same without per-case fork isolation (see hegel_run_test_nofork). */
void hegel_state_machine_run_nofork_n (hegel_state_machine * sm, uint64_t n_cases);

/* ---- Debug output ---- */

/* Attach a line to the counterexample report: printed with the minimal
** failing test case only (the engine stamps the executions whose output
** can end up in the report; everything else is dropped) — use this to
** annotate the minimal counterexample with computed values.
** Example:
**   char buf[128];
**   snprintf(buf, sizeof(buf), "computed hash = %u", hash);
**   hegel_note(tc, buf);
*/
void hegel_note (hegel_testcase * tc, const char * msg);

/* ---- Spans ----
**
** Spans group draws together so the shrinker can treat them as one
** structural unit instead of as independent bytes.  Wrap the draws
** that produce a single logical thing — a list element, a struct, a
** oneof variant — between hegel_start_span and hegel_stop_span.
**
** Without spans, the shrinker can only delete or shrink individual
** bytes; with spans, it can drop a whole list element, swap two
** sibling subtrees, or minimize one variant without touching the others.
** Spans nest; always pair start/stop.
**
** Example (manual list of structs):
**   hegel_start_span(tc, HEGEL_SPAN_LIST);
**   int n = hegel_draw_int(tc, 0, 10);
**   for (int i = 0; i < n; i++) {
**     hegel_start_span(tc, HEGEL_SPAN_LIST_ELEMENT);
**     int x = hegel_draw_int(tc, 0, 100);
**     int y = hegel_draw_int(tc, 0, 100);
**     // ... use x, y ...
**     hegel_stop_span(tc, 0);
**   }
**   hegel_stop_span(tc, 0);
**
** `label` identifies the generator that opened the span: the engine
** treats spans with the same label as candidates for swapping and
** duplicating, and does nothing else with them.  Derive labels from
** names with hegel_label("mylib.thing") so differently-shaped
** generators get different labels; the HEGEL_SPAN_* constants are
** small legacy identities kept for existing callers. */

#define HEGEL_SPAN_LIST          1
#define HEGEL_SPAN_LIST_ELEMENT  2
#define HEGEL_SPAN_SET           3
#define HEGEL_SPAN_SET_ELEMENT   4
#define HEGEL_SPAN_MAP           5
#define HEGEL_SPAN_MAP_ENTRY     6
#define HEGEL_SPAN_TUPLE         7
#define HEGEL_SPAN_ONE_OF        8
#define HEGEL_SPAN_OPTIONAL      9
#define HEGEL_SPAN_FIXED_DICT   10
#define HEGEL_SPAN_FLAT_MAP     11
#define HEGEL_SPAN_FILTER       12
#define HEGEL_SPAN_MAPPED       13
#define HEGEL_SPAN_SAMPLED_FROM 14
#define HEGEL_SPAN_ENUM_VARIANT 15
#define HEGEL_SPAN_USER       1024  /* user labels start here */

/* The span label for a generator name: 64-bit FNV-1a of the name's
** bytes, as libhegel's hegel_label_from_name.  Pure — callable anywhere,
** including at file scope via a static initializer function. */
uint64_t hegel_label (const char * name);

/* The span label for a generator built from others: 64-bit FNV-1a over
** the labels' little-endian bytes, in order, as libhegel's
** hegel_label_combine.  Pass the generator's own label first and its
** components' after it, so a list of ints and a list of strings get
** different labels:
**
**   uint64_t parts[] = { hegel_label ("mylib.list"), elem_label };
**   uint64_t label = hegel_label_combine (parts, 2);
**
** Order-sensitive; combining one label does not return it unchanged;
** n = 0 gives the FNV offset basis (labels may be NULL then).  Pure, like
** hegel_label — no engine, no test case. */
uint64_t hegel_label_combine (const uint64_t * labels, size_t n);

void hegel_start_span (hegel_testcase * tc, uint64_t label);

/* End the current span.  Pass 0 for `discard` in normal use.
** Pass non-zero only when the span turned out to be a dead end
** (e.g. filtered out) so the shrinker can ignore its bytes. */
void hegel_stop_span  (hegel_testcase * tc, int discard);

/* ---- Collections ----
**
** For variable-length values the ENGINE decides how many elements to
** produce: loop on hegel_collection_more, drawing one element per true.
** Shrinking then deletes elements directly — prefer this over drawing a
** length and looping.  hegel_collection_reject tells the engine the last
** element was invalid (e.g. a duplicate) so it can draw another.
**
**   hegel_collection c = hegel_collection_new (tc, 0, 10);
**   while (hegel_collection_more (tc, c)) items[n++] = hegel_draw_int (tc, 0, 99);
**   hegel_collection_free (tc, c);
**
** Handles are freed automatically at the end of the test case if the
** body escaped (failure, assume) before freeing them. */

typedef struct { int64_t id; } hegel_collection;

hegel_collection hegel_collection_new    (hegel_testcase * tc, uint64_t min_size,
                                          uint64_t max_size);  /* UINT64_MAX = unbounded */
bool             hegel_collection_more   (hegel_testcase * tc, hegel_collection c);
void             hegel_collection_reject (hegel_testcase * tc, hegel_collection c);
void             hegel_collection_free   (hegel_testcase * tc, hegel_collection c);

/* ---- Repeat loops ----
**
** Run a body as many times as the ENGINE wants — each run an iteration —
** so a failing loop shrinks to the fewest iterations, as a collection
** shrinks to the fewest elements.  For "call this API again and again"
** properties: a stateful object under test, a sequence of operations.
**
**   Counter * k = counter_new ();            // state the iterations share
**   HEGEL_REPEAT (tc) {
**     int n = hegel_draw_int (tc, 0, 10);
**     counter_add (k, n);
**     HEGEL_ASSERT (counter_ok (k), "counter broke after adding %d", n);
**   }
**   counter_free (k);                        // runs once the loop is over
**
** A failure inside an iteration (HEGEL_ASSERT, hegel_fail, a crash in fork
** mode or inside HEGEL_ISOLATED) ends the loop and the case; the runtime
** tells the engine that the iteration failed (hegel_repeat_failed) for
** you, whatever the transport, so the shrinker can cut the iterations
** before it.  The loop starts where hegel_repeat_new / HEGEL_REPEAT is
** reached in the draw sequence; the engine may run the body zero times.
** When the case is replayed for the report, each iteration's output is
** headed by a "Repetition #N" note.
**
** Cost: the loop is "logically infinite", as in every Hegel frontend — a
** passing case typically runs hundreds to tens of thousands of
** iterations, and many run until the case's choice budget (2^20 choices)
** ends it as an overrun.  Every draw is an engine call; under the fork
** runners each one is a pipe round trip, so a passing loop can take
** seconds per case there and trip the engine's too_slow health check.
** Run repeat properties under hegel_run_test_nofork* (HEGEL_ISOLATED
** around crash-prone code) or on few test cases; or stop a raw loop
** yourself (below).
**
** Two forms:
**   - HEGEL_REPEAT (tc) { ... } — a loop statement.  hegel_assume (or an
**     engine-side rejection) in the body discards the WHOLE case.  Don't
**     leave the body with break / return / goto: the loop's handle stays
**     open until the case ends, and a later failure would be blamed on
**     the iteration you left (continue is fine: it ends the iteration).
**   - hegel_repeat_run (tc, body, ctx) — the body as a function, called
**     once per iteration.  A hegel_assume in the body ends only THAT
**     iteration and the loop goes on, the semantics of hegel-rust's
**     tc.repeat.  The body may return early (that ends the iteration).
**     hegel_alloc blocks made in a rejected iteration last until the case
**     ends.
**
** The raw protocol, for loops the forms above don't fit:
**
**   hegel_repeat r = hegel_repeat_new (tc);
**   while (hegel_repeat_more (tc, r)) { ...one iteration... }
**   hegel_repeat_free (tc, r);
**
** hegel_repeat_more decides whether another iteration runs; after false
** the body must not run again.  To stop earlier on your own terms (an
** iteration cap), just leave the loop and hegel_repeat_free the handle —
** freeing also tells the runtime no iteration is open any more.  hegel_repeat_failed tells the engine the
** iteration just run failed and ends the loop; the runtime calls it
** itself when a case fails mid-iteration, so call it only if an iteration
** fails in a way the runtime doesn't see — and fail the case right after.
** Handles are freed automatically at the end of the test case if the
** body escaped before freeing them.  Loops nest (a failure is then
** reported to every open loop, innermost first). */

typedef struct { int64_t id; } hegel_repeat;

hegel_repeat hegel_repeat_new    (hegel_testcase * tc);
bool         hegel_repeat_more   (hegel_testcase * tc, hegel_repeat r);
void         hegel_repeat_failed (hegel_testcase * tc, hegel_repeat r);
void         hegel_repeat_free   (hegel_testcase * tc, hegel_repeat r);

typedef void (*hegel_repeat_fn) (hegel_testcase * tc, void * ctx);

void hegel_repeat_run (hegel_testcase * tc, hegel_repeat_fn body, void * ctx);

/* HEGEL_REPEAT's loop state: the handle and the iteration number. */
typedef struct { hegel_repeat r; uint64_t n; } hegel__repeat_loop;

hegel__repeat_loop hegel__repeat_begin (hegel_testcase * tc);
bool               hegel__repeat_step  (hegel_testcase * tc, hegel__repeat_loop * l);

#define HEGEL_REPEAT(tc) \
  for (hegel__repeat_loop hegel__repeat_ = hegel__repeat_begin (tc); \
       hegel__repeat_step ((tc), &hegel__repeat_); )

/* ---- Recursion ----
**
** One recursive value (a tree, an expression) per hegel_recursion_new;
** the engine owns branch probabilities, depth and leaf budgets, so
** recursive values shrink and distribute like every other Hegel
** frontend's.  Protocol, starting with the root at depth 0:
**   - hegel_recursion_branch(depth): true -> build a branch, drawing its
**     children at depth+1 with this same protocol; false -> a leaf:
**   - hegel_recursion_leaf: 0 -> draw the leaf; 1 -> the attempt
**     outgrew its budget: unwind without drawing anything more, call
**     hegel_recursion_retry (which discards the case once attempts are
**     exhausted), and start again from the root.
**   - hegel_recursion_finish once the root is complete: 0 -> keep the
**     value; 1 -> the engine discarded it: start again from the root
**     (WITHOUT calling retry).
** hegel_draw_recursive (below) runs this loop for you; use the raw calls
** only when you need control it doesn't give. */

typedef struct { int64_t id; } hegel_recursion;

hegel_recursion hegel_recursion_new    (hegel_testcase * tc, uint64_t max_depth,
                                        uint64_t max_leaves);
bool            hegel_recursion_branch (hegel_testcase * tc, hegel_recursion r, uint64_t depth);
int             hegel_recursion_leaf   (hegel_testcase * tc, hegel_recursion r);
void            hegel_recursion_retry  (hegel_testcase * tc, hegel_recursion r);
int             hegel_recursion_finish (hegel_testcase * tc, hegel_recursion r);
void            hegel_recursion_free   (hegel_testcase * tc, hegel_recursion r);

/* ---- Per-case memory: hegel_alloc ----
**
** Memory owned by the test case: freed when the case ends, however it
** ends — return, HEGEL_ASSERT / hegel_fail, hegel_assume, an engine
** overrun, a health failure — so a generator needs no cleanup chain:
**
**   Graph * g   = hegel_alloc (tc, sizeof *g);
**   g->nvert    = hegel_draw_int (tc, 1, 50);
**   g->edges    = hegel_alloc (tc, (size_t) g->nvert * sizeof *g->edges);
**   ...                                   // no free: the case owns them
**
** hegel_alloc returns zeroed memory (calloc-like: no garbage that could
** change from case to case and make a failure flaky), aligned for any
** type.  hegel_realloc grows or shrinks a block (NULL = hegel_alloc;
** bytes past the old size are zeroed); hegel_free releases one early
** (NULL is a no-op).  Out of memory, or a pointer hegel_alloc did not
** return, is a health failure.
**
** Where the memory lives: in the process running the test body — the
** forked child in fork mode (it never travels to the parent), the
** runner in nofork mode, where a jump out of the case would otherwise
** leak it.  In a stateful test the blocks a rule makes last until the
** end of the case (a rule that assumes keeps them: it may have hung them
** on the state); teardown runs before they are freed.  A HEGEL_ISOLATED
** block may read earlier blocks and allocate its own; under the nofork
** runner it is a forked child, which sees the earlier blocks
** copy-on-write and whose own blocks vanish with it, while under the
** fork runners it runs inline and its blocks are the case's.  So don't
** hegel_free an earlier block inside one: it would be gone in one mode
** and not the other.  A recursive generator's abandoned attempts are
** freed as they are abandoned (see hegel_draw_recursive, below).
**
** Don't hand a block to code that will free() it, and don't use one
** after the test function returns. */

void * hegel_alloc   (hegel_testcase * tc, size_t size);
void * hegel_realloc (hegel_testcase * tc, void * p, size_t size);
void   hegel_free    (hegel_testcase * tc, void * p);

/* ---- Recursive values: hegel_draw_recursive ----
**
** The protocol above, run for you.  Write one node function that builds
** ONE node; where the node has a child, it calls hegel_recursive_child,
** which runs the protocol for that sub-value (calling the node function
** again) and returns what it built:
**
**   typedef struct Expr { int op; int val; struct Expr * a, * b; } Expr;
**
**   static void * gen_expr (hegel_testcase * tc, bool leaf, void * ctx)
**   {
**     Expr * e = hegel_alloc (tc, sizeof *e);       // case-owned, zeroed
**     if (leaf) {                                   // no children here
**       e->val = hegel_draw_int (tc, 0, 9);
**       return e;
**     }
**     e->op = hegel_draw_int (tc, 1, 2);            // '+' or '*'
**     e->a = hegel_recursive_child (tc);
**     e->b = hegel_recursive_child (tc);
**     return e;
**   }
**
**   Expr * root = hegel_draw_recursive (tc, HEGEL_RECURSIVE_MAX_DEPTH,
**                                       HEGEL_RECURSIVE_MAX_LEAVES, gen_expr, NULL);
**
** The engine decides, for the root and for every child, whether it is a
** branch (leaf = false: the node may call hegel_recursive_child, any
** number of times — a fixed arity, a drawn one, per optional slot) or a
** leaf (leaf = true: build it without children; returning NULL is fine
** when "leaf" means "absent").  It owns the branch probabilities, the
** depth cap and the leaf budget, as in every Hegel frontend.  When an
** attempt outgrows the leaf budget, hegel_recursive_child does not
** return: it jumps back into hegel_draw_recursive, which frees every
** hegel_alloc block made since the attempt began, has the engine close
** the spans left open, and builds the value again from the root (the
** engine discards the case once attempts run out).  Each sub-value is
** wrapped in its own span, so the shrinker can drop or swap subtrees.
**
** Rules (the price of the unwind):
**   - allocate nodes with hegel_alloc, never malloc: an unwound attempt
**     frees its hegel_alloc blocks; anything else leaks.  The value is
**     the test case's — don't free it (hegel_free is allowed);
**   - across a hegel_recursive_child call, hold nothing the unwind would
**     leak or leave half-done (a malloc'd buffer, a lock, a file, a
**     global you restore afterwards);
**   - call hegel_recursive_child only from inside a node function, and
**     not when leaf is true (both are health failures);
**   - a draw in a node function that discards the case (hegel_assume)
**     discards the whole case, not just the node.
** A node function may run a hegel_draw_recursive of its own (a tree of
** trees); hegel_recursive_child always means the innermost one.
** Defaults: 100 leaves is hegel-rust's recursive-generator budget, 50
** levels the retired schema layer's HEGEL_SELF cap; with them a binary
** tree averages ~80 nodes (tests/selftest/test_recursive_tree.c). */

#define HEGEL_RECURSIVE_MAX_DEPTH   50
#define HEGEL_RECURSIVE_MAX_LEAVES 100

typedef void * (*hegel_recursive_fn) (hegel_testcase * tc, bool leaf, void * ctx);

void * hegel_draw_recursive  (hegel_testcase * tc, uint64_t max_depth, uint64_t max_leaves,
                              hegel_recursive_fn node, void * ctx);
void * hegel_recursive_child (hegel_testcase * tc);

/* ---- Variable pools ----
**
** A pool tracks ids of values the test generated earlier (typically in
** stateful tests: "act on some existing handle").  hegel_pool_add
** returns a fresh id to associate with the value just made;
** hegel_pool_draw lets the engine pick (and shrink) which earlier id to
** reuse — consume=true removes it.  Drawing from an empty pool discards
** the case.  Ids are stable while shrinking. */

typedef struct { int64_t id; } hegel_pool;

hegel_pool hegel_pool_new     (hegel_testcase * tc);
int64_t    hegel_pool_add     (hegel_testcase * tc, hegel_pool p);
int64_t    hegel_pool_draw    (hegel_testcase * tc, hegel_pool p, bool consume);
void       hegel_pool_restore (hegel_testcase * tc, hegel_pool p, int64_t var);
void       hegel_pool_free    (hegel_testcase * tc, hegel_pool p);

/* ---- Grammars: strings from a Lark grammar ----
**
** Draw a string the grammar accepts.  The grammar is in the syntax of
** the Lark parsing library (https://lark-parser.readthedocs.io/en/stable/
** grammar.html): rules, terminals (strings, regexes, priorities),
** %import (Lark's bundled common / python / unicode / lark grammars
** included), %ignore, %declare, %override, %extend, templates.
**
**   static const char * EXPR =
**     "?start: sum\n"
**     "?sum: product | sum \"+\" product\n"
**     "?product: atom | product \"*\" atom\n"
**     "?atom: NUMBER | \"(\" sum \")\"\n"
**     "%import common.NUMBER\n";
**
**   char * s = hegel_draw_grammar (tc, EXPR, NULL);    // NULL = rule "start"
**
** The string is one choice of the test case and shrinks through its
** derivation (to short strings of the grammar).  It is a NUL-terminated
** C string with no NUL inside (terminals never draw one), allocated
** with hegel_alloc: the test case owns it and frees it when the case
** ends (don't free() it; hegel_free is allowed).  It is never truncated,
** which a caller buffer could not promise: a cut-off string would not be
** in the language.  Up to 8192 characters by default (UTF-8, so maybe
** more bytes); set max_size below to keep them short.
**
** The grammar is checked and compiled on its first draw in a run, and
** reused for every later case (in fork mode, by the parent, which owns
** the engine).  A grammar the engine rejects (bad syntax, an undefined
** rule, a rule with no finite string, a cap below the smallest string)
** is a health failure carrying the engine's message.  A draw the engine
** can't complete within its work limit discards the test case.
**
** hegel_draw_grammar_with takes options (zero fields = defaults):
**   start       the rule to generate (NULL = "start");
**   max_size    most characters per string, explicit values included
**               (0 = 8192; at most 1048576);
**   max_depth   how deeply rules may nest (0 = 200; at most 500);
**   import_dir  the directory %import reads from: `%import a.b.X` reads
**               <import_dir>/a/b.lark, Lark's bundled grammars stand in
**               for missing files (NULL = bundled grammars only).  Read
**               by the process that owns the engine — the fork parent,
**               same cwd — once per run;
**   explicit_names / n_explicit, fill / fill_ctx: terminals whose values
**               the TEST draws.  The engine draws the string; for each
**               occurrence of an explicit terminal (in order) it calls
**               fill (tc, i, fill_ctx), i indexing explicit_names, which
**               draws the value with ordinary hegel draws and returns it;
**               hegel-c copies the value right away and splices it in.
**               Every terminal the grammar %declares must be named; a
**               defined terminal may be named to override its pattern.
**               The value must lex as that terminal where it lands (the
**               engine doesn't check it), must be valid UTF-8, and its
**               pointer must survive the callback's return (a static or
**               fill_ctx buffer, or a hegel_alloc block).  If the values
**               push the string over max_size, the engine draws a new
**               string and asks again from its first terminal (so fill
**               may run more times than the result has terminals); after
**               a few such attempts it discards the case.  Each value
**               gets its own span.  Explicit names with no fill, or a fill
**               returning NULL, is a health failure.
**
**   static const char * pick_name (hegel_testcase * tc, size_t i, void * ctx)
**   {
**     static const char * names[] = { "x", "y", "zz" };
**     (void) i; (void) ctx;
**     return names[hegel_draw_int (tc, 0, 2)];
**   }
**   static const char * const vars[] = { "VAR" };
**   hegel_grammar_options o = { .explicit_names = vars, .n_explicit = 1,
**                               .fill = pick_name };
**   char * s = hegel_draw_grammar_with (tc, "start: VAR (\"+\" VAR)*\n"
**                                           "%declare VAR\n", &o);
**
** No grammar draws inside HEGEL_ISOLATED (they are engine calls). */

typedef const char * (*hegel_grammar_fill_fn) (hegel_testcase * tc, size_t terminal,
                                               void * ctx);

typedef struct {
  const char *          start;
  uint64_t              max_size;
  uint32_t              max_depth;
  const char *          import_dir;
  const char * const *  explicit_names;
  size_t                n_explicit;
  hegel_grammar_fill_fn fill;
  void *                fill_ctx;
} hegel_grammar_options;

char * hegel_draw_grammar      (hegel_testcase * tc, const char * grammar, const char * start);
char * hegel_draw_grammar_with (hegel_testcase * tc, const char * grammar,
                                const hegel_grammar_options * opts);

/* ---- Crash isolation for a block: HEGEL_ISOLATED ----
**
** Generate first, then run the code under test in a block that may
** crash without taking the run down:
**
**   Graph * g = gen_graph (tc);              // draws: talk to the engine
**   HEGEL_ISOLATED (tc) {                    // no draws in here
**     int r = graph_build (g);
**     HEGEL_ASSERT (r == 0, "build failed: %d", r);
**   }
**
** Under hegel_run_test_nofork the block runs in a forked child that sees
** every value drawn before it (copy-on-write — nothing is serialized);
** a crash there is a failure whose origin names the block, and it
** shrinks like any other.  HEGEL_ASSERT / hegel_assume / hegel_note
** inside the block work as usual.  Under the fork runners the whole case
** is already isolated, so the block runs inline.
**
** The block is one bug: every failure inside it — crash, abort,
** HEGEL_ASSERT, hegel_fail — reports origin "HEGEL_ISOLATED at file:line"
** (the message still says which).  So code whose one bug shows up as a
** crash on some runs and a bad result on others (memory garbage decides)
** shrinks as one failure: put the call AND its check in the block.  Two
** asserts that witness different bugs belong in different blocks, or
** after the block.  HEGEL_C_MERGE_ORIGINS=1 in the environment does the
** same for a whole run, with no code change (and merges distinct bugs).
**
** Rules: no hegel_draw_* / span / collection calls inside the block (a
** health failure says so), no nesting, and don't leave the block with
** break / return / goto. */

#define HEGEL_ISOLATED(tc) \
  for (int hegel__iso_ = hegel__isolate_enter ((tc), __FILE__ ":" HEGEL__STR (__LINE__)); \
       hegel__iso_; hegel__iso_ = hegel__isolate_leave (tc))

int hegel__isolate_enter (hegel_testcase * tc, const char * site);
int hegel__isolate_leave (hegel_testcase * tc);

/* ---- Assertions and assumptions ---- */

/* If condition is 0, discard this test case (not a failure) */
void hegel_assume (hegel_testcase * tc, int condition);

/* Fail the test with a message — triggers hegel shrinking */
void hegel_fail (const char * msg);

/* Fail with an explicit ORIGIN — the stable identifier the shrinker
** uses to tell distinct bugs apart.  Two failures with the same origin
** are shrunk together; different origins are treated as different
** bugs.  Derive origin from the LOCATION of the failing check (the
** HEGEL_ASSERT macro passes file:line automatically), never from a
** message that embeds drawn values — that would make every draw look
** like a fresh bug and the shrinker would never converge. */
void hegel_fail_with_origin (const char * origin, const char * msg);

/* Record a numeric observation for targeted PBT: the engine biases
** later test cases toward inputs that scored HIGHER under the same
** label.  No-op unless the targeting phase is enabled (it is by
** default).  Label must be non-NULL, stable, and UTF-8.  The target
** phase starts late in a run: with the default 100 test cases it rarely
** gets going — ask for ~1000 (see tests/selftest/test_target.c). */
void hegel_target (hegel_testcase * tc, double value, const char * label);

/* ---- Events: statistics observations ----
**
** Record what a test case exercised, for the engine's end-of-run
** statistics report (like Hypothesis's event()):
**
**   if (n == 0) hegel_event (tc, "empty input");
**   hegel_event_value (tc, "input length", (double) n);
**
** hegel_event: the report shows, per label, the percentage of test cases
** in which the label was recorded at least once (repeats within one case
** count once).  hegel_event_value: the report shows, per label, count,
** min, median, mean, p90 and max of every value recorded (any number per
** case).  Only generation-phase cases that completed (passed or failed)
** are counted: discarded cases (hegel_assume) and shrink / replay cases
** are not.  Events never steer generation or shrinking — unlike
** hegel_target — and cost almost nothing.
**
** The engine prints the report on stderr at the end of each run (fork,
** nofork, suites, stateful), only with its show_statistics setting on:
** HEGEL_STATISTICS=1 in the environment.  Off, events report nothing.
** For example:
**
**   Statistics (over 100 test cases):
**     * empty input: 12.0% of test cases
**     * input length: count 100, min 0, median 4, mean 4.31, p90 8, max 10
**
** Label: non-NULL UTF-8.  Value: finite (NaN / infinity is a health
** failure).  Both are engine calls: fine in fork and nofork bodies and in
** stateful rules, a health failure inside a HEGEL_ISOLATED block. */
void hegel_event       (hegel_testcase * tc, const char * label);
void hegel_event_value (hegel_testcase * tc, const char * label, double value);
/* ---- end of events ---- */

/* Assert a condition — if false, fails with message and triggers shrinking */
void hegel_assert (int condition, const char * msg);

/* Emit a HEALTH CHECK failure — a different kind of failure from
** hegel_fail:
**
**   hegel_fail         → the CODE UNDER TEST is broken.  Shrink, show
**                        the user a minimal counterexample.
**   hegel_health_fail  → the TEST SETUP is broken (e.g. a fixture the
**                        test needs could not be built, or a generator
**                        can never produce a valid value).  The user needs to fix the
**                        test, not the code.  Shrinking shouldn't add
**                        signal; the problem is invariant across
**                        inputs.
**
** Emits a message prefixed with "Health check failure: " so downstream
** tooling (CI filters, the selftest Makefile's TESTS_HEALTH category)
** can distinguish health issues from real property failures.
**
** This is a true no-shrink signal: the run aborts immediately after
** the message — no shrinking cycles are spent on a failure that is
** invariant across inputs.  (Historical note: the old Rust-bridge
** implementation could only route this through the normal fail+shrink
** path; the pure-C core aborts the run directly.) */
void hegel_health_fail (const char * msg);

/*
** Convenience macro: HEGEL_ASSERT(cond, fmt, ...) formats a message and
** fails the test. Use this instead of assert() in hegel test functions.
** The failure's shrinker origin is the assert's file:line, so distinct
** asserts are tracked as distinct bugs even when the formatted message
** varies with the drawn values.
*/
#define HEGEL_ASSERT(cond, ...) \
  do { \
    if (!(cond)) { \
      char _hegel_buf[512]; \
      snprintf (_hegel_buf, sizeof (_hegel_buf), __VA_ARGS__); \
      hegel_fail_with_origin ("HEGEL_ASSERT at " __FILE__ ":" HEGEL__STR(__LINE__), \
                              _hegel_buf); \
    } \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* HEGEL_C_H */
