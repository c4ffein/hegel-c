/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

#include "hegel_engine.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static hg_engine g_engine;
static void *    g_handle  = NULL;
static int       g_failed  = 0;

static void * try_dlopen (const char * path, char * tried, size_t tried_cap)
{
  size_t used = strlen (tried);
  snprintf (tried + used, tried_cap - used, "  %s\n", path);
  return dlopen (path, RTLD_NOW | RTLD_LOCAL);
}

static void * open_libhegel (char * tried, size_t tried_cap)
{
  const char * env = getenv ("HEGEL_LIBHEGEL_PATH");
  if (env && env[0]) {
    struct stat st;
    if (stat (env, &st) == 0 && S_ISDIR (st.st_mode)) {
      char p[2048];
      snprintf (p, sizeof p, "%.2000s/libhegel.so", env);
      void * h = try_dlopen (p, tried, tried_cap);
      if (h) return h;
    } else {
      void * h = try_dlopen (env, tried, tried_cap);
      if (h) return h;
    }
  }
  void * h = try_dlopen ("./build/libhegel.so", tried, tried_cap);
  if (h) return h;
  h = try_dlopen ("./.hegel/libhegel/libhegel.so", tried, tried_cap);
  if (h) return h;
  return try_dlopen ("libhegel.so", tried, tried_cap);
}

/* Resolve one symbol or record failure. */
static void * need (void * handle, const char * name, int * ok)
{
  void * p = dlsym (handle, name);
  if (!p) {
    fprintf (stderr, "hegel-c: libhegel is missing symbol %s (%s) — is it older than "
             "hegeltest-c 0.47?\n", name, dlerror ());
    *ok = 0;
  }
  return p;
}

#define BIND(field, sym) (*(void **) &e->field = need (h, sym, &ok))

const hg_engine * hg_engine_get (void)
{
  if (g_handle) return &g_engine;
  if (g_failed) return NULL;

  char tried[4096] = "";
  void * h = open_libhegel (tried, sizeof tried);
  if (!h) {
    fprintf (stderr,
             "hegel-c: could not load libhegel.so — the Hegel engine cdylib.\n"
             "Paths tried:\n%s"
             "Set HEGEL_LIBHEGEL_PATH to the .so (or its directory), or run\n"
             "`make libhegel` at the repo root to place it under ./build/.\n"
             "Prebuilt artifacts: https://github.com/hegeldev/hegel/releases\n",
             tried);
    g_failed = 1;
    return NULL;
  }

  int ok = 1;
  hg_engine * e = &g_engine;
  BIND (context_new,                "hegel_context_new");
  BIND (context_last_error,         "hegel_context_last_error");
  BIND (version,                    "hegel_version");
  BIND (settings_new,               "hegel_settings_new");
  BIND (settings_free,              "hegel_settings_free");
  BIND (settings_set_test_cases,    "hegel_settings_set_test_cases");
  BIND (settings_set_verbosity,     "hegel_settings_set_verbosity");
  BIND (settings_set_seed,          "hegel_settings_set_seed");
  BIND (settings_set_report_multiple_failures, "hegel_settings_set_report_multiple_failures");
  BIND (settings_set_database,      "hegel_settings_set_database");
  BIND (settings_set_database_key,  "hegel_settings_set_database_key");
  BIND (settings_set_phases,        "hegel_settings_set_phases");
  BIND (settings_set_suppress_health_check, "hegel_settings_set_suppress_health_check");
  BIND (run_start,                  "hegel_run_start");
  BIND (run_start_blob,             "hegel_run_start_blob");
  BIND (next_test_case,             "hegel_next_test_case");
  BIND (run_result,                 "hegel_run_result");
  BIND (run_result_free,            "hegel_run_result_free");
  BIND (run_free,                   "hegel_run_free");
  BIND (test_case_free,             "hegel_test_case_free");
  BIND (test_case_should_capture,   "hegel_test_case_should_capture");
  BIND (mark_complete,              "hegel_mark_complete");
  BIND (start_span,                 "hegel_start_span");
  BIND (stop_span,                  "hegel_stop_span");
  BIND (target,                     "hegel_target");
  BIND (generate_boolean,           "hegel_generate_boolean");
  BIND (generate_integer,           "hegel_generate_integer");
  BIND (generate_integer_big,       "hegel_generate_integer_big");
  BIND (generate_float,             "hegel_generate_float");
  BIND (generate_bytes,             "hegel_generate_bytes");
  BIND (generate_bytes_result_free, "hegel_generate_bytes_result_free");
  BIND (string_generator_text,      "hegel_string_generator_text");
  BIND (string_generator_regex,     "hegel_string_generator_regex");
  BIND (string_generator_free,      "hegel_string_generator_free");
  BIND (generate_string,            "hegel_generate_string");
  BIND (owned_string_free,          "hegel_owned_string_free");
  BIND (new_collection,             "hegel_new_collection");
  BIND (collection_more,            "hegel_collection_more");
  BIND (collection_reject,          "hegel_collection_reject");
  BIND (collection_free,            "hegel_collection_free");
  BIND (new_recursion,              "hegel_new_recursion");
  BIND (recursion_branch,           "hegel_recursion_branch");
  BIND (recursion_leaf,             "hegel_recursion_leaf");
  BIND (recursion_retry,            "hegel_recursion_retry");
  BIND (recursion_finish,           "hegel_recursion_finish");
  BIND (recursion_free,             "hegel_recursion_free");
  BIND (new_pool,                   "hegel_new_pool");
  BIND (pool_add,                   "hegel_pool_add");
  BIND (pool_generate,              "hegel_pool_generate");
  BIND (pool_restore,               "hegel_pool_restore");
  BIND (pool_free,                  "hegel_pool_free");
  BIND (new_state_machine,          "hegel_new_state_machine");
  BIND (sm_next_group,              "hegel_state_machine_next_group");
  BIND (sm_next_rule,               "hegel_state_machine_next_rule");
  BIND (sm_rule_rejected,           "hegel_state_machine_rule_rejected");
  BIND (sm_should_check_invariant,  "hegel_state_machine_should_check_invariant");
  BIND (sm_free,                    "hegel_state_machine_free");
  BIND (result_status,              "hegel_run_result_status");
  BIND (result_error,               "hegel_run_result_error");
  BIND (result_error_kind,          "hegel_run_result_error_kind");
  BIND (result_failure_count,       "hegel_run_result_failure_count");
  BIND (result_failure,             "hegel_run_result_failure");
  BIND (failure_free,               "hegel_failure_free");
  BIND (failure_origin,             "hegel_failure_origin");
  BIND (failure_blob,               "hegel_failure_reproduction_blob");
  BIND (failure_caveat,             "hegel_failure_caveat");

  if (!ok) {
    dlclose (h);
    g_failed = 1;
    return NULL;
  }
  e->ctx = e->context_new ();
  g_handle = h;
  return &g_engine;
}

const char * hg_last_error (const hg_engine * e)
{
  static char buf[2048];
  hg_str s = e->context_last_error (e->ctx);
  size_t n = s.len < sizeof buf - 1 ? s.len : sizeof buf - 1;
  if (n) memcpy (buf, s.data, n);
  buf[n] = '\0';
  return buf;
}
