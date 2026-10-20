#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# pin-run.sh — run a pin's test binaries and judge them against the
# state's expectation (see scripts/pin-worktree.sh --states).
#
# Usage: scripts/pin-run.sh <red|green> <binary>...
#
#   green  every binary must exit 0
#   red    every binary must exit non-zero (the bug is found).  If it
#          printed a "MINIMAL nvert=N ..." line and PIN_MAX_NVERT is set,
#          N must be <= PIN_MAX_NVERT (shrink quality).
#
# Environment: PIN_TIMEOUT (seconds, default 600), PIN_MAX_NVERT,
# PIN_RUN_ENV (extra VAR=value words for the run, e.g. MALLOC_PERTURB_=170),
# HEGEL_TEST_ULIMIT (core size, default 0).  Binaries run with the repo
# root as cwd, where the runtime finds build/libhegel.so.

set -u

REPO_ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
EXPECT=${1:?usage: pin-run.sh <red|green> <binary>...}
shift

errors=0
for t in "$@"; do
  printf '  %-36s' "$(basename "$t")"
  # shellcheck disable=SC2086
  output=$(cd "$REPO_ROOT" && ulimit -c "${HEGEL_TEST_ULIMIT:-0}" && \
           env ${PIN_RUN_ENV:-} timeout "${PIN_TIMEOUT:-600}" "$t" 2>&1)
  rc=$?
  caveat=$(printf '%s\n' "$output" | grep -m1 -oE 'caveat: .*')
  if [ "$EXPECT" = green ]; then
    if [ $rc -eq 0 ]; then echo "OK (green)"
    else
      echo "FAIL — expected green, got exit $rc"
      printf '%s\n' "$output" | tail -20
      errors=$((errors + 1))
    fi
  elif [ $rc -eq 0 ]; then
    echo "FAIL — expected red, bug not found (exit 0)"
    errors=$((errors + 1))
  elif [ $rc -eq 124 ]; then
    echo "FAIL — timed out after ${PIN_TIMEOUT:-600}s"
    errors=$((errors + 1))
  else
    minimal=$(printf '%s\n' "$output" | grep -oE 'MINIMAL nvert=[0-9]+ nedges=[0-9]+ edges=\[[^]]*\]' | tail -1)
    origin=$(printf '%s\n' "$output" | grep -m1 -oE '^origin: .*')
    if [ -n "$minimal" ] && [ -n "${PIN_MAX_NVERT:-}" ]; then
      nvert=$(echo "$minimal" | sed -n 's/MINIMAL nvert=\([0-9]*\).*/\1/p')
      if [ "$nvert" -gt "$PIN_MAX_NVERT" ]; then
        echo "FAIL — shrunk to nvert=$nvert (expected <= $PIN_MAX_NVERT): $minimal"
        errors=$((errors + 1))
      else
        echo "OK (red, shrunk to $minimal)"
      fi
    else
      echo "OK (red, exit $rc)"
    fi
    [ -z "$origin" ] || echo "      $origin"
  fi
  [ -z "$caveat" ] || echo "      $caveat"
done
[ $errors -eq 0 ]
