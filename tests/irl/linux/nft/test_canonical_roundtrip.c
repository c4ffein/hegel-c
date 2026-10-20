/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* STATUS — KNOWN BLOCKED 2026-04-30.
**
** This test compiles and runs but every case fails with kernel EOPNOTSUPP
** ("Could not process rule: Not supported") on the very first real-add
** (`add table inet t0`).  The same input via the standalone `nft` binary
** or via a tiny libnftables-only C reproducer (.qemu/probes/lib_probe.c)
** works perfectly in the same VM with the same kernel modules loaded.
** The failure only manifests when libnftables is linked against the
** hegel-c static lib.  Hypotheses (untested):
**
**   1. Symbol conflict between Rust's libc / panic hook and libnftnl /
**      libmnl that libnftables depends on for netlink.
**   2. hegeltest's signal/IPC plumbing leaving residual state that
**      breaks netlink RECV.
**   3. Some sigprocmask delta from the Rust runtime that drops netlink
**      ack messages.
**
** Resolving needs strace + netlink message comparison + maybe rebuilding
** libnftables with debug symbols.  Out of scope for the v0 sandbox push
** but a real prerequisite for kernel-state nft round-trip testing.
** Reproducer: cd .qemu && python3 drive.py run_canonical.sh.
**
** Property when this is unblocked: canonicalization stability of
** nftables rulesets. */

/* Property: canonicalization stability of nftables rulesets.
**
**     1. Generate ruleset s (structurally valid)
**     2. Real-add to kernel via libnftables
**     3. Capture canonical form c1 = `list ruleset`
**     4. Flush kernel
**     5. Re-add c1 via a fresh ctx
**     6. Capture canonical form c2
**     7. Flush kernel
**     8. assert c1 == c2  (modulo trailing whitespace)
**
** A failure means libnftables's parse → format pipeline is not idempotent
** for some structurally valid input — a real upstream finding.
**
** REQUIRES kernel netlink access — must run inside the qemu/Alpine guest
** built by .qemu/.  Each test case mutates kernel ruleset state and flushes
** between attempts; do NOT run on a host you care about. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "hegel_c.h"
#include <nftables/libnftables.h>

#define MAX_TABLES           2
#define MAX_CHAINS_PER_TABLE 3
#define MAX_RULES_PER_CHAIN  3
#define BUF_SIZE             16384

static const char *FILTER_HOOKS[] = { "input", "output", "forward" };
#define N_FILTER_HOOKS (sizeof(FILTER_HOOKS) / sizeof(FILTER_HOOKS[0]))

/* Verdict kinds — index selects a generator below. */
enum {
  V_ACCEPT,
  V_DROP,
  V_RETURN,
  V_COUNTER_ACCEPT,
  V_COUNTER_DROP,
  V_JUMP,
  V_GOTO,
  N_VERDICT_KINDS
};

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

/* Strip trailing whitespace from each line + collapse repeated blank lines.
** This makes c1 == c2 robust against incidental whitespace differences
** that aren't real canonicalization bugs. */
static void normalize (const char *in, char *out, int max)
{
  int oi = 0;
  int line_start = 0;
  bool last_blank = false;
  for (int i = 0; in[i] && oi < max - 1; i++) {
    if (in[i] == '\n') {
      while (oi > line_start && (out[oi-1] == ' ' || out[oi-1] == '\t')) oi--;
      bool blank_line = (oi == line_start);
      if (blank_line && last_blank) { /* drop dupes */ continue; }
      out[oi++] = '\n';
      last_blank = blank_line;
      line_start = oi;
    } else {
      out[oi++] = in[i];
    }
  }
  out[oi] = 0;
}

/* Run a single command via a shared context. Output buffers are
** freed/reset between calls via nft_ctx_*_buffer toggles. */
static int run_cmd (struct nft_ctx *ctx, const char *cmd,
                    char *out_buf, int out_max,
                    char *err_buf, int err_max)
{
  /* Reset the buffers — toggle off+on to clear accumulated output. */
  nft_ctx_unbuffer_output (ctx);
  nft_ctx_unbuffer_error (ctx);
  nft_ctx_buffer_output (ctx);
  nft_ctx_buffer_error (ctx);
  int rc = nft_run_cmd_from_buffer (ctx, cmd);
  const char *o = nft_ctx_get_output_buffer (ctx);
  const char *e = nft_ctx_get_error_buffer (ctx);
  if (out_buf) snprintf (out_buf, out_max, "%s", o ? o : "");
  if (err_buf) snprintf (err_buf, err_max, "%s", e ? e : "");
  return rc;
}

