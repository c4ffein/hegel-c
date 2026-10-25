#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
#
# Reproducible sandbox for testing the hegel-bughunt skill against Scotch.
#
# Goal: create a clean directory containing only what an agent applying the
# hegel-bughunt skill from cold should see — no internal project docs,
# no test references, no git history, no bug-disclosing strings. The agent
# operates in this sandbox; we then verify whether the skill drives the
# agent to the known SCOTCH_graphOrder + STRATDISCONNECTED bug autonomously.
#
# Usage:
#   scripts/setup-bughunt-sandbox.sh [SANDBOX_PATH]
#
# Defaults to /tmp/hegel-bughunt-sandbox-N (auto-incremented if exists).

set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ $# -ge 1 ]]; then
    SANDBOX="$1"
else
    n=1
    while [[ -e "/tmp/hegel-bughunt-sandbox-$n" ]]; do
        n=$((n + 1))
    done
    SANDBOX="/tmp/hegel-bughunt-sandbox-$n"
fi

echo "==== Source: $SOURCE_ROOT"
echo "==== Sandbox: $SANDBOX"

if [[ -e "$SANDBOX" ]]; then
    echo "ERROR: $SANDBOX already exists. Delete it or pick another path." >&2
    exit 1
fi

mkdir -p "$SANDBOX"/{hegel-c,scotch,workspace}

# ---- Stage 0: ensure the artifacts under test are current ----
#
# The sandbox ships PREBUILT binaries (Stage 1), so a stale build would
# mean dogfooding against old code.  Rebuild both from source, then diff
# their checksums against the previous run — so a change in the library
# under test is visible before we launch the agent.

echo "==== Ensuring engine + lib are current (make lib libhegel) ===="
make -C "$SOURCE_ROOT" lib libhegel

LIB_A="$SOURCE_ROOT/build/libhegel_c.a"
LIB_SO="$SOURCE_ROOT/build/libhegel.so"
for f in "$LIB_A" "$LIB_SO"; do
    [[ -f "$f" ]] || { echo "ERROR: expected prebuilt $f is missing." >&2; exit 1; }
done

HASH_RECORD="$SOURCE_ROOT/build/.bughunt-sandbox-libhashes"
NEW_HASHES="$(cd "$SOURCE_ROOT/build" && sha256sum libhegel_c.a libhegel.so)"
echo "==== Library-under-test checksums ===="
echo "$NEW_HASHES" | sed 's/^/    /'
if [[ -f "$HASH_RECORD" ]]; then
    if [[ "$NEW_HASHES" == "$(cat "$HASH_RECORD")" ]]; then
        echo "    (identical to the previous sandbox run)"
    else
        echo "    CHANGED since the previous sandbox run:"
        diff <(cat "$HASH_RECORD") <(echo "$NEW_HASHES") | sed 's/^/      /' || true
    fi
fi
printf '%s\n' "$NEW_HASHES" > "$HASH_RECORD"

# ---- Stage 1: copy hegel-c essentials (PREBUILT, no source) ----
#
# The agent gets:
#   - public headers (hegel_c.h, hegel_gen.h)
#   - the PREBUILT static lib (build/libhegel_c.a) and engine cdylib
#     (build/libhegel.so) — no hegel-c or engine source at all
#   - the .claude/skills/hegel and .claude/skills/hegel-bughunt skills
#   - CLAUDE.md (project conventions; vetted clean of bug content)
#
# The agent does NOT get:
#   - README.md / TODO.md / NEXT.md (mention the bug)
#   - docs/findings.toml, build/findings.html, submissions/ (the bugs we found
#     and the patches we sent for them)
#   - docs/* (internal docs)
#   - tests/* (any of them — would leak example tests / patterns)
#   - inspiration/* (other than scotch — would leak hegel-skill, theft, etc.)
#   - core/ or hegel_gen.c source (internal; already inside libhegel_c.a)
#   - .git/ (deleted bug-tests are in history)

mkdir -p "$SANDBOX/hegel-c/build"
cp "$SOURCE_ROOT/hegel_c.h"          "$SANDBOX/hegel-c/"
cp "$SOURCE_ROOT/hegel_gen.h"        "$SANDBOX/hegel-c/"
cp "$SOURCE_ROOT/CLAUDE.md"          "$SANDBOX/hegel-c/"
cp "$LIB_A"                          "$SANDBOX/hegel-c/build/"
cp "$LIB_SO"                         "$SANDBOX/hegel-c/build/"
cp -r "$SOURCE_ROOT/.claude"         "$SANDBOX/hegel-c/"

