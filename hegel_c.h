/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef HEGEL_C_H
#define HEGEL_C_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* ---- Test runners ---- */

/* Run a property test: test_fn is called once per generated test case.
** Each test case runs in a forked child process (crash-safe).
** On failure, prints the counterexample and calls exit(1). */
void hegel_run_test (void (*test_fn)(hegel_testcase *));
void hegel_run_test_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* Same semantics but returns 0 on success, 1 on failure instead of
** calling exit().  Allows running multiple tests in one binary while
** sharing a single Hegel server process. */
int hegel_run_test_result (void (*test_fn)(hegel_testcase *));
int hegel_run_test_result_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* Same as above but WITHOUT fork isolation.
** NOT RECOMMENDED: a crash kills the process with no shrinking.
** Provided for benchmarking fork overhead. */
void hegel_run_test_nofork (void (*test_fn)(hegel_testcase *));
void hegel_run_test_nofork_n (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* ---- Primitive draw functions ---- */

/* Draw random values within [min_val, max_val] */
int      hegel_draw_int    (hegel_testcase * tc, int min_val, int max_val);
int64_t  hegel_draw_i64    (hegel_testcase * tc, int64_t min_val, int64_t max_val);
uint64_t hegel_draw_u64    (hegel_testcase * tc, uint64_t min_val, uint64_t max_val);
size_t   hegel_draw_usize  (hegel_testcase * tc, size_t min_val, size_t max_val);
float    hegel_draw_float  (hegel_testcase * tc, float min_val, float max_val);
double   hegel_draw_double (hegel_testcase * tc, double min_val, double max_val);

/* Draw a random text string with length in [min_size, max_size].
** Writes a null-terminated string to buf.  Returns the string length
** (not counting null terminator), or 0 if capacity < 1. */
int hegel_draw_text  (hegel_testcase * tc, int min_size, int max_size,
                      char * buf, int capacity);

/* Draw a string matching a regex pattern.  Same return semantics as
** hegel_draw_text. */
int hegel_draw_regex (hegel_testcase * tc, const char * pattern,
                      char * buf, int capacity);

/* ---- Test suite ---- */

/* A test suite runs multiple tests in one binary, sharing a single
** Hegel server process.  This amortizes the ~1s server startup cost
** across all tests instead of paying it per-binary.
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

/* ---- Debug output ---- */

/* Print a message during the final replay of a failing test case only.
** Does nothing during generation and shrinking — use this to annotate
** the minimal counterexample with computed values.
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
** `label` identifies the kind of span.  Built-in labels mirror
** hegeltest's internal labels (LIST, ONE_OF, etc.) so the server
** treats them the same as native generators.  User code should use
** values >= HEGEL_SPAN_USER to avoid colliding with built-ins. */

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

void hegel_start_span (hegel_testcase * tc, uint64_t label);

/* End the current span.  Pass 0 for `discard` in normal use.
** Pass non-zero only when the span turned out to be a dead end
** (e.g. filtered out) so the shrinker can ignore its bytes. */
void hegel_stop_span  (hegel_testcase * tc, int discard);

/* ---- Assertions and assumptions ---- */

/* If condition is 0, discard this test case (not a failure) */
void hegel_assume (hegel_testcase * tc, int condition);

/* Fail the test with a message — triggers hegel shrinking */
void hegel_fail (const char * msg);

/* Assert a condition — if false, fails with message and triggers shrinking */
void hegel_assert (int condition, const char * msg);

/*
** Convenience macro: HEGEL_ASSERT(cond, fmt, ...) formats a message and
** calls hegel_fail. Use this instead of assert() in hegel test functions.
*/
#define HEGEL_ASSERT(cond, ...) \
  do { \
    if (!(cond)) { \
      char _hegel_buf[512]; \
      snprintf (_hegel_buf, sizeof (_hegel_buf), __VA_ARGS__); \
      hegel_fail (_hegel_buf); \
    } \
  } while (0)

/* ---- Composable generators ---- */

/*
** Generators are opaque objects that describe how to produce random values.
** Create them with hegel_gen_* factory functions, draw values with
** hegel_gen_draw_* functions, and free them with hegel_gen_free.
**
** Generators form a tree: combinators (hegel_gen_one_of, hegel_gen_optional)
** contain sub-generators.  Draw functions recursively evaluate the tree,
** calling hegel's primitive draws at each leaf — so generators work
** transparently in both fork and non-fork modes, and hegel can shrink
** each primitive draw independently.
**
** Ownership: combinators consume their sub-generator arguments.
** Do NOT free sub-generators after passing them to a combinator.
** Call hegel_gen_free on the root to free the entire tree.
**
** Example:
**
**   hegel_gen * small = hegel_gen_int (0, 10);
**   hegel_gen * large = hegel_gen_int (1000, 9999);
**   hegel_gen * gens[] = { small, large };
**   hegel_gen * bimodal = hegel_gen_one_of (gens, 2);
**   // small and large are now owned by bimodal — do NOT free them.
**
**   int val = hegel_gen_draw_int (tc, bimodal);
**   hegel_gen_free (bimodal);  // frees the whole tree
*/

/* Opaque generator handle */
typedef struct HegelGen hegel_gen;

/* -- Numeric generators -- */
hegel_gen * hegel_gen_int    (int min_val, int max_val);
hegel_gen * hegel_gen_i64    (int64_t min_val, int64_t max_val);
hegel_gen * hegel_gen_u64    (uint64_t min_val, uint64_t max_val);
hegel_gen * hegel_gen_float  (float min_val, float max_val);
hegel_gen * hegel_gen_double (double min_val, double max_val);
hegel_gen * hegel_gen_bool   (void);

/* -- String generators -- */

/* Generate random text with length in [min_size, max_size]. */
hegel_gen * hegel_gen_text  (int min_size, int max_size);

/* Generate strings matching a regex pattern.
** The pattern is copied — the caller may free the original. */
hegel_gen * hegel_gen_regex (const char * pattern);

/* -- Combinators -- */

/* Pick one sub-generator at random, draw from it.
** All sub-generators must produce the same type.
** Takes ownership of gens[0..count-1]. */
hegel_gen * hegel_gen_one_of (hegel_gen ** gens, int count);

/* Draw an index in [0, count).  The caller maps it to a value.
** Semantically equivalent to hegel_gen_int(0, count - 1). */
hegel_gen * hegel_gen_sampled_from (int count);

/* Optionally draw from inner.  Draws a bool first: if true, produces
** a value from inner; if false, produces nothing.
** Takes ownership of inner. */
hegel_gen * hegel_gen_optional (hegel_gen * inner);

/* -- map / filter / flat_map --
**
** Transform generators using C callbacks.  Each takes a source generator
** (ownership transferred), a function pointer, and a void* context passed
** to the callback on every draw.  The caller manages ctx lifetime — it
** must outlive the generator.
**
** map:      draw from source, transform value through callback.
** filter:   draw from source, keep if predicate returns non-zero.
**           Retries up to 3 times, then discards the test case.
** flat_map: draw from source, use value to create a new generator
**           via callback, draw from it, free it.  The callback must
**           return a fresh generator (via hegel_gen_* factories). */

/* map — transform values */
hegel_gen * hegel_gen_map_int    (hegel_gen * source,
                                  int    (*map_fn)(int, void *),    void * ctx);
hegel_gen * hegel_gen_map_i64    (hegel_gen * source,
                                  int64_t (*map_fn)(int64_t, void *), void * ctx);
hegel_gen * hegel_gen_map_double (hegel_gen * source,
                                  double (*map_fn)(double, void *), void * ctx);

/* filter — keep values satisfying predicate (non-zero = keep) */
hegel_gen * hegel_gen_filter_int    (hegel_gen * source,
                                     int (*pred_fn)(int, void *),    void * ctx);
hegel_gen * hegel_gen_filter_i64    (hegel_gen * source,
                                     int (*pred_fn)(int64_t, void *), void * ctx);
hegel_gen * hegel_gen_filter_double (hegel_gen * source,
                                     int (*pred_fn)(double, void *), void * ctx);

/* flat_map — dependent generation (callback returns a new generator) */
hegel_gen * hegel_gen_flat_map_int    (hegel_gen * source,
                                       hegel_gen * (*fn)(int, void *),    void * ctx);
hegel_gen * hegel_gen_flat_map_i64    (hegel_gen * source,
                                       hegel_gen * (*fn)(int64_t, void *), void * ctx);
hegel_gen * hegel_gen_flat_map_double (hegel_gen * source,
                                       hegel_gen * (*fn)(double, void *), void * ctx);

/* -- Scalar draw functions -- */
int      hegel_gen_draw_int    (hegel_testcase * tc, hegel_gen * gen);
int64_t  hegel_gen_draw_i64    (hegel_testcase * tc, hegel_gen * gen);
uint64_t hegel_gen_draw_u64    (hegel_testcase * tc, hegel_gen * gen);
float    hegel_gen_draw_float  (hegel_testcase * tc, hegel_gen * gen);
double   hegel_gen_draw_double (hegel_testcase * tc, hegel_gen * gen);
int      hegel_gen_draw_bool   (hegel_testcase * tc, hegel_gen * gen);

/* Draw a text/regex string into a caller-provided buffer.
** Writes a null-terminated string to buf.  Returns the string length
** (not counting null terminator), or 0 if capacity < 1. */
int hegel_gen_draw_text (hegel_testcase * tc, hegel_gen * gen,
                         char * buf, int capacity);

/* -- Optional draw functions --
**
** Returns 1 if value present (written to *out), 0 if absent.
** For non-Optional generators, always draws and returns 1. */
int hegel_gen_draw_optional_int    (hegel_testcase * tc, hegel_gen * gen, int * out);
int hegel_gen_draw_optional_i64    (hegel_testcase * tc, hegel_gen * gen, int64_t * out);
int hegel_gen_draw_optional_u64    (hegel_testcase * tc, hegel_gen * gen, uint64_t * out);
int hegel_gen_draw_optional_float  (hegel_testcase * tc, hegel_gen * gen, float * out);
int hegel_gen_draw_optional_double (hegel_testcase * tc, hegel_gen * gen, double * out);

/* -- List draw functions --
**
** Draw a variable-length list of values into a caller-provided buffer.
** Length is drawn from hegel (participates in shrinking).
** Returns the number of elements written.
**
** buf must have space for at least `capacity` elements.
** The actual length is clamped to min(max_len, capacity). */
int hegel_gen_draw_list_int    (hegel_testcase * tc, hegel_gen * elem_gen,
                                int min_len, int max_len,
                                int * buf, int capacity);
int hegel_gen_draw_list_i64    (hegel_testcase * tc, hegel_gen * elem_gen,
                                int min_len, int max_len,
                                int64_t * buf, int capacity);
int hegel_gen_draw_list_u64    (hegel_testcase * tc, hegel_gen * elem_gen,
                                int min_len, int max_len,
                                uint64_t * buf, int capacity);
int hegel_gen_draw_list_float  (hegel_testcase * tc, hegel_gen * elem_gen,
                                int min_len, int max_len,
                                float * buf, int capacity);
int hegel_gen_draw_list_double (hegel_testcase * tc, hegel_gen * elem_gen,
                                int min_len, int max_len,
                                double * buf, int capacity);

/* Free a generator and all sub-generators it owns.
** Safe to call with NULL. */
void hegel_gen_free (hegel_gen * gen);

#ifdef __cplusplus
}
#endif

#endif /* HEGEL_C_H */
