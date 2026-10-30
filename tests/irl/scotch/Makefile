# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# Scotch integration tests — property-test real Scotch graph
# partitioning with hegel-c.
#
# Two ways to get Scotch headers and libs:
#
#   make test-local  — uses inspiration/scotch built from source
#                      (make inspiration && cd inspiration/scotch/src && make scotch)
#
#   make test-deb    — uses system libscotch-dev (apt install libscotch-dev)
#                      headers in /usr/include/scotch, used in CI

REPO_ROOT    = $(abspath ../../..)
HEGEL_C_LIB  = $(REPO_ROOT)/build
SCOTCH_LOCAL ?= $(REPO_ROOT)/inspiration/targets/scotch

# Scotch paths — set by test-local / test-deb targets
SCOTCH_INC   ?=
SCOTCH_LIB   ?=

# Core-dump size limit (1K blocks).  Default 0 suppresses cores from
# expected child crashes (TESTS_SHRINK reducer demos).  Override with
# `make HEGEL_TEST_ULIMIT=unlimited test-local` to keep cores.
HEGEL_TEST_ULIMIT ?= 0

# Env for the reducer-quality (shrink) tests.  The graphOrder bug reads
# UNINITIALIZED heap memory inside Scotch, so whether a given input
# crashes depends on heap garbage — nondeterministic across replays,
# which trips the engine's flaky-test detector and aborts shrinking.
# MALLOC_PERTURB_ makes glibc fill fresh allocations with a fixed byte
# pattern: same input -> same garbage -> same outcome -> clean shrink.
HEGEL_SHRINK_ENV ?= MALLOC_PERTURB_=170

# ASAN=1: link against an AddressSanitizer Scotch (see test-pin).
ASAN       ?=
ASAN_FLAGS  = $(if $(filter 1,$(ASAN)),-fsanitize=address -fno-omit-frame-pointer -g)

CC       = gcc
MPICC    = mpicc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) $(SCOTCH_INC) -funwind-tables -fexceptions $(ASAN_FLAGS)
LDFLAGS      = $(ASAN_FLAGS) -L$(HEGEL_C_LIB) $(SCOTCH_LIB) -lhegel_c -lscotch -lscotcherr -lz -lm -lpthread -ldl -lrt
LDFLAGS_MPI  = -L$(HEGEL_C_LIB) $(SCOTCH_LIB) -lhegel_c -lptscotch -lscotch -lptscotcherr -lscotcherr -lz -lm -lpthread -ldl -lrt

# Sequential Scotch tests
TESTS_PASS = test_graph_part

# Reducer-quality demos: tests that are EXPECTED to fail and that
# we additionally check shrunk to a small minimal counterexample.
# Each binary must (a) exit non-zero and (b) emit a hegel_note line
# starting with "MINIMAL nvert=N nedges=M ..." where N is small.
TESTS_SHRINK = test_graph_order_shrink

# PT-Scotch (MPI) tests — use MPI_Comm_spawn, no mpiexec
TESTS_MPI  = test_dgraph_part

# SCOTCH_archBuild terminal-pairing bug-hunt tests (arch_build.c:309).
# NOT part of the default suite: the bug is unfixed upstream, so these
# are expected RED on the pristine reference checkout (and on upstream
# patch e6c094fc), GREEN only with the v2 one-line fix.  See
# arch_build_pairing.h for the bug matrix; run via
# `make test-archbuild-pairing`.
TESTS_PAIRING = test_arch_build_pairing_classic test_arch_build_pairing

ALL_TESTS = $(TESTS_PASS) $(TESTS_SHRINK) $(TESTS_MPI)

# Property suite (props/): one property per Scotch API area, from the
# 2026-04 blind bug-hunt dogfood plus the witnesses of later finds.  Each
# props/test_x.c builds as ./test_x.  Not in test-local / test-deb: ~25 s per
# test, and older Scotch (Debian's) fails the ordering ones on the
# hgraphOrderCp bug.  `make test-props` runs the ones expected GREEN on
# current master: PIN_TESTS of pins/current.conf.  The others witness open
# bugs and are run by their pins (`make test-pin PIN=...`).
TESTS_PROPS       = $(basename $(notdir $(wildcard props/test_*.c)))
TESTS_PROPS_GREEN = $(shell . ./pins/current.conf; echo $$PIN_TESTS)

.PHONY: FORCE help all clean test test-local test-deb _run-tests hegel-c-lib check-license \
	test-pin test-pins fetch-pins test-props _run-props

