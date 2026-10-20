/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property: any structurally-generated nftables ruleset must parse
** successfully under libnftables in dry-run mode (no kernel mutation).
**
** This is the minimal v0 nft test — it validates the parser front end
** without touching kernel state.  Run inside the qemu/Alpine guest
** where libnftables is installed.  See .qemu/ for the harness. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "hegel_c.h"
#include <nftables/libnftables.h>

#define MAX_TABLES           3
#define MAX_CHAINS_PER_TABLE 2
#define MAX_RULES_PER_CHAIN  3
#define BUF_SIZE             8192

/* Limit hooks to ones that are valid for `type filter` in family inet. */
static const char *FILTER_HOOKS[] = { "input", "output", "forward" };
#define N_FILTER_HOOKS (sizeof(FILTER_HOOKS) / sizeof(FILTER_HOOKS[0]))

/* Simple terminal verdicts that are valid as standalone rule statements. */
static const char *VERDICTS[] = { "accept", "drop", "return" };
#define N_VERDICTS (sizeof(VERDICTS) / sizeof(VERDICTS[0]))

static int append (char *buf, int *off, int max, const char *fmt, ...)
{
  if (*off >= max) return -1;
  va_list ap;
  va_start (ap, fmt);
  int n = vsnprintf (buf + *off, max - *off, fmt, ap);
  va_end (ap);
  if (n < 0 || n >= max - *off) return -1;
  *off += n;
  return 0;
}

void test_nft_dryrun_parse (hegel_testcase *tc)
{
  char buf[BUF_SIZE];
  int off = 0;

  int n_tables = hegel_draw_int (tc, 1, MAX_TABLES);
  for (int t = 0; t < n_tables; t++) {
    HEGEL_ASSERT (append (buf, &off, BUF_SIZE,
                          "add table inet t%d\n", t) == 0,
                  "buffer overflow at table %d", t);
    int n_chains = hegel_draw_int (tc, 1, MAX_CHAINS_PER_TABLE);
    for (int c = 0; c < n_chains; c++) {
      int hook_idx = hegel_draw_int (tc, 0, N_FILTER_HOOKS - 1);
      int prio     = hegel_draw_int (tc, -100, 100);
      HEGEL_ASSERT (append (buf, &off, BUF_SIZE,
                            "add chain inet t%d c%d { type filter hook %s priority %d; }\n",
                            t, c, FILTER_HOOKS[hook_idx], prio) == 0,
                    "buffer overflow at chain t%d c%d", t, c);
      int n_rules = hegel_draw_int (tc, 0, MAX_RULES_PER_CHAIN);
      for (int r = 0; r < n_rules; r++) {
        int v = hegel_draw_int (tc, 0, N_VERDICTS - 1);
        HEGEL_ASSERT (append (buf, &off, BUF_SIZE,
                              "add rule inet t%d c%d %s\n",
                              t, c, VERDICTS[v]) == 0,
                      "buffer overflow at rule t%d c%d r%d", t, c, r);
      }
    }
  }

  hegel_note (tc, buf);

  struct nft_ctx *ctx = nft_ctx_new (NFT_CTX_DEFAULT);
  HEGEL_ASSERT (ctx != NULL, "nft_ctx_new failed");
  nft_ctx_set_dry_run (ctx, true);
  nft_ctx_buffer_output (ctx);
  nft_ctx_buffer_error (ctx);

  int rc = nft_run_cmd_from_buffer (ctx, buf);
  if (rc != 0) {
    const char *err = nft_ctx_get_error_buffer (ctx);
    /* Note before assert so the failure replay shows the err text. */
    hegel_note (tc, err ? err : "(no error buffer)");
  }
  HEGEL_ASSERT (rc == 0, "nft dry-run parse failed (rc=%d)", rc);

  nft_ctx_free (ctx);
}

int main (void)
{
  hegel_run_test_n (test_nft_dryrun_parse, 100);
  return 0;
}
