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
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release
SCOTCH_LOCAL = $(REPO_ROOT)/inspiration/scotch

# Scotch paths — set by test-local / test-deb targets
SCOTCH_INC   ?=
SCOTCH_LIB   ?=

# Core-dump size limit (1K blocks).  Default 0 suppresses cores from
# expected child crashes (TESTS_SHRINK reducer demos).  Override with
# `make HEGEL_TEST_ULIMIT=unlimited test-local` to keep cores.
HEGEL_TEST_ULIMIT ?= 0

CC       = gcc
MPICC    = mpicc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) $(SCOTCH_INC) -funwind-tables -fexceptions
LDFLAGS      = -L$(HEGEL_C_LIB) $(SCOTCH_LIB) -lhegel_c -lscotch -lscotcherr -lz -lm -lpthread -ldl -lrt
LDFLAGS_MPI  = -L$(HEGEL_C_LIB) $(SCOTCH_LIB) -lhegel_c -lptscotch -lscotch -lptscotcherr -lscotcherr -lz -lm -lpthread -ldl -lrt

# Sequential Scotch tests
TESTS_PASS = test_graph_part test_graph_part_schema

# Reducer-quality demos: tests that are EXPECTED to fail and that
# we additionally check shrunk to a small minimal counterexample.
# Each binary must (a) exit non-zero and (b) emit a hegel_note line
# starting with "MINIMAL nvert=N nedges=M ..." where N is small.
TESTS_SHRINK = test_graph_order_shrink

# PT-Scotch (MPI) tests — use MPI_Comm_spawn, no mpiexec
TESTS_MPI  = test_dgraph_part

ALL_TESTS = $(TESTS_PASS) $(TESTS_SHRINK) $(TESTS_MPI)

.PHONY: help all clean test test-local test-deb _run-tests hegel-c-lib check-license

help:
	@echo "tests/irl/scotch Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build all test binaries (requires SCOTCH_INC / SCOTCH_LIB)"
	@echo "  test-local        Build and test with inspiration/scotch (built from source)"
	@echo "  test-deb          Build and test with system libscotch-dev"
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

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && cargo build --release; \
	fi

test_graph_part: test_graph_part.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_graph_part_schema: test_graph_part_schema.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_graph_order_shrink: test_graph_order_shrink.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Standalone investigation binary — not in TESTS_PASS.
# Reproduces a fork-mode orphan bug; see the header of the .c file.
test_array_inline_orphan_repro: test_array_inline_orphan_repro.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

test_dgraph_part: test_dgraph_part.c hegel-c-lib
	$(MPICC) $(CFLAGS) -o $@ $< $(LDFLAGS_MPI)

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
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && timeout 120 $(CURDIR)/$$t 2>&1); \
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
	for f in *.c Makefile; do \
		if ! head -3 "$$f" | grep -q "SPDX-License-Identifier: MIT"; then \
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
	rm -f $(ALL_TESTS)