help:
	@echo "tests/irl/scotch Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build all test binaries (requires SCOTCH_INC / SCOTCH_LIB)"
	@echo "  test-local        Build and test with inspiration/scotch (built from source)"
	@echo "  test-deb          Build and test with system libscotch-dev"
	@echo "  test-props [ASAN=1]"
	@echo "                    Property suite (props/) against inspiration/targets/scotch,"
	@echo "                    the tests expected green (red ones run via their pins)"
	@echo "  test-pin PIN=x [STATE=buggy|fixed|upstream] [ASAN=1]"
	@echo "                    Build Scotch at bug pin x (pins/x.conf) in a worktree and"
	@echo "                    run its tests: buggy must fail + shrink small, fixed must pass,"
	@echo "                    upstream (if the pin has one) as the pin says"
	@echo "  test-pins         test-pin for every pin and state ($(PINS))"
	@echo "  fetch-pins        Build every pin's Scotch worktrees (then works offline)"
	@echo "  hegel-c-lib       Build libhegel_c.a only"
	@echo "  check-license     Verify SPDX headers"
	@echo "  clean             Remove test binaries"
	@echo ""
	@echo "Variables:"
	@echo "  HEGEL_TEST_ULIMIT=N  Core-dump size limit (1K blocks) for test runs"
	@echo "                       Default 0 (suppress); set to 'unlimited' to keep cores"
	@echo ""
	@echo "Includes PT-Scotch MPI tests (requires mpicc + libptscotch)"

all: hegel-c-lib $(ALL_TESTS)

# libhegel_c.a is delegated to the root Makefile, which tracks every
# core/ source and header.  FORCE asks it on every run (a near-instant
# no-op when current); make then re-checks the file, so test binaries
# relink only when the library really changed.  `libhegel` keeps
# build/libhegel.so pointing at the pinned engine build.
hegel-c-lib: $(HEGEL_C_LIB)/libhegel_c.a

$(HEGEL_C_LIB)/libhegel_c.a: FORCE
	@$(MAKE) --no-print-directory -C $(REPO_ROOT) lib libhegel

FORCE:

test_graph_part: test_graph_part.c $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_graph_order_shrink: test_graph_order_shrink.c $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(TESTS_PROPS): %: props/%.c props/graph_gen.h props/scotch_helpers.h $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -Iprops -o $@ $< $(LDFLAGS)

test_dgraph_part: test_dgraph_part.c $(HEGEL_C_LIB)/libhegel_c.a
	$(MPICC) $(CFLAGS) -o $@ $< $(LDFLAGS_MPI)

