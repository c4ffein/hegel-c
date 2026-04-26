# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# MPI + hegel-c tests.
#
# Recommended pattern (test_mpi_spawn): hegel forks a child, the child
# calls MPI_Init (singleton), MPI_Comm_spawn to launch workers,
# MPI_Intercomm_merge for collectives.  No mpiexec needed.  Both PASS
# and FAIL/SHRINK tests work — shrinking replays cases, each replay
# spawns fresh workers.
#
# Key detail: MPI_Intercomm_merge is required — OpenMPI 5.x has issues
# with collectives on raw intercommunicators from singleton spawn, but
# merged intracommunicators work perfectly.
#
# Alternative (test_mpi_collective): mpiexec -n N, each rank runs
# independent hegel with MPI_Init inside forked children.  Works for
# PASS tests but ranks desynchronize during shrinking.
#
# Prerequisites: mpicc, mpiexec (e.g., apt install openmpi-bin libopenmpi-dev)

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release
MPI_RANKS   ?= 2

# Core-dump size limit (1K blocks).  Default 0 suppresses cores from
# expected child crashes during shrinking.  Override with
# `make HEGEL_TEST_ULIMIT=unlimited test` to keep cores when debugging.
HEGEL_TEST_ULIMIT ?= 0

CC       = mpicc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) -funwind-tables -fexceptions
LDFLAGS  = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

# Tests that need mpiexec -n N (multi-rank, fork mode)
TESTS_MPIEXEC = test_mpi_collective

# Tests that use MPI_Comm_spawn (singleton mode, no mpiexec, fork mode)
TESTS_SPAWN = test_mpi_spawn

# Shrink tests with MPI_Comm_spawn (should FAIL, exit non-zero)
TESTS_SPAWN_SHRINK = test_mpi_spawn_shrink

ALL_TESTS = $(TESTS_MPIEXEC) $(TESTS_SPAWN) $(TESTS_SPAWN_SHRINK)

.PHONY: help all clean test hegel-c-lib check-license

help:
	@echo "tests/mpi Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build all MPI test binaries"
	@echo "  test              Build and run MPI tests with mpiexec"
	@echo "  hegel-c-lib       Build libhegel_c.a only"
	@echo "  check-license     Verify SPDX headers"
	@echo "  clean             Remove test binaries"
	@echo ""
	@echo "Variables:"
	@echo "  MPI_RANKS=N          Number of MPI ranks (default: 2)"
	@echo "  HEGEL_TEST_ULIMIT=N  Core-dump size limit (1K blocks) for test runs"
	@echo "                       Default 0 (suppress); set to 'unlimited' to keep cores"
	@echo ""
	@echo "Prerequisites: mpicc, mpiexec (openmpi-bin libopenmpi-dev)"

all: hegel-c-lib $(ALL_TESTS)

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && cargo build --release; \
	fi

test_%: test_%.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all check-license
	@passed=0; errors=0; total=0; \
	echo ""; \
	echo "==== MPI mpiexec tests ($(MPI_RANKS) ranks) ===="; \
	for t in $(TESTS_MPIEXEC); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && timeout 120 mpiexec --allow-run-as-root -n $(MPI_RANKS) $(CURDIR)/$$t 2>&1); \
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
	echo "==== MPI spawn tests (singleton, no mpiexec) ===="; \
	for t in $(TESTS_SPAWN); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && OMPI_MCA_btl=tcp,self timeout 60 $(CURDIR)/$$t 2>&1); \
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
	echo "==== MPI spawn shrink tests (should FAIL, exit non-zero) ===="; \
	for t in $(TESTS_SPAWN_SHRINK); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && OMPI_MCA_btl=tcp,self timeout 120 $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
			echo "OK (exit $$rc)"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL — expected non-zero exit but got $$rc"; \
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
