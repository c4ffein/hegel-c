# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# C equivalents of hegel-rust tests.
# Each test here corresponds 1-1 to a test in inspiration/hegel-rust/tests/.
# See manifest.md for the full correspondence map.

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release
CC           = gcc
CFLAGS       = -Wall -Wextra -O2 -I$(REPO_ROOT) -funwind-tables -fexceptions
LDFLAGS      = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

# Tests that verify all examples satisfy a predicate (should PASS, exit 0)
TESTS_PASS = test_integers_i32_bounds \
             test_integers_i64_bounds \
             test_integers_u64_bounds \
             test_combinators_filter \
             test_combinators_one_of \
             test_combinators_optional \
             test_combinators_flat_map \
             test_combinators_map

# Tests that verify shrink quality (should FAIL, exit non-zero, check shrunk value)
TESTS_SHRINK = test_shrink_int_above_13 \
               test_shrink_int_to_zero \
               test_shrink_boundary_100

ALL_TESTS = $(TESTS_PASS) $(TESTS_SHRINK)

.PHONY: all clean test report verify hegel-c-lib

all: hegel-c-lib $(ALL_TESTS)

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && $(HOME)/.cargo/bin/cargo build --release; \
	fi

test_%: test_%.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all
	@passed=0; failed=0; total=0; \
	echo ""; \
	echo "==== PASS tests (all examples satisfy predicate) ===="; \
	for t in $(TESTS_PASS); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "OK"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL (exit $$rc)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== SHRINK tests (verify minimal counterexample) ===="; \
	for t in $(TESTS_SHRINK); do \
		total=$$((total + 1)); \
		printf "  %-35s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			draw=$$(echo "$$output" | grep -oP 'Draw 1: \K[^ ]+' | head -1); \
			expected=$$(grep 'EXPECTED_SHRINK:' $(CURDIR)/$$t.c | head -1 | sed 's/.*EXPECTED_SHRINK: *//'); \
			if [ "$$draw" = "$$expected" ]; then \
				echo "OK (shrunk to $$draw)"; \
				passed=$$((passed + 1)); \
			else \
				echo "WRONG SHRINK (got $$draw, expected $$expected)"; \
				failed=$$((failed + 1)); \
			fi; \
		else \
			echo "FAIL — expected non-zero exit but got 0"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Results: $$passed/$$total passed, $$failed errors ===="; \
	if [ $$failed -ne 0 ]; then exit 1; fi

report:
	@echo "# hegel-rust → hegel-c Test Correspondence Report" > $(CURDIR)/report.md; \
	echo "" >> $(CURDIR)/report.md; \
	echo "Generated: $$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> $(CURDIR)/report.md; \
	echo "" >> $(CURDIR)/report.md; \
	echo "## Results" >> $(CURDIR)/report.md; \
	echo "" >> $(CURDIR)/report.md; \
	echo "| C test | Rust source | Status |" >> $(CURDIR)/report.md; \
	echo "|--------|-------------|--------|" >> $(CURDIR)/report.md; \
	for t in $(TESTS_PASS); do \
		rust_src=$$(grep 'RUST_SOURCE:' $(CURDIR)/$$t.c | head -1 | sed 's/.*RUST_SOURCE: *//'); \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "| $$t | $$rust_src | PASS |" >> $(CURDIR)/report.md; \
		else \
			echo "| $$t | $$rust_src | **FAIL** (exit $$rc) |" >> $(CURDIR)/report.md; \
		fi; \
	done; \
	for t in $(TESTS_SHRINK); do \
		rust_src=$$(grep 'RUST_SOURCE:' $(CURDIR)/$$t.c | head -1 | sed 's/.*RUST_SOURCE: *//'); \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		draw=$$(echo "$$output" | grep -oP 'Draw 1: \K[^ ]+' | head -1); \
		expected=$$(grep 'EXPECTED_SHRINK:' $(CURDIR)/$$t.c | head -1 | sed 's/.*EXPECTED_SHRINK: *//'); \
		if [ $$rc -ne 0 ] && [ "$$draw" = "$$expected" ]; then \
			echo "| $$t | $$rust_src | PASS (shrunk→$$draw) |" >> $(CURDIR)/report.md; \
		else \
			echo "| $$t | $$rust_src | **FAIL** (got $$draw, want $$expected) |" >> $(CURDIR)/report.md; \
		fi; \
	done; \
	echo "" >> $(CURDIR)/report.md; \
	echo "## Not yet ported" >> $(CURDIR)/report.md; \
	echo "" >> $(CURDIR)/report.md; \
	cat $(CURDIR)/manifest.md | grep '^\- \[ \]' >> $(CURDIR)/report.md 2>/dev/null || true; \
	echo ""; \
	echo "Report written to $(CURDIR)/report.md"; \
	cat $(CURDIR)/report.md

verify:
	@if [ ! -d "$(REPO_ROOT)/inspiration/hegel-rust" ]; then \
		echo "ERROR: Run 'make inspiration' from repo root first."; \
		exit 1; \
	fi; \
	prompt=$$(cat $(CURDIR)/VERIFY_PROMPT.md); \
	passed=0; failed=0; total=0; \
	echo "# Equivalence Verification Report" > $(CURDIR)/verify-report.md; \
	echo "" >> $(CURDIR)/verify-report.md; \
	echo "Generated: $$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> $(CURDIR)/verify-report.md; \
	echo "" >> $(CURDIR)/verify-report.md; \
	echo "| C test | Verdict |" >> $(CURDIR)/verify-report.md; \
	echo "|--------|---------|" >> $(CURDIR)/verify-report.md; \
	for f in test_*.c; do \
		total=$$((total + 1)); \
		name=$$(basename $$f .c); \
		printf "  %-35s" "$$name"; \
		rust_src=$$(grep 'RUST_SOURCE:' $$f | head -1 | sed 's/.*RUST_SOURCE: *//'); \
		rust_file=$$(echo "$$rust_src" | sed 's/::.*//' ); \
		rust_fn=$$(echo "$$rust_src" | sed 's/.*:://'); \
		verdict=$$(cd $(REPO_ROOT) && claude -p --model haiku \
			--system-prompt "$$prompt" \
			--allowedTools "Read,Glob,Grep" \
			--dangerously-skip-permissions \
			"Verify this C test. C file: tests/from-hegel-rust/$$f — Rust source: inspiration/hegel-rust/$$rust_src" \
			2>/dev/null | grep '^VERDICT:' | head -1); \
		if [ -z "$$verdict" ]; then \
			verdict="VERDICT: ERROR — no verdict returned"; \
		fi; \
		echo "$$verdict"; \
		echo "| $$name | $$verdict |" >> $(CURDIR)/verify-report.md; \
		case "$$verdict" in \
			*MISMATCH*|*ERROR*) failed=$$((failed + 1)) ;; \
			*MATCH*) passed=$$((passed + 1)) ;; \
			*) failed=$$((failed + 1)) ;; \
		esac; \
	done; \
	echo ""; \
	echo "==== Verification: $$passed/$$total OK, $$failed issues ===="; \
	echo "" >> $(CURDIR)/verify-report.md; \
	echo "**$$passed/$$total OK, $$failed issues**" >> $(CURDIR)/verify-report.md; \
	echo ""; \
	echo "Report written to $(CURDIR)/verify-report.md"; \
	if [ $$failed -ne 0 ]; then exit 1; fi

clean:
	rm -f $(ALL_TESTS) report.md verify-report.md
