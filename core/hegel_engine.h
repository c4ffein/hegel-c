/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** dlopen binding to libhegel — the official C ABI of Hegel's native
** in-process engine (hegel-rust's `hegeltest-c` crate, header
** `hegel.h`; verified against hegeltest-c 0.47.1 / hegel-rust 0.50.1).
**
** We bind at runtime via dlopen/dlsym rather than linking -lhegel,
** for two reasons:
**   1. Symbol freedom: libhegel exports `hegel_start_span`,
**      `hegel_note`, `hegel_target`, `hegel_collection_more`, ... with
**      different signatures than the public hegel-c API of the same
**      names.  dlsym'd function pointers live in our namespace;
**      nothing collides at link time.
**   2. Distribution: a test binary needs only this static lib; the
**      engine cdylib is located at runtime (env var, ./build/, or
**      system paths) and can be the prebuilt GitHub-release artifact.
**
** Types below mirror hegel.h's opaque handles and constants (we can't
** include hegel.h itself next to hegel_c.h — the same-named functions
** would clash).  The authoritative documentation is upstream's header:
** deps/hegel-rust/hegel-c/include/hegel.h
**
** Every engine call takes the single process-wide context `e->ctx`
** (single active run per process by design); `hg_last_error` reads its
** diagnostic.
*/

#ifndef HEGEL_ENGINE_H
#define HEGEL_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Opaque engine handles (never dereferenced on our side) */
typedef struct hg_context    hg_context;
typedef struct hg_settings   hg_settings;
typedef struct hg_run        hg_run;
typedef struct hg_tc         hg_tc;
typedef struct hg_result     hg_result;
typedef struct hg_failure    hg_failure;
typedef struct hg_collection hg_collection;
typedef struct hg_recursion  hg_recursion;
typedef struct hg_pool       hg_pool;
typedef struct hg_sm         hg_sm;
typedef struct hg_strgen     hg_strgen;

/* hegel_string_t / hegel_owned_string_t / hegel_generate_bytes_result_t */
typedef struct { const char * data; size_t len; } hg_str;
typedef struct { uint8_t * data; size_t len; }    hg_bytes;

#define HG_STR_Z(s) ((hg_str){ (s), strlen (s) })

/* hegel_result_t */
#define HG_OK                   0
#define HG_E_STOP_TEST         -1
#define HG_E_ASSUME            -2
#define HG_E_BACKEND           -3
#define HG_E_INVALID_HANDLE    -4
#define HG_E_INVALID_ARG       -5
#define HG_E_ALREADY_COMPLETE  -6
#define HG_E_NOT_COMPLETE      -7
#define HG_E_INTERNAL          -8
#define HG_E_CONCURRENT_USE    -9
#define HG_E_RETRY            -10
#define HG_E_NOT_FOUND        -11

/* hegel_status_t */
#define HG_STATUS_VALID       0
#define HG_STATUS_INVALID     1
#define HG_STATUS_OVERRUN     2
#define HG_STATUS_INTERESTING 3

/* hegel_verbosity_t (note: NORMAL is 0 since the 0.2x ABI) */
#define HG_VERBOSITY_NORMAL   0
#define HG_VERBOSITY_QUIET    1
#define HG_VERBOSITY_VERBOSE  2
#define HG_VERBOSITY_DEBUG    3

/* hegel_run_status_t */
#define HG_RUN_PASSED 0
#define HG_RUN_FAILED 1
#define HG_RUN_ERROR  2

/* hegel_run_error_kind_t */
#define HG_RUN_ERROR_NONE             0
#define HG_RUN_ERROR_HEALTH_CHECK     1
#define HG_RUN_ERROR_FLAKY            2
#define HG_RUN_ERROR_NONDETERMINISTIC 3
#define HG_RUN_ERROR_UNSATISFIABLE    4
#define HG_RUN_ERROR_USAGE            5
#define HG_RUN_ERROR_INTERNAL         6

/* HEGEL_STATE_MACHINE_DONE */
#define HG_SM_DONE INT64_MIN

typedef void (*hg_output_cb) (void * user_data, const char * line, size_t len);

