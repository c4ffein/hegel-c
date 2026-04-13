# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) -funwind-tables -fexceptions
LDFLAGS  = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

# Per-case count for the single-test bench (override: make bench-single N=5000).
# The suite bench uses hegeltest's default count — see test_bench_suite.c.
N          ?= 1000

# Iterations and warmup for each measured run.
ITERS      ?= 5
WARMUPS    ?= 1

# Optional pinning. If taskset is available we pin to CPU 0 to reduce
# migration noise. Override with TASKSET= to disable.
TASKSET    ?= $(shell command -v taskset >/dev/null 2>&1 && echo "taskset -c 0")

BENCH_BINS = test_bench_single test_bench_single_nofork \
             test_bench_suite  test_bench_suite_nofork

.PHONY: help all clean bench bench-single bench-suite hegel-c-lib check-license

help:
	@echo "tests/bench Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build libhegel_c.a + bench binaries (fork + nofork)"
	@echo "  bench             Run bench-single and bench-suite"
	@echo "  bench-single      Time the single-test bench, fork vs nofork"
	@echo "  bench-suite       Time the suite-mode bench, fork vs nofork"
	@echo "  check-license     Verify SPDX headers on all source files"
	@echo "  clean             Remove bench binaries"
	@echo ""
	@echo "Variables (override on the command line):"
	@echo "  N=1000            Cases per run for bench-single"
	@echo "  ITERS=5           Measured iterations per variant"
	@echo "  WARMUPS=1         Warmup iterations (discarded)"
	@echo "  TASKSET='...'     Prefix used to pin runs (default: taskset -c 0 if available)"

all: hegel-c-lib $(BENCH_BINS)

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && cargo build --release; \
	fi

test_bench_single: test_bench_single.c bench_lib.h hegel-c-lib
	$(CC) $(CFLAGS) -DBENCH_N_CASES=$(N) -o $@ $< $(LDFLAGS)

test_bench_single_nofork: test_bench_single.c bench_lib.h hegel-c-lib
	$(CC) $(CFLAGS) -DBENCH_N_CASES=$(N) -DHEGEL_BENCH_NOFORK -o $@ $< $(LDFLAGS)

test_bench_suite: test_bench_suite.c bench_lib.h hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_bench_suite_nofork: test_bench_suite.c bench_lib.h hegel-c-lib
	$(CC) $(CFLAGS) -DHEGEL_BENCH_NOFORK -o $@ $< $(LDFLAGS)

# run_one NAME BIN — runs BIN once, prints the elapsed wall time in
# seconds. Uses /usr/bin/time so we get a machine-readable number
# even when the shell's builtin `time` does not cooperate.
define run_one
	@$(TASKSET) /usr/bin/env bash -c ' \
		start=$$(date +%s.%N); \
		"$(CURDIR)/$(2)" >/dev/null 2>&1; \
		rc=$$?; \
		end=$$(date +%s.%N); \
		awk -v s=$$start -v e=$$end "BEGIN { printf \"%.3f\", e - s }"; \
		if [ $$rc -ne 0 ]; then echo -n " (rc=$$rc)"; fi; \
		echo'
endef

# run_variant NAME BIN — runs WARMUPS + ITERS times, prints mean + stddev.
define run_variant
	@echo "  $(1)"
	@for i in $$(seq 1 $(WARMUPS)); do \
		printf "    warmup %d: " $$i; \
		$(TASKSET) /usr/bin/env bash -c ' \
			start=$$(date +%s.%N); \
			"$(CURDIR)/$(2)" >/dev/null 2>&1; \
			end=$$(date +%s.%N); \
			awk -v s=$$start -v e=$$end "BEGIN { printf \"%.3f\\n\", e - s }"'; \
	done
	@samples=""; \
	for i in $$(seq 1 $(ITERS)); do \
		t=$$($(TASKSET) /usr/bin/env bash -c ' \
			start=$$(date +%s.%N); \
			"$(CURDIR)/$(2)" >/dev/null 2>&1; \
			end=$$(date +%s.%N); \
			awk -v s=$$start -v e=$$end "BEGIN { printf \"%.3f\", e - s }"'); \
		printf "    run %d:    %ss\n" $$i $$t; \
		samples="$$samples $$t"; \
	done; \
	echo "$$samples" | awk '{ \
		n = NF; \
		for (i = 1; i <= n; i++) { sum += $$i; } \
		mean = sum / n; \
		for (i = 1; i <= n; i++) { d = $$i - mean; ss += d * d; } \
		stddev = (n > 1) ? sqrt(ss / (n - 1)) : 0; \
		printf "    mean:     %.3fs  stddev: %.3fs  (n=%d)\n", mean, stddev, n; \
	}'

endef

bench: bench-single bench-suite

bench-single: test_bench_single test_bench_single_nofork
	@echo ""
	@echo "==== single-test bench (N=$(N) cases, $(ITERS) iters + $(WARMUPS) warmup) ===="
	$(call run_variant,fork,test_bench_single)
	$(call run_variant,nofork,test_bench_single_nofork)

bench-suite: test_bench_suite test_bench_suite_nofork
	@echo ""
	@echo "==== suite bench (10 tests, hegeltest default cases, $(ITERS) iters + $(WARMUPS) warmup) ===="
	$(call run_variant,fork,test_bench_suite)
	$(call run_variant,nofork,test_bench_suite_nofork)

check-license:
	@missing=0; \
	for f in *.c *.h Makefile; do \
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
	rm -f $(BENCH_BINS)
