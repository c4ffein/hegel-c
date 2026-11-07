/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** The two helpers every libhegel example shares (libhegel/examples/
** hegel_check.h, re-typed here): CHECK aborts on an unexpected error
** code with the context's message, STR builds a hegel_string_t.  The
** raw_*.c programs use only these on top of hegel.h.
*/
#ifndef RAW_CHECK_H
#define RAW_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel.h"

#define STR(s) ((hegel_string_t){ (s), strlen (s) })

#define CHECK(fn, ctx, ...)                                                   \
  do {                                                                        \
    hegel_result_t check_rc_ = fn ((ctx), ##__VA_ARGS__);                     \
    if (check_rc_ != HEGEL_OK) {                                              \
      hegel_string_t m_ = hegel_context_last_error ((ctx));                   \
      fprintf (stderr, "%s:%d: %s: rc=%d %.*s\n", __FILE__, __LINE__, #fn,    \
               (int) check_rc_, (int) m_.len, m_.data ? m_.data : "");        \
      abort ();                                                               \
    }                                                                         \
  } while (0)

#endif