# ---- Stage 2: copy Scotch ----
#
# Take the upstream Scotch source (no fork-specific bug reports) and copy
# its src/ tree into the sandbox. We pre-build sequential libscotch so the
# agent doesn't burn time on the build.

if [[ -d "$SOURCE_ROOT/inspiration/targets/scotch" ]]; then
    cp -r "$SOURCE_ROOT/inspiration/targets/scotch/"* "$SANDBOX/scotch/"
else
    echo "ERROR: inspiration/targets/scotch not found. Run 'make inspiration' in the source first." >&2
    exit 1
fi

# Strip any .git, REPORTS, or hegel/ scaffolding that may have leaked in
find "$SANDBOX/scotch" -type d \( -name '.git' -o -name 'REPORTS' -o -name 'hegel' \) -exec rm -rf {} + 2>/dev/null || true

# Build sequential Scotch so the agent doesn't have to
echo "==== Building Scotch ===="
if [[ -d "$SANDBOX/scotch/src" ]]; then
    cd "$SANDBOX/scotch/src"
    if [[ ! -f Makefile.inc ]]; then
        cp Make.inc/Makefile.inc.x86-64_pc_linux2 Makefile.inc 2>/dev/null \
            || echo "WARN: no template Makefile.inc found; agent will need to handle"
    fi
    make scotch -j4 2>&1 | tail -20 || echo "WARN: scotch build failed"
    cd "$SANDBOX"
fi

# ---- Stage 3: empty workspace dir for agent output ----

mkdir -p "$SANDBOX/workspace/tests"

# ---- Stage 4: strip remaining .git directories ----

find "$SANDBOX" -type d -name '.git' -exec rm -rf {} + 2>/dev/null || true

# ---- Stage 5: bug-leak audit ----
#
# Scan the agent-visible files for strings that would leak the bug.
# Scotch's own source is allowed to mention SCOTCH_STRATDISCONNECTED (it's
# public API — the agent will see it from headers anyway). What we forbid
# is bug-context strings: the bug report URL, the offending function name,
# any "this is broken" prose.

echo "==== Bug-leak audit ===="
LEAK_PATTERNS=(
    "hgraphOrderCp"
    "BUG_REPORT"
    "off-by-ordenum"
    "off.by.ordenum"
    "graphOrder.*bug"
    "bug.*graphOrder"
    "STRATDISCONNECTED.*bug"
    "bug.*STRATDISCONNECTED"
    "c4ffein/scotch"
)

LEAKED=0
# Scan everything except scotch source (its CHANGELOG, etc., may legitimately mention things)
for pat in "${LEAK_PATTERNS[@]}"; do
    hits=$(grep -rli "$pat" "$SANDBOX/hegel-c/" 2>/dev/null || true)
    if [[ -n "$hits" ]]; then
        echo "  LEAK: pattern '$pat' found in:"
        echo "$hits" | sed 's/^/    /'
        LEAKED=1
    fi
done

if [[ "$LEAKED" -ne 0 ]]; then
    echo "==== BUG LEAK DETECTED. Sandbox is not safe for honest dogfood. ===="
    exit 2
fi

# ---- Stage 6: summary ----

echo
echo "==== Sandbox ready ===="
echo "  Path: $SANDBOX"
echo
echo "  Layout:"
echo "    hegel-c/         headers + .claude/skills/ + prebuilt build/{libhegel_c.a,libhegel.so}"
echo "    scotch/          upstream Scotch source + pre-built libscotch"
echo "    workspace/       empty; agent writes its test suite into workspace/tests/"
echo
echo "  Sizes:"
du -sh "$SANDBOX"/{hegel-c,scotch,workspace} 2>/dev/null | sed 's/^/    /'
echo
echo "  Agent should be told to:"
echo "    - cd $SANDBOX/workspace"
echo "    - read .claude/skills/ in $SANDBOX/hegel-c/"
echo "    - compile with -I$SANDBOX/hegel-c and link $SANDBOX/hegel-c/build/libhegel_c.a"
echo "    - export HEGEL_LIBHEGEL_PATH=$SANDBOX/hegel-c/build/libhegel.so  (runtime engine)"
echo "    - link against $SANDBOX/scotch/lib/ + $SANDBOX/scotch/include/"
echo "    - write the audit suite into $SANDBOX/workspace/tests/"
