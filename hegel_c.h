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
} hegel_run_options;

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
**     (WITHOUT calling retry). */

typedef struct { int64_t id; } hegel_recursion;

hegel_recursion hegel_recursion_new    (hegel_testcase * tc, uint64_t max_depth,
                                        uint64_t max_leaves);
bool            hegel_recursion_branch (hegel_testcase * tc, hegel_recursion r, uint64_t depth);
int             hegel_recursion_leaf   (hegel_testcase * tc, hegel_recursion r);
void            hegel_recursion_retry  (hegel_testcase * tc, hegel_recursion r);
int             hegel_recursion_finish (hegel_testcase * tc, hegel_recursion r);
void            hegel_recursion_free   (hegel_testcase * tc, hegel_recursion r);

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
