#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# pin-worktree.sh — check out (and build) an upstream project at a pinned
# state, for the bug-hunt targets under tests/irl/<project>/.
#
# Usage:
#   scripts/pin-worktree.sh <project> <pin> [--state S] [--asan] [--path]
#   scripts/pin-worktree.sh <project> <pin> --states
#   scripts/pin-worktree.sh <project> --list
#
# <project> is a directory tests/irl/<project>/ holding project.conf (where
# the upstream repo lives and how to build it) and pins/<pin>.conf (one bug
# or target).  The checkout is a git worktree of a shared object store under
# the gitignored inspiration/, so nothing is downloaded twice and only the
# pinned commits are ever fetched.  Re-running is cheap: a stamp per build
# step skips up-to-date work.  Prints the worktree path on stdout.
#
# A pin names its states one of three ways:
#   upstream fix:   PIN_FIX_SHA                  buggy = PIN_FIX_SHA^   fixed = PIN_FIX_SHA
#   our patch:      PIN_BUGGY_REF + PIN_FIX_PATCH buggy = PIN_BUGGY_REF  fixed = it + the patch
#   no known bug:   PIN_REF                      current = PIN_REF
# Optional:
#   PIN_UPSTREAM_REF (+ PIN_UPSTREAM_EXPECT red|green): upstream's own
#                    attempt, kept as a third state (e.g. a "fix" that is
#                    still wrong, recorded as expected red)
#   PIN_PATCH        a patch applied in every state (our shims)
#   PIN_EXPECT       expectation of the `current` state (default green)
#   PIN_BUILD        build steps for project_build (default: PROJECT_BUILD)
#   PIN_ENV          extra VAR=value words for the test runs (read by the
#                    projects' test-pin, e.g. HEGEL_C_FORK=clean)
# Refs are full SHAs or tags (a partial store can only fetch those).
#
#   --state S   buggy (default) | fixed | upstream | current
#   --states    print "<state> <red|green>" per state the pin has
#   --asan      build with AddressSanitizer, in a separate ...-asan worktree
#   --path      print the worktree path only, without fetching or building

set -eu

REPO_ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)

usage() {
  echo "usage: scripts/pin-worktree.sh <project> <pin> [--state S] [--asan] [--path]" >&2
  echo "       scripts/pin-worktree.sh <project> <pin> --states" >&2
  echo "       scripts/pin-worktree.sh <project> --list" >&2
  exit 2
}

