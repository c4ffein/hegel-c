#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# scotch-worktree.sh — build Scotch at a pinned bug's before/after state.
#
# Usage:
#   scripts/scotch-worktree.sh <pin> [--state buggy|fixed|upstream] [--asan] [--path]
#   scripts/scotch-worktree.sh <pin> --states
#   scripts/scotch-worktree.sh --list
#
# Reads tests/irl/scotch/pins/<pin>.conf and makes a git worktree of the
# main Scotch clone (inspiration/targets/scotch, from `make inspiration`)
# at inspiration/targets/scotch@<pin>-<state>, then builds it with the
# main clone's src/Makefile.inc.  Worktrees share the clone's objects, so
# nothing is downloaded twice; everything stays under the gitignored
# inspiration/.  Re-running is cheap: a stamp skips an up-to-date build.
#
# A pin names its states one of two ways:
#   upstream fix:  PIN_FIX_SHA            buggy = PIN_FIX_SHA^, fixed = PIN_FIX_SHA
#   our patch:     PIN_BUGGY_REF + PIN_FIX_PATCH (relative to pins/)
#                                         buggy = PIN_BUGGY_REF, fixed = it + the patch
# and may add PIN_UPSTREAM_REF (+ PIN_UPSTREAM_EXPECT red|green): upstream's
# own attempt, kept as a third state — e.g. an upstream fix that is still
# wrong, recorded as an expected-RED state.
#
#   --state buggy  (default)  the bug is present           (expected red)
#   --state fixed             the fix is applied           (expected green)
#   --state upstream          PIN_UPSTREAM_REF             (PIN_UPSTREAM_EXPECT)
#   --states                  print "<state> <red|green>" per state the pin has
#   --asan                    build with AddressSanitizer, in ...@<pin>-<state>-asan
#                             (link the tests with -fsanitize=address too)
#   --path                    print the worktree path only (for Makefiles)

set -eu

REPO_ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
PINS_DIR="$REPO_ROOT/tests/irl/scotch/pins"
MAIN="$REPO_ROOT/inspiration/targets/scotch"

usage() {
  echo "usage: scripts/scotch-worktree.sh <pin> [--state buggy|fixed|upstream] [--asan] [--path]" >&2
  echo "       scripts/scotch-worktree.sh <pin> --states" >&2
  echo "       scripts/scotch-worktree.sh --list" >&2
  exit 2
}