/* Resolved entry points of the loaded libhegel. */
typedef struct {
  hg_context * ctx;

  hg_context * (*context_new)        (void);
  hg_str       (*context_last_error) (const hg_context * ctx);
  int          (*version)            (hg_context * ctx, hg_str * out);

  int (*settings_new)                   (hg_context *, hg_settings ** out);
  int (*settings_free)                  (hg_context *, hg_settings *);
  int (*settings_set_test_cases)        (hg_context *, hg_settings *, uint64_t n);
  int (*settings_set_verbosity)         (hg_context *, hg_settings *, uint32_t v);
  int (*settings_set_seed)              (hg_context *, hg_settings *, uint64_t seed, bool has);
  int (*settings_set_report_multiple_failures) (hg_context *, hg_settings *, bool yes);
  int (*settings_set_database)          (hg_context *, hg_settings *, const hg_str * db);

  int (*run_start)        (hg_context *, const hg_settings *, hg_output_cb, void *, hg_run ** out);
  int (*next_test_case)   (hg_context *, hg_run *, hg_tc ** out);
  int (*run_result)       (hg_context *, hg_run *, hg_result ** out);
  int (*run_result_free)  (hg_context *, hg_result *);
  int (*run_free)         (hg_context *, hg_run *);

  int (*test_case_free)           (hg_context *, hg_tc *);
  int (*test_case_should_capture) (hg_context *, const hg_tc *, bool * out);
  int (*mark_complete)            (hg_context *, hg_tc *, uint32_t status, const hg_str * origin);

  int (*start_span) (hg_context *, hg_tc *, uint64_t label);
  int (*stop_span)  (hg_context *, hg_tc *, bool discard);
  int (*target)     (hg_context *, hg_tc *, double value, hg_str label);

  int (*generate_boolean)     (hg_context *, hg_tc *, double p, bool forced, bool has_forced,
                               bool * out);
  int (*generate_integer)     (hg_context *, hg_tc *, int64_t min, int64_t max, int64_t * out);
  int (*generate_integer_big) (hg_context *, hg_tc *, const uint8_t * min, size_t min_len,
                               const uint8_t * max, size_t max_len,
                               uint8_t * out, size_t out_cap, size_t * out_len);
  int (*generate_float)       (hg_context *, hg_tc *, uint32_t width, double min, double max,
                               bool allow_nan, bool allow_inf, bool excl_min, bool excl_max,
                               double smallest_nonzero, double * out);
  int (*generate_bytes)       (hg_context *, hg_tc *, uint64_t min, uint64_t max, hg_bytes * out);
  int (*generate_bytes_result_free) (hg_context *, hg_bytes *);

  int (*string_generator_text)  (hg_context *, uint64_t min, uint64_t max, const hg_str * codec,
                                 uint32_t min_cp, uint32_t max_cp,
                                 const hg_str * cats, size_t ncats,
                                 const hg_str * xcats, size_t nxcats,
                                 hg_str include, hg_str exclude, hg_strgen ** out);
  int (*string_generator_regex) (hg_context *, hg_str pattern, bool fullmatch,
                                 const hg_strgen * alphabet, hg_strgen ** out);
  int (*string_generator_free)  (hg_context *, hg_strgen *);
  int (*generate_string)        (hg_context *, hg_tc *, const hg_strgen *, hg_str * out);
  int (*owned_string_free)      (hg_context *, hg_str *);

  int (*new_collection)    (hg_context *, hg_tc *, uint64_t min, uint64_t max, hg_collection ** out);
  int (*collection_more)   (hg_context *, hg_tc *, hg_collection *, bool * out);
  int (*collection_reject) (hg_context *, hg_tc *, hg_collection *, const hg_str * why);
  int (*collection_free)   (hg_context *, hg_collection *);

  int (*new_recursion)    (hg_context *, hg_tc *, uint64_t max_depth, uint64_t max_leaves,
                           hg_recursion ** out);
  int (*recursion_branch) (hg_context *, hg_tc *, hg_recursion *, uint64_t depth, bool * out);
  int (*recursion_leaf)   (hg_context *, hg_tc *, hg_recursion *);
  int (*recursion_retry)  (hg_context *, hg_tc *, hg_recursion *);
  int (*recursion_finish) (hg_context *, hg_tc *, hg_recursion *);
  int (*recursion_free)   (hg_context *, hg_recursion *);

  int (*new_pool)      (hg_context *, hg_tc *, hg_pool ** out);
  int (*pool_add)      (hg_context *, hg_tc *, hg_pool *, int64_t * out_id);
  int (*pool_generate) (hg_context *, hg_tc *, hg_pool *, bool consume, int64_t * out_id);
  int (*pool_restore)  (hg_context *, hg_tc *, hg_pool *, int64_t id);
  int (*pool_free)     (hg_context *, hg_pool *);

  int (*new_state_machine) (hg_context *, hg_tc *,
                            const hg_str * rule_names, const int64_t * rule_groups,
                            const double * rule_weights, size_t num_rules,
                            const hg_str * inv_names, const bool * inv_always,
                            size_t num_invs, int64_t min_conc, int64_t max_conc,
                            int64_t step_count, hg_sm ** out, int64_t * out_conc);
  int (*sm_next_group)          (hg_context *, hg_tc *, hg_sm *, int64_t * out_group);
  int (*sm_next_rule)           (hg_context *, hg_tc *, hg_sm *, int64_t worker, int64_t * out);
  int (*sm_rule_rejected)       (hg_context *, hg_tc *, hg_sm *, int64_t worker);
  int (*sm_should_check_invariant) (hg_context *, hg_tc *, hg_sm *, int64_t idx, bool * out);
  int (*sm_free)                (hg_context *, hg_sm *);

  int (*result_status)        (hg_context *, const hg_result *, uint32_t * out);
  int (*result_error)         (hg_context *, const hg_result *, const hg_str ** out);
  int (*result_error_kind)    (hg_context *, const hg_result *, uint32_t * kind, uint32_t * hc);
  int (*result_failure_count) (hg_context *, const hg_result *, size_t * out);
  int (*result_failure)       (hg_context *, const hg_result *, size_t i, hg_failure ** out);
  int (*failure_free)         (hg_context *, hg_failure *);
  int (*failure_origin)       (hg_context *, const hg_failure *, hg_str * out);
  int (*failure_blob)         (hg_context *, const hg_failure *, const hg_str ** out);
  int (*failure_caveat)       (hg_context *, const hg_failure *, const hg_str ** out);
} hg_engine;

/*
** Load libhegel and resolve all entry points.  Idempotent — the first
** successful load is cached for the process lifetime.  Search order:
**   1. $HEGEL_LIBHEGEL_PATH (a .so file, or a directory containing
**      libhegel.so)
**   2. ./build/libhegel.so   (relative to cwd — test harnesses cd to
**      the repo root)
**   3. ./.hegel/libhegel/libhegel.so
**   4. dlopen("libhegel.so") — the system loader path
**
** On failure prints the paths tried to stderr and returns NULL.
*/
const hg_engine * hg_engine_get (void);

/* The context's last diagnostic as a NUL-terminated string (copied into
** a static buffer; valid until the next call). */
const char * hg_last_error (const hg_engine * e);

#endif /* HEGEL_ENGINE_H */
