/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_label_combine and hegel_label are bit-for-bit libhegel's
**       hegel_label_combine and hegel_label_from_name.
**
** hegel-c computes labels in pure C (no engine, no test case); libhegel
** exports the same hashes.  This test dlopens libhegel itself (as the
** runtime does: $HEGEL_LIBHEGEL_PATH, else ./build/libhegel.so) and
** compares.
**
** Layer 1: hegel_label_combine() / hegel_label() — the functions under
**          test (hegel-c's own).
** Layer 2: a hegel test drawing 0..8 labels (full u64 range) and a
**          name (text), asserting hegel-c's results equal the engine's;
**          plus fixed checks: n = 0 is the FNV offset basis, order
**          matters, combining one label does not return it.
** Layer 3: main() runs it (fork mode) and exits 0 if all held.
**
** Expected: EXIT 0.
*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hegel_c.h"

/* libhegel's entry points, by their ABI (hegel.h); names prefixed so
** they don't clash with hegel_c.h's. */
typedef struct { const char * data; size_t len; } eng_str;
static void *   (*eng_context_new) (void);
static int      (*eng_label_combine) (void * ctx, const uint64_t * labels, size_t len,
                                      uint64_t * out);
static int      (*eng_label_from_name) (void * ctx, eng_str name, uint64_t * out);
static void *   g_ctx;

static
int
load_engine (void)
{
  const char *        env = getenv ("HEGEL_LIBHEGEL_PATH");
  char                path[2048] = "./build/libhegel.so";
  struct stat         st;
  void *              h;

  if (env && env[0]) {
    if (stat (env, &st) == 0 && S_ISDIR (st.st_mode))
      snprintf (path, sizeof path, "%.2000s/libhegel.so", env);
    else
      snprintf (path, sizeof path, "%s", env);
  }
  if ((h = dlopen (path, RTLD_NOW | RTLD_LOCAL)) == NULL) {
    fprintf (stderr, "could not load %s: %s\n", path, dlerror ());
    return (-1);
  }
  *(void **) &eng_context_new = dlsym (h, "hegel_context_new");
  *(void **) &eng_label_combine = dlsym (h, "hegel_label_combine");
  *(void **) &eng_label_from_name = dlsym (h, "hegel_label_from_name");
  if (!eng_context_new || !eng_label_combine || !eng_label_from_name)
    return (-1);
  g_ctx = eng_context_new ();
  return (g_ctx ? 0 : -1);
}

static
uint64_t
engine_combine (
const uint64_t *            labels,
size_t                      n)
{
  uint64_t            out = 0;

  if (eng_label_combine (g_ctx, labels, n, &out) != 0)
    hegel_fail ("engine hegel_label_combine failed");
  return (out);
}

/* ---- Layer 2: hegel test ---- */

static
void
testCombine (
hegel_testcase *            tc)
{
  uint64_t            labels[8];
  size_t              n = (size_t) hegel_draw_int (tc, 0, 8);
  size_t              i;
  char                name[64];
  uint64_t            eng;

  for (i = 0; i < n; i ++)
    labels[i] = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (hegel_label_combine (labels, n) == engine_combine (labels, n),
                "combine of %zu labels differs from the engine's", n);

  hegel_draw_text (tc, 0, 30, name, sizeof name);
  if (eng_label_from_name (g_ctx, (eng_str) { name, strlen (name) }, &eng) != 0)
    hegel_fail ("engine hegel_label_from_name failed");
  HEGEL_ASSERT (hegel_label (name) == eng, "label of \"%s\" differs from the engine's", name);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  uint64_t            ab[2] = { 1, 2 };
  uint64_t            ba[2] = { 2, 1 };
  uint64_t            one = hegel_label ("x");

  if (load_engine () != 0) {
    printf ("could not load libhegel\n");
    return (1);
  }
  if (hegel_label_combine (NULL, 0) != 0xcbf29ce484222325ULL
      || hegel_label_combine (NULL, 0) != engine_combine (NULL, 0)) {
    printf ("combine of nothing is not the FNV basis\n");
    return (1);
  }
  if (hegel_label_combine (ab, 2) == hegel_label_combine (ba, 2)) {
    printf ("combine ignores order\n");
    return (1);
  }
  if (hegel_label_combine (&one, 1) == one) {
    printf ("combining one label returned it\n");
    return (1);
  }
  return (hegel_run_test_result_n (testCombine, 500));
}
