/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** dlopen binding to libhegel — the official C ABI of Hegel's native
** in-process engine (hegel-rust's `hegeltest-c` crate, header
** `hegel.h`).
**
** We bind at runtime via dlopen/dlsym rather than linking -lhegel,
** for two reasons:
**   1. Symbol freedom: libhegel exports `hegel_start_span` /
**      `hegel_stop_span` with different signatures than the public
**      hegel-c API of the same names.  dlsym'd function pointers
**      live in our namespace; nothing collides at link time.
**   2. Distribution: a test binary needs only this static lib; the
**      engine cdylib is located at runtime (env var, ./build/, or
**      system paths) and can be the prebuilt GitHub-release artifact.
**
** Types below mirror hegel.h's opaque handles and constants.  The
** authoritative documentation is upstream's header:
** inspiration/hegel/hegel-rust/hegel-c/include/hegel.h
*/

#ifndef HEGEL_ENGINE_H
#define HEGEL_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Opaque engine handles (never dereferenced on our side) */
typedef struct hg_settings  hg_settings;
typedef struct hg_run       hg_run;
typedef struct hg_tc        hg_tc;
typedef struct hg_result    hg_result;
typedef struct hg_failure   hg_failure;

/* Status codes (hegel.h HEGEL_OK / HEGEL_E_*) */
#define HG_OK                   0
#define HG_E_STOP_TEST         -1
#define HG_E_ASSUME            -2
#define HG_E_BACKEND           -3
#define HG_E_INVALID_HANDLE    -4
#define HG_E_INVALID_ARG       -5
#define HG_E_ALREADY_COMPLETE  -6
#define HG_E_NOT_COMPLETE      -7
#define HG_E_INTERNAL          -8

/* hegel_status_t */
#define HG_STATUS_VALID       0
#define HG_STATUS_INVALID     1
#define HG_STATUS_OVERRUN     2
#define HG_STATUS_INTERESTING 3

/* hegel_verbosity_t */
#define HG_VERBOSITY_QUIET    0
#define HG_VERBOSITY_NORMAL   1
#define HG_VERBOSITY_VERBOSE  2
#define HG_VERBOSITY_DEBUG    3

/* Resolved entry points of the loaded libhegel. */
typedef struct {
  hg_settings * (*settings_new)            (void);
  void          (*settings_free)           (hg_settings * s);
  void          (*settings_test_cases)     (hg_settings * s, uint64_t n);
  void          (*settings_verbosity)      (hg_settings * s, int v);
  void          (*settings_seed)           (hg_settings * s, uint64_t seed, bool has_seed);
  void          (*settings_derandomize)    (hg_settings * s, bool derandomize);
  void          (*settings_report_multiple_failures) (hg_settings * s, bool yes);
  void          (*settings_database)       (hg_settings * s, const char * database);
  void          (*settings_phases)         (hg_settings * s, uint32_t phases);
  void          (*settings_suppress_health_check) (hg_settings * s, uint32_t checks);

  hg_run *      (*run_start)               (const hg_settings * settings);
  hg_tc *       (*next_test_case)          (hg_run * run);
  const hg_result * (*run_result)          (hg_run * run);
  void          (*run_free)                (hg_run * run);

  int           (*generate)                (hg_tc * tc,
                                            const uint8_t * schema_cbor, size_t schema_len,
                                            const uint8_t ** out_value_cbor, size_t * out_value_len);
  int           (*start_span)              (hg_tc * tc, uint64_t label);
  int           (*stop_span)               (hg_tc * tc, bool discard);
  int           (*new_pool)                (hg_tc * tc, int64_t * out_pool_id);
  int           (*pool_add)                (hg_tc * tc, int64_t pool_id, int64_t * out_variable_id);
  int           (*pool_generate)           (hg_tc * tc, int64_t pool_id, bool consume,
                                            int64_t * out_variable_id);
  int           (*target)                  (hg_tc * tc, double value, const char * label);
  int           (*mark_complete)           (hg_tc * tc, int status, const char * origin);
  bool          (*tc_is_final_replay)      (const hg_tc * tc);

  bool          (*result_passed)           (const hg_result * r);
  size_t        (*result_failure_count)    (const hg_result * r);
  const hg_failure * (*result_failure)     (const hg_result * r, size_t index);
  const char *  (*failure_panic_message)   (const hg_failure * f);
  const char *  (*failure_diagnostic)      (const hg_failure * f);
  const char *  (*failure_origin)          (const hg_failure * f);
  const char *  (*failure_reproduction_blob) (const hg_failure * f);

  const char *  (*last_error_message)      (void);
  const char *  (*version)                 (void);
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

#endif /* HEGEL_ENGINE_H */