void test_nft_canonical_roundtrip (hegel_testcase *tc)
{
  char input[BUF_SIZE];
  int off = 0;

  int n_tables = hegel_draw_int (tc, 1, MAX_TABLES);
  for (int t = 0; t < n_tables; t++) {
    HEGEL_ASSERT (append (input, &off, BUF_SIZE,
                          "add table inet t%d\n", t) == 0,
                  "buffer overflow");

    int n_chains = hegel_draw_int (tc, 1, MAX_CHAINS_PER_TABLE);
    /* First emit chain headers so jumps can reference them. */
    for (int c = 0; c < n_chains; c++) {
      int hook_idx = hegel_draw_int (tc, 0, N_FILTER_HOOKS - 1);
      int prio     = hegel_draw_int (tc, -100, 100);
      HEGEL_ASSERT (append (input, &off, BUF_SIZE,
                            "add chain inet t%d c%d { type filter hook %s priority %d; }\n",
                            t, c, FILTER_HOOKS[hook_idx], prio) == 0,
                    "buffer overflow");
    }

    /* Then rules — jumps may target chains 0..n_chains-1 in same table. */
    for (int c = 0; c < n_chains; c++) {
      int n_rules = hegel_draw_int (tc, 0, MAX_RULES_PER_CHAIN);
      for (int r = 0; r < n_rules; r++) {
        int v = hegel_draw_int (tc, 0, N_VERDICT_KINDS - 1);
        const char *fmt = NULL;
        int target = 0;
        switch (v) {
        case V_ACCEPT:         fmt = "add rule inet t%d c%d accept\n"; break;
        case V_DROP:           fmt = "add rule inet t%d c%d drop\n"; break;
        case V_RETURN:         fmt = "add rule inet t%d c%d return\n"; break;
        case V_COUNTER_ACCEPT: fmt = "add rule inet t%d c%d counter accept\n"; break;
        case V_COUNTER_DROP:   fmt = "add rule inet t%d c%d counter drop\n"; break;
        case V_JUMP: {
          target = hegel_draw_int (tc, 0, n_chains - 1);
          /* Avoid self-jump which works but is uninteresting. */
          if (target == c) target = (c + 1) % n_chains;
          HEGEL_ASSERT (append (input, &off, BUF_SIZE,
                                "add rule inet t%d c%d jump c%d\n",
                                t, c, target) == 0, "buffer overflow");
          continue;
        }
        case V_GOTO: {
          target = hegel_draw_int (tc, 0, n_chains - 1);
          if (target == c) target = (c + 1) % n_chains;
          HEGEL_ASSERT (append (input, &off, BUF_SIZE,
                                "add rule inet t%d c%d goto c%d\n",
                                t, c, target) == 0, "buffer overflow");
          continue;
        }
        }
        if (fmt) {
          HEGEL_ASSERT (append (input, &off, BUF_SIZE, fmt, t, c) == 0,
                        "buffer overflow");
        }
      }
    }
  }

  hegel_note (tc, "input:");
  hegel_note (tc, input);

  /* One shared ctx for all kernel ops in this test case. */
  struct nft_ctx *ctx = nft_ctx_new (NFT_CTX_DEFAULT);
  HEGEL_ASSERT (ctx != NULL, "nft_ctx_new failed");

  /* Cleanup any prior test residue first (defensive). */
  char tmp_err[2048];
  run_cmd (ctx, "flush ruleset\n", NULL, 0, tmp_err, sizeof tmp_err);

  /* Step 1: real-add the generated ruleset. */
  char err1[2048];
  int rc1 = run_cmd (ctx, input, NULL, 0, err1, sizeof err1);
  if (rc1 != 0) {
    hegel_note (tc, "first add failed:");
    hegel_note (tc, err1);
  }
  HEGEL_ASSERT (rc1 == 0, "first add failed (rc=%d)", rc1);

  /* Step 2: capture canonical form c1. */
  char c1[BUF_SIZE], err2[2048];
  int rc2 = run_cmd (ctx, "list ruleset\n", c1, sizeof c1, err2, sizeof err2);
  HEGEL_ASSERT (rc2 == 0, "first list failed (rc=%d): %s", rc2, err2);

  /* Step 3: flush. flush ruleset removes all tables, sets, chains, rules. */
  run_cmd (ctx, "flush ruleset\n", NULL, 0, tmp_err, sizeof tmp_err);

  /* Step 4: re-add c1. */
  char err3[2048];
  int rc3 = run_cmd (ctx, c1, NULL, 0, err3, sizeof err3);
  if (rc3 != 0) {
    hegel_note (tc, "canonical c1 failed to re-parse:");
    hegel_note (tc, c1);
    hegel_note (tc, "error:");
    hegel_note (tc, err3);
  }
  HEGEL_ASSERT (rc3 == 0, "canonical re-parse failed (rc=%d)", rc3);

  /* Step 5: capture c2. */
  char c2[BUF_SIZE], err4[2048];
  int rc4 = run_cmd (ctx, "list ruleset\n", c2, sizeof c2, err4, sizeof err4);
  HEGEL_ASSERT (rc4 == 0, "second list failed (rc=%d)", rc4);

  /* Final flush so next test case starts clean. */
  run_cmd (ctx, "flush ruleset\n", NULL, 0, tmp_err, sizeof tmp_err);
  nft_ctx_free (ctx);

  /* Step 6: compare canonicals (after whitespace normalization). */
  char n1[BUF_SIZE], n2[BUF_SIZE];
  normalize (c1, n1, BUF_SIZE);
  normalize (c2, n2, BUF_SIZE);

  if (strcmp (n1, n2) != 0) {
    hegel_note (tc, "DIVERGENCE: c1 != c2");
    hegel_note (tc, "c1:");
    hegel_note (tc, n1);
    hegel_note (tc, "c2:");
    hegel_note (tc, n2);
  }
  HEGEL_ASSERT (strcmp (n1, n2) == 0, "canonical form not stable");
}

int main (void)
{
  /* nofork mode: each test case runs in the same process. Required because
  ** libnftables's netlink subsystem doesn't survive fork-then-netlink in the
  ** child (kernel returns EOPNOTSUPP on add table inet under fork mode but
  ** the manual nft binary works fine in the same VM — likely a netlink-pid
  ** or capability quirk we'd need more time to characterize). */
  hegel_run_test_nofork_n (test_nft_canonical_roundtrip, 200);
  return 0;
}