test_arch_build_pairing_classic: test_arch_build_pairing_classic.c arch_build_pairing.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_arch_build_pairing: test_arch_build_pairing.c arch_build_pairing.h $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Bug-hunt runner for the arch_build.c:309 pairing bug (local Scotch
# build only — the point is to compare Scotch source variants).
test-archbuild-pairing:
	$(MAKE) _run-pairing \
		SCOTCH_INC="-I$(SCOTCH_LOCAL)/include" \
		SCOTCH_LIB="-L$(SCOTCH_LOCAL)/lib"

_run-pairing: hegel-c-lib $(TESTS_PAIRING)
	@errors=0; \
	echo ""; \
	echo "==== SCOTCH_archBuild pairing tests (expect RED on buggy Scotch) ===="; \
	for t in $(TESTS_PAIRING); do \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && timeout 300 $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "GREEN"; \
		else \
			echo "RED (exit $$rc)"; \
			echo "$$output" | tail -20; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	if [ $$errors -ne 0 ]; then exit 1; fi

# ---- Bug pins: one Scotch worktree per (bug, state) ----------------------
# A pin (pins/<name>.conf) names a bug's before/after states and the tests
# that witness it.  scripts/pin-worktree.sh builds Scotch at the requested
# state in a worktree of inspiration/targets/scotch (shared with the Linux
# pins, see tests/irl/linux/); the pin's tests are rebuilt against it and
# judged by scripts/pin-run.sh.  ASAN=1 uses an AddressSanitizer build of
# Scotch (separate worktree) and links the tests with it.
PIN   ?=
STATE ?=
PINS   = $(basename $(notdir $(wildcard pins/*.conf)))
WORKTREE = $(REPO_ROOT)/scripts/pin-worktree.sh scotch
PINRUN   = $(REPO_ROOT)/scripts/pin-run.sh

test-pin: hegel-c-lib
	@[ -n "$(PIN)" ] || { echo "usage: make test-pin PIN=<pin> [STATE=buggy|fixed|upstream] [ASAN=1]  (pins: $(PINS))" >&2; exit 2; }
	@set -e; . pins/$(PIN).conf; \
	state="$(STATE)"; \
	[ -n "$$state" ] || state=$$($(WORKTREE) $(PIN) --states | head -1 | cut -d' ' -f1); \
	expect=$$($(WORKTREE) $(PIN) --states | awk -v s="$$state" '$$1 == s { print $$2 }'); \
	asan="$(ASAN)"; [ "$${PIN_ASAN:-}" != 1 ] || asan=1; \
	dir=$$($(WORKTREE) $(PIN) --state $$state $$( [ "$$asan" != 1 ] || echo --asan)); \
	echo ""; echo "==== Pin $(PIN) [$$state$$( [ "$$asan" != 1 ] || echo +asan), expect $$expect] ===="; \
	rm -f $$PIN_TESTS; \
	$(MAKE) --no-print-directory $$PIN_TESTS SCOTCH_INC="-I$$dir/include" SCOTCH_LIB="-L$$dir/lib" \
		ASAN=$$asan >/dev/null; \
	bins=""; for t in $$PIN_TESTS; do bins="$$bins $(CURDIR)/$$t"; done; \
	PIN_MAX_NVERT=$${PIN_MAX_NVERT:-} \
		PIN_RUN_ENV="$(HEGEL_SHRINK_ENV) ASAN_OPTIONS=detect_leaks=0 $${PIN_ENV:-}" \
		HEGEL_TEST_ULIMIT=$(HEGEL_TEST_ULIMIT) $(PINRUN) $$expect $$bins

test-pins:
	@errors=0; \
	for p in $(PINS); do for s in $$($(WORKTREE) $$p --states | cut -d' ' -f1); do \
		$(MAKE) --no-print-directory test-pin PIN=$$p STATE=$$s || errors=$$((errors + 1)); \
	done; done; \
	echo ""; echo "==== Pins: $$errors failing (pin, state) pair(s) ===="; \
	[ $$errors -eq 0 ]

fetch-pins:
	@for p in $(PINS); do for s in $$($(WORKTREE) $$p --states | cut -d' ' -f1); do \
		$(WORKTREE) $$p --state $$s >/dev/null || exit 1; \
	done; done; echo "all pins fetched and built"

test-props: hegel-c-lib
	@rm -f $(TESTS_PROPS_GREEN)
	@$(MAKE) --no-print-directory $(TESTS_PROPS_GREEN) SCOTCH_INC="-I$(SCOTCH_LOCAL)/include" \
		SCOTCH_LIB="-L$(SCOTCH_LOCAL)/lib" ASAN=$(ASAN) >/dev/null
	@echo ""; echo "==== Scotch property suite (props/, expect green) ===="
	@bins=""; for t in $(TESTS_PROPS_GREEN); do bins="$$bins $(CURDIR)/$$t"; done; \
		PIN_RUN_ENV="ASAN_OPTIONS=detect_leaks=0" HEGEL_TEST_ULIMIT=$(HEGEL_TEST_ULIMIT) \
		$(PINRUN) green $$bins

test:
	@echo "ERROR: use 'make test-local' or 'make test-deb'" >&2; exit 1

test-local: clean
	$(MAKE) _run-tests \
		SCOTCH_INC="-I$(SCOTCH_LOCAL)/include" \
		SCOTCH_LIB="-L$(SCOTCH_LOCAL)/lib"

test-deb: clean
	$(MAKE) _run-tests \
		SCOTCH_INC="-I/usr/include/scotch" \
		SCOTCH_LIB=""

_run-tests: all check-license
	@passed=0; errors=0; total=0; \
	echo ""; \
	echo "==== Scotch sequential tests ===="; \
	for t in $(TESTS_PASS); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "OK"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL (exit $$rc)"; \
			echo "$$output"; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Scotch reducer-quality tests (must shrink to small counterexample) ===="; \
	for t in $(TESTS_SHRINK); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(HEGEL_SHRINK_ENV) timeout 120 $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "FAIL — expected non-zero exit but got 0"; \
			errors=$$((errors + 1)); \
		else \
			minimal=$$(echo "$$output" | grep -oE 'MINIMAL nvert=[0-9]+ nedges=[0-9]+ edges=\[[^]]*\]' | tail -1); \
			if [ -z "$$minimal" ]; then \
				echo "FAIL — exit $$rc but no MINIMAL line in stderr"; \
				errors=$$((errors + 1)); \
			else \
				nvert=$$(echo "$$minimal" | sed -n 's/MINIMAL nvert=\([0-9]*\).*/\1/p'); \
				if [ "$$nvert" -gt 5 ]; then \
					echo "FAIL — shrunk to nvert=$$nvert (expected <= 5): $$minimal"; \
					errors=$$((errors + 1)); \
				else \
					echo "OK (shrunk to $$minimal)"; \
					passed=$$((passed + 1)); \
				fi; \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "==== PT-Scotch MPI tests (MPI_Comm_spawn, no mpiexec) ===="; \
	for t in $(TESTS_MPI); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && OMPI_MCA_btl=tcp,self timeout 120 $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "OK"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL (exit $$rc)"; \
			echo "$$output"; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Results: $$passed/$$total passed, $$errors errors ===="; \
	if [ $$errors -ne 0 ]; then exit 1; fi

check-license:
	@missing=0; \
	for f in *.c props/*.c props/*.h Makefile; do \
		if ! head -3 "$$f" | grep -q "SPDX-License-Identifier[:] MIT"; then \
			echo "  MISSING: $$f"; \
			missing=$$((missing + 1)); \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo "$$missing file(s) missing SPDX header"; \
		exit 1; \
	fi; \
	echo "All files have SPDX headers."

clean:
	rm -f $(ALL_TESTS) $(TESTS_PAIRING) $(TESTS_PROPS)
