# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

HEGEL_DIR    = ..
HEGEL_C_LIB  = $(HEGEL_DIR)/rust-version/target/release
SCOTCH_LIB   = ../../build/lib

# Hegel server is resolved relative to cwd (.hegel/venv/bin/hegel).
# REPO_ROOT must point to the repo root where .hegel/ lives.
REPO_ROOT    = $(abspath ../..)

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I$(HEGEL_DIR) -funwind-tables -fexceptions
LDFLAGS  = -L$(HEGEL_C_LIB) -L$(SCOTCH_LIB) \
           -lhegel_c -lscotch -lscotcherr \
           -lpthread -lm -ldl

# Tests that should PASS (exit 0)
TESTS_PASS = test_pass_simple \
             test_assume \
             test_gen_map \
             test_gen_filter \
             test_gen_flatmap \
             test_gen_oneof \
             test_gen_optional \
             test_gen_list \
             test_gen_map_ctx \
             test_gen_composed

# Tests that should FAIL (exit non-zero) — hegel detects the bug
TESTS_FAIL = test_shrink_boundary \
             test_shrink_overflow \
             test_shrink_square

# Tests that should FAIL via crash isolation (exit non-zero)
TESTS_CRASH = test_crash_segfault \
              test_crash_abort \
              test_crash_stackoverflow

ALL_TESTS = $(TESTS_PASS) $(TESTS_FAIL) $(TESTS_CRASH)

.PHONY: all clean test hegel-c-lib check-license

all: hegel-c-lib $(ALL_TESTS)

hegel-c-lib:
	cd $(HEGEL_DIR)/rust-version && cargo build --release

test_%: test_%.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all check-license
	@passed=0; failed=0; errors=0; total=0; \
	echo ""; \
	echo "==== PASS tests (should exit 0) ===="; \
	for t in $(TESTS_PASS); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "OK"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL (exit $$rc) — expected 0"; \
			echo "    $$output" | head -5; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== FAIL tests (hegel should detect the bug, exit non-zero) ===="; \
	for t in $(TESTS_FAIL); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "OK (exit $$rc)"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL — expected non-zero exit but got 0"; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== CRASH tests (fork should catch crash, exit non-zero) ===="; \
	for t in $(TESTS_CRASH); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "OK (exit $$rc)"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL — expected non-zero exit but got 0"; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Results: $$passed/$$total passed, $$errors errors ===="; \
	if [ $$errors -ne 0 ]; then exit 1; fi

check-license:
	@missing=0; \
	for f in $(HEGEL_DIR)/hegel_c.h \
	         $(HEGEL_DIR)/rust-version/src/lib.rs \
	         *.c Makefile; do \
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