[ $# -ge 2 ] || usage
PROJECT=$1
shift
PDIR="$REPO_ROOT/tests/irl/$PROJECT"
[ -f "$PDIR/project.conf" ] || { echo "no project '$PROJECT' ($PDIR/project.conf)" >&2; exit 1; }
# shellcheck disable=SC1091
. "$PDIR/project.conf"
: "${PROJECT_REPO:?project.conf missing PROJECT_REPO}"
: "${PROJECT_STORE:?project.conf missing PROJECT_STORE}"
: "${PROJECT_WORKTREES:?project.conf missing PROJECT_WORKTREES}"
STORE="$REPO_ROOT/$PROJECT_STORE"
PINS_DIR="$PDIR/pins"

if [ "$1" = "--list" ]; then
  for conf in "$PINS_DIR"/*.conf; do
    [ -f "$conf" ] || continue
    # shellcheck disable=SC1090
    ( . "$conf"
      if [ -n "${PIN_FIX_SHA:-}" ]; then what="fix=$(printf %.12s "$PIN_FIX_SHA")"
      elif [ -n "${PIN_FIX_PATCH:-}" ]; then what="patch=$PIN_FIX_PATCH"
      else what="ref=$(printf %.12s "${PIN_REF:-?}")"; fi
      printf '  %-24s %-28s tests=%s\n' "$(basename "$conf" .conf)" "$what" "${PIN_TESTS:-}" )
  done
  exit 0
fi

PIN=$1
shift
STATE=
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
PIN_FIX_SHA=${PIN_FIX_SHA:-}
PIN_FIX_PATCH=${PIN_FIX_PATCH:-}
PIN_BUGGY_REF=${PIN_BUGGY_REF:-}
PIN_REF=${PIN_REF:-}
PIN_UPSTREAM_REF=${PIN_UPSTREAM_REF:-}
PIN_PATCH=${PIN_PATCH:-}
PIN_BUILD=${PIN_BUILD:-${PROJECT_BUILD:-}}

if [ -n "$PIN_FIX_SHA" ]; then KIND=fix
elif [ -n "$PIN_FIX_PATCH" ] && [ -n "$PIN_BUGGY_REF" ]; then KIND=patch
elif [ -n "$PIN_REF" ]; then KIND=ref
else echo "$CONF: set PIN_FIX_SHA, PIN_BUGGY_REF + PIN_FIX_PATCH, or PIN_REF" >&2; exit 1
fi

if [ "$STATES_ONLY" = 1 ]; then
  if [ "$KIND" = ref ]; then
    echo "current ${PIN_EXPECT:-green}"
  else
    echo "buggy red"
    echo "fixed green"
  fi
  [ -z "$PIN_UPSTREAM_REF" ] || echo "upstream ${PIN_UPSTREAM_EXPECT:-green}"
  exit 0
fi

[ -n "$STATE" ] || { [ "$KIND" = ref ] && STATE=current || STATE=buggy; }
PATCHES=
[ -z "$PIN_PATCH" ] || PATCHES="$PINS_DIR/$PIN_PATCH"
case "$KIND:$STATE" in
  fix:buggy)     REF="$PIN_FIX_SHA^" ;;
  fix:fixed)     REF="$PIN_FIX_SHA" ;;
  patch:buggy)   REF="$PIN_BUGGY_REF" ;;
  patch:fixed)   REF="$PIN_BUGGY_REF"; PATCHES="$PATCHES $PINS_DIR/$PIN_FIX_PATCH" ;;
  ref:current)   REF="$PIN_REF" ;;
  *:upstream)
    [ -n "$PIN_UPSTREAM_REF" ] || { echo "pin '$PIN' has no upstream state" >&2; exit 2; }
    REF="$PIN_UPSTREAM_REF" ;;
  *) echo "pin '$PIN' has no state '$STATE' (see --states)" >&2; exit 2 ;;
esac

log() { echo "pin-worktree: $*" >&2; }

# ---- object store ----
store_git() { git -C "$STORE" "$@"; }

ensure_store() {
  [ -d "$STORE" ] && return 0
  case "${PROJECT_STORE_KIND:-clone}" in
    partial)
      log "creating partial object store $STORE"
      mkdir -p "$STORE"
      store_git init -q --bare
      store_git remote add origin "$PROJECT_REPO"
      store_git config remote.origin.promisor true
      store_git config remote.origin.partialclonefilter blob:none ;;
    *)
      log "no clone at $STORE — run 'make inspiration'"; exit 1 ;;
  esac
}

# GIT_NO_LAZY_FETCH: a partial store must not fetch a missing commit on
# demand — that pulls its whole history.  fetch_ref fetches it shallow.
resolve() { GIT_NO_LAZY_FETCH=1 store_git rev-parse -q --verify "$1^{commit}" 2>/dev/null; }

fetch_ref() {
  base=${1%^}
  case "${PROJECT_STORE_KIND:-clone}" in
    partial)
      log "fetching $base"
      if printf %s "$base" | grep -qE '^[0-9a-f]{40}$'; then
        store_git fetch -q --depth 2 --filter=blob:none origin "$base"
      else
        store_git fetch -q --depth 2 --filter=blob:none origin tag "$base"
      fi ;;
    *)
      log "fetching origin into $STORE"
      store_git fetch -q --tags origin ;;
  esac
}

# Worktrees are keyed by what is checked out, so pins at the same commit
# share one: <prefix>@<sha12>[+<pin>-<state> when patched][-asan].
name_for() {
  n="$REPO_ROOT/$PROJECT_WORKTREES@$(printf %.12s "$1")"
  [ -z "$PATCHES" ] || n="$n+$PIN-$STATE"
  [ "$ASAN" = 0 ] || n="$n-asan"
  echo "$n"
}

if [ "$PATH_ONLY" = 1 ]; then
  # Without fetching: resolve what the store already has.
  ensure_store
  SHA=$(resolve "$REF") || { echo "$REF not fetched yet — run without --path first" >&2; exit 1; }
  name_for "$SHA"
  exit 0
fi

ensure_store
# One project-wide lock: worktree creation and builds never race.
exec 9>"$REPO_ROOT/$PROJECT_WORKTREES.lock"
flock 9

SHA=$(resolve "$REF") || { fetch_ref "$REF"; SHA=$(resolve "$REF") || { log "cannot resolve $REF"; exit 1; }; }
DEST=$(name_for "$SHA")

if [ ! -d "$DEST" ]; then
  log "adding worktree $DEST ($REF)"
  store_git worktree add -q --no-checkout --detach "$DEST" "$SHA"
  if [ -n "${PROJECT_SPARSE:-}" ]; then
    # shellcheck disable=SC2086
    git -C "$DEST" sparse-checkout set --cone $PROJECT_SPARSE
  fi
  git -C "$DEST" checkout -q --detach "$SHA"
  for p in $PATCHES; do
    log "applying $(basename "$p")"
    git -C "$DEST" apply "$p"
  done
fi
# Keep the sparse paths in sync with project.conf (it may have grown).
if [ -n "${PROJECT_SPARSE:-}" ]; then
  have=$(git -C "$DEST" sparse-checkout list | sort | tr '\n' ' ')
  want=$(printf '%s\n' $PROJECT_SPARSE | sort | tr '\n' ' ')
  if [ "$have" != "$want" ]; then
    log "updating sparse paths in $DEST"
    # shellcheck disable=SC2086
    git -C "$DEST" sparse-checkout set --cone $PROJECT_SPARSE
    rm -f "$DEST"/.hegel-built-*
  fi
fi
HEAD=$(git -C "$DEST" rev-parse HEAD)
[ "$HEAD" = "$SHA" ] || { log "$DEST is at $HEAD, expected $SHA — remove it: git -C $STORE worktree remove --force $DEST"; exit 1; }

PHASH=
for p in $PATCHES; do PHASH="$PHASH$(git hash-object "$p")"; done
JOBS=$(nproc 2>/dev/null || echo 4)
for step in $PIN_BUILD; do
  stamp="$DEST/.hegel-built-$step"
  want="$SHA asan=$ASAN patches=$PHASH"
  if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then continue; fi
  log "building $step in $DEST (log: $DEST/build-$step.log)"
  ( cd "$DEST" && ASAN=$ASAN JOBS=$JOBS project_build "$step" ) > "$DEST/build-$step.log" 2>&1 || {
    log "build failed — tail of $DEST/build-$step.log:"
    tail -20 "$DEST/build-$step.log" >&2
    exit 1
  }
  echo "$want" > "$stamp"
done
echo "$DEST"