[ $# -ge 1 ] || usage
if [ "$1" = "--list" ]; then
  for conf in "$PINS_DIR"/*.conf; do
    [ -f "$conf" ] || continue
    # shellcheck disable=SC1090
    ( . "$conf"
      if [ -n "${PIN_FIX_SHA:-}" ]; then fix="fix=$(printf %.12s "$PIN_FIX_SHA")"
      else fix="patch=$PIN_FIX_PATCH"; fi
      printf '  %-18s %-28s tests=%s\n' "$(basename "$conf" .conf)" "$fix" "$PIN_TESTS" )
  done
  exit 0
fi

PIN=$1
shift
STATE=buggy
PATH_ONLY=0
STATES_ONLY=0
ASAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --state)   STATE=${2:?}; shift 2 ;;
    --state=*) STATE=${1#--state=}; shift ;;
    --path)    PATH_ONLY=1; shift ;;
    --asan)    ASAN=1; shift ;;
    --states)  STATES_ONLY=1; shift ;;
    *) echo "unknown argument: $1" >&2; usage ;;
  esac
done

CONF="$PINS_DIR/$PIN.conf"
[ -f "$CONF" ] || { echo "no pin '$PIN' ($CONF); try --list" >&2; exit 1; }
# shellcheck disable=SC1090
. "$CONF"
PIN_BUILD=${PIN_BUILD:-scotch}
PIN_FIX_SHA=${PIN_FIX_SHA:-}
PIN_FIX_PATCH=${PIN_FIX_PATCH:-}
PIN_UPSTREAM_REF=${PIN_UPSTREAM_REF:-}
if [ -n "$PIN_FIX_SHA" ]; then
  BUGGY_REF="$PIN_FIX_SHA^"
elif [ -n "$PIN_FIX_PATCH" ] && [ -n "${PIN_BUGGY_REF:-}" ]; then
  BUGGY_REF="$PIN_BUGGY_REF"
else
  echo "$CONF: set PIN_FIX_SHA, or PIN_BUGGY_REF + PIN_FIX_PATCH" >&2; exit 1
fi

if [ "$STATES_ONLY" = 1 ]; then
  echo "buggy red"
  echo "fixed green"
  [ -z "$PIN_UPSTREAM_REF" ] || echo "upstream ${PIN_UPSTREAM_EXPECT:-green}"
  exit 0
fi

PATCH=
case "$STATE" in
  buggy) REF="$BUGGY_REF" ;;
  fixed)
    if [ -n "$PIN_FIX_SHA" ]; then REF="$PIN_FIX_SHA"
    else REF="$BUGGY_REF"; PATCH="$PINS_DIR/$PIN_FIX_PATCH"; fi ;;
  upstream)
    [ -n "$PIN_UPSTREAM_REF" ] || { echo "pin '$PIN' has no upstream state" >&2; exit 2; }
    REF="$PIN_UPSTREAM_REF" ;;
  *) echo "--state must be buggy, fixed or upstream, got '$STATE'" >&2; exit 2 ;;
esac

DEST="$MAIN@$PIN-$STATE"
[ "$ASAN" = 0 ] || DEST="$DEST-asan"
if [ "$PATH_ONLY" = 1 ]; then echo "$DEST"; exit 0; fi

# Progress goes to stderr so callers can capture stdout.
log() { echo "scotch-worktree: $*" >&2; }

[ -d "$MAIN/.git" ] || { log "no Scotch clone at $MAIN — run 'make inspiration'"; exit 1; }
[ -f "$MAIN/src/Makefile.inc" ] || {
  log "$MAIN/src/Makefile.inc missing — copy one from $MAIN/src/Make.inc/ first"
  exit 1
}

if ! git -C "$MAIN" cat-file -e "$REF^{commit}" 2>/dev/null; then
  log "fetching $REF into the main clone"
  git -C "$MAIN" fetch -q origin
fi
SHA=$(git -C "$MAIN" rev-parse "$REF")

if [ -d "$DEST" ]; then
  HEAD=$(git -C "$DEST" rev-parse HEAD)
  [ "$HEAD" = "$SHA" ] || { log "$DEST is at $HEAD, expected $SHA — remove it with 'git -C $MAIN worktree remove --force $DEST'"; exit 1; }
else
  log "adding worktree $DEST at $REF"
  git -C "$MAIN" worktree add -q --detach "$DEST" "$SHA"
  if [ -n "$PATCH" ]; then
    log "applying $(basename "$PATCH")"
    git -C "$DEST" apply "$PATCH"
  fi
fi

STAMP="$DEST/.hegel-built"
WANT="$SHA $PIN_BUILD asan=$ASAN patch=${PATCH:+$(git hash-object "$PATCH")}"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ]; then
  log "$PIN [$STATE] up to date ($DEST)"
  exit 0
fi

cp "$MAIN/src/Makefile.inc" "$DEST/src/Makefile.inc"
if [ "$ASAN" = 1 ]; then
  # Scotch's build tools run during the build, so they get ASan too: fine.
  sed -i -e 's/^\(CFLAGS[[:space:]]*=.*\)/\1 -fsanitize=address -fno-omit-frame-pointer -g/' \
         -e 's/^\(LDFLAGS[[:space:]]*=.*\)/\1 -fsanitize=address/' "$DEST/src/Makefile.inc"
fi
JOBS=$(nproc 2>/dev/null || echo 4)
for target in $PIN_BUILD; do
  log "building $target at $REF (log: $DEST/build-$target.log)"
  make -C "$DEST/src" -j"$JOBS" "$target" > "$DEST/build-$target.log" 2>&1 || {
    log "build failed — tail of $DEST/build-$target.log:"
    tail -20 "$DEST/build-$target.log" >&2
    exit 1
  }
done
echo "$WANT" > "$STAMP"
log "built $PIN [$STATE] -> $DEST"
