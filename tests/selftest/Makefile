# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/build

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) -funwind-tables -fexceptions
LDFLAGS  = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

# Core-dump size limit (in 1024-byte blocks) for test invocations.
# Defaults to 0 because TESTS_CRASH and the fork-mode parent's
# crash-isolation path produce many expected SIGSEGV/SIGABRT signals;
# their core dumps would otherwise pile up in REPO_ROOT.  Override
# with `make HEGEL_TEST_ULIMIT=unlimited test` to keep cores when
# debugging an unexpected crash.
HEGEL_TEST_ULIMIT ?= 0

# Tests that should PASS (exit 0)
TESTS_PASS = test_pass_simple \
             test_assume \
             test_result_api \
             test_suite_api \
             test_strategy_gen \
             test_span_basic \
             test_span_tree_json_passes \
             test_shrink_failure_found_while_shrinking \
             test_stateful_smoke \
             test_stateful_precond \
             test_draw_bytes_smoke \
             test_isolated_pass \
             test_collection_basic \
             test_recursion_tree \
             test_pool_basic \
             test_run_options \
             test_reproduce_blob \
             test_target \
             test_report_multiple \
             test_recursion_nary_tree \
             test_alloc_lifetime \
             test_recursive_tree \
             test_event_stats \
             test_value_draws_datetime \
             test_value_draws_net \
             test_settings \
             test_profiles \
             test_repeat_basic \
             test_grammar_parse \
             test_grammar_explicit \
             test_label_combine

# Tests that should FAIL (exit non-zero) — hegel detects the bug
TESTS_FAIL = test_shrink_boundary \
             test_shrink_overflow \
             test_shrink_square \
             test_strategy_shrink \
             test_span_tree_json_shrinks \
             test_stateful_shrink \
             test_isolated_assert \
             test_collection_shrink

# Tests that should FAIL via crash isolation (exit non-zero)
TESTS_CRASH = test_crash_segfault \
              test_isolated_crash \
              test_crash_abort \
              test_crash_stackoverflow

# Tests that should fail via a health check (an engine one such as
# filter_too_much, or hegel_health_fail) — must exit non-zero AND
# emit a recognizable "Health check failure" message to stderr.
# Stricter than the plain TESTS_FAIL category because we also
# check the message is visible to the user.
#
# The _suite variants additionally verify that the suite ran a
# passing test BEFORE the failing one (via a "ran_passes_first
# ok" marker the test prints).
TESTS_HEALTH = test_filter_too_much \
               test_filter_too_much_suite \
               test_isolated_draw_inside \
               test_recursive_misuse \
               test_event_misuse \
               test_value_draws_misuse \
               test_repeat_stale_handle \
               test_grammar_malformed

# Tests that should FAIL and report a specific shrunk result: exit
# non-zero AND print a line "shrunk: <X>" where <X> is the text after
# "EXPECTED_SHRINK:" in the test's header comment.
TESTS_SHRINK = test_recursive_shrinks \
               test_span_list_shrinks \
               test_event_shrink \
               test_value_draws_shrink \
               test_repeat_shrinks \
               test_repeat_crash_shrinks \
               test_grammar_shrinks

ALL_TESTS = $(TESTS_PASS) $(TESTS_FAIL) $(TESTS_CRASH) $(TESTS_HEALTH) $(TESTS_SHRINK)

.PHONY: FORCE help all clean test hegel-c-lib check-license

help:
	@echo "tests/selftest Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build Rust lib + all test binaries"
	@echo "  test              Build, run all tests, check SPDX headers"
	@echo "  hegel-c-lib       Build libhegel_c.a only"
	@echo "  check-license     Verify SPDX headers on all source files"
	@echo "  clean             Remove test binaries"
	@echo ""
	@echo "Variables:"
	@echo "  HEGEL_TEST_ULIMIT=N  Core-dump size limit (1K blocks) for test runs"
	@echo "                       Default 0 (suppress); set to 'unlimited' to keep cores"

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

test_%: test_%.c $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all check-license
	@passed=0; failed=0; errors=0; total=0; \
	echo ""; \
	echo "==== PASS tests (should exit 0) ===="; \
	for t in $(TESTS_PASS); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
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
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
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
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
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
	echo "==== HEALTH-CHECK tests (must exit non-zero AND emit a recognizable message) ===="; \
	for t in $(TESTS_HEALTH); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && timeout 30 $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "FAIL — expected non-zero exit but got 0"; \
			errors=$$((errors + 1)); \
		elif ! echo "$$output" | grep -q "Health check failure"; then \
			echo "FAIL — exit $$rc but no 'Health check failure' in stderr"; \
			errors=$$((errors + 1)); \
		elif echo "$$t" | grep -q "_suite$$" \
			&& ! echo "$$output" | grep -q "ran_passes_first ok"; then \
			echo "FAIL — suite did not reach passes_first marker"; \
			errors=$$((errors + 1)); \
		else \
			echo "OK (exit $$rc, message visible)"; \
			passed=$$((passed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== SHRINK tests (must exit non-zero AND print the expected shrunk result) ===="; \
	for t in $(TESTS_SHRINK); do \
		total=$$((total + 1)); \
		printf "  %-30s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		got=$$(echo "$$output" | sed -n 's/^shrunk: *//p' | head -1); \
		expected=$$(grep 'EXPECTED_SHRINK:' $(CURDIR)/$$t.c | head -1 | sed 's/.*EXPECTED_SHRINK: *//'); \
		if [ $$rc -eq 0 ]; then \
			echo "FAIL — expected non-zero exit but got 0"; \
			errors=$$((errors + 1)); \
		elif [ "$$got" != "$$expected" ]; then \
			echo "WRONG SHRINK (got '$$got', expected '$$expected')"; \
			errors=$$((errors + 1)); \
		else \
			echo "OK (shrunk: $$got)"; \
			passed=$$((passed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Results: $$passed/$$total passed, $$errors errors ===="; \
	if [ $$errors -ne 0 ]; then exit 1; fi

check-license:
	@missing=0; \
	for f in $(REPO_ROOT)/hegel_c.h \
	         $(REPO_ROOT)/core/*.c $(REPO_ROOT)/core/*.h \
	         *.c Makefile; do \
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
	rm -f $(ALL_TESTS)
