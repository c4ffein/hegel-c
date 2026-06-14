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
    fprintf (stderr, "hegel-c: libhegel is missing symbol %s (%s)\n",
             name, dlerror ());
    *ok = 0;
  }
  return p;
}

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
             "Prebuilt artifacts: https://github.com/hegeldev/hegel-rust/releases\n",
             tried);
    g_failed = 1;
    return NULL;
  }

  int ok = 1;
  hg_engine * e = &g_engine;
  *(void **)&e->settings_new          = need (h, "hegel_settings_new", &ok);
  *(void **)&e->settings_free         = need (h, "hegel_settings_free", &ok);
  *(void **)&e->settings_test_cases   = need (h, "hegel_settings_test_cases", &ok);
  *(void **)&e->settings_verbosity    = need (h, "hegel_settings_verbosity", &ok);
  *(void **)&e->settings_seed         = need (h, "hegel_settings_seed", &ok);
  *(void **)&e->settings_derandomize  = need (h, "hegel_settings_derandomize", &ok);
  *(void **)&e->settings_report_multiple_failures
                                      = need (h, "hegel_settings_report_multiple_failures", &ok);
  *(void **)&e->settings_database     = need (h, "hegel_settings_database", &ok);
  *(void **)&e->settings_phases       = need (h, "hegel_settings_phases", &ok);
  *(void **)&e->settings_suppress_health_check
                                      = need (h, "hegel_settings_suppress_health_check", &ok);
  *(void **)&e->run_start             = need (h, "hegel_run_start", &ok);
  *(void **)&e->next_test_case        = need (h, "hegel_next_test_case", &ok);
  *(void **)&e->run_result            = need (h, "hegel_run_result", &ok);
  *(void **)&e->run_free              = need (h, "hegel_run_free", &ok);
  *(void **)&e->generate              = need (h, "hegel_generate", &ok);
  *(void **)&e->start_span            = need (h, "hegel_start_span", &ok);
  *(void **)&e->stop_span             = need (h, "hegel_stop_span", &ok);
  *(void **)&e->new_pool              = need (h, "hegel_new_pool", &ok);
  *(void **)&e->pool_add              = need (h, "hegel_pool_add", &ok);
  *(void **)&e->pool_generate         = need (h, "hegel_pool_generate", &ok);
  *(void **)&e->target                = need (h, "hegel_target", &ok);
  *(void **)&e->mark_complete         = need (h, "hegel_mark_complete", &ok);
  *(void **)&e->tc_is_final_replay    = need (h, "hegel_test_case_is_final_replay", &ok);
  *(void **)&e->result_passed         = need (h, "hegel_run_result_passed", &ok);
  *(void **)&e->result_failure_count  = need (h, "hegel_run_result_failure_count", &ok);
  *(void **)&e->result_failure        = need (h, "hegel_run_result_failure", &ok);
  *(void **)&e->failure_panic_message = need (h, "hegel_failure_panic_message", &ok);
  *(void **)&e->failure_diagnostic    = need (h, "hegel_failure_diagnostic", &ok);
  *(void **)&e->failure_origin        = need (h, "hegel_failure_origin", &ok);
  *(void **)&e->failure_reproduction_blob
                                      = need (h, "hegel_failure_reproduction_blob", &ok);
  *(void **)&e->last_error_message    = need (h, "hegel_last_error_message", &ok);
  *(void **)&e->version               = need (h, "hegel_version", &ok);

  if (!ok) {
    dlclose (h);
    g_failed = 1;
    return NULL;
  }
  g_handle = h;
  return &g_engine;
}
