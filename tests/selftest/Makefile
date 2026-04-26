# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release

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
             test_gen_map \
             test_schema_filter_ints \
             test_schema_filter_floats \
             test_schema_ints \
             test_schema_floats \
             test_gen_flatmap \
             test_schema_oneof \
             test_schema_optional \
             test_schema_array_of_ints \
             test_gen_map_ctx \
             test_gen_composed \
             test_result_api \
             test_suite_api \
             test_strategy_gen \
             test_span_basic \
             test_span_tree_json_passes \
             test_schema_recursive_tree_json_roundtrip \
             test_schema_union_variant_tagged_untagged \
             test_schema_array_of_variant_struct_pointers \
             test_schema_nested_array_of_arrays_matrix \
             test_schema_array_all_composition_patterns \
             test_schema_array_inline_of_tagged_unions \
             test_schema_array_of_raw_struct_pointers_by_kind \
             test_schema_array_inline_of_bare_unions_common_prefix \
             test_schema_functional_combinators \
             test_schema_shape_get_and_reuse \
             test_schema_inline_struct \
             test_schema_facets_nonadjacent \
             test_schema_facets_reversed \
             test_schema_facets_nested \
             test_schema_facets_self_recursion \
             test_schema_top_level_draw \
             test_schema_map_of_one_of \
             test_binding_basic \
             test_binding_scope_walk \
             test_binding_arr_of \
             test_binding_jagged_2d \
             test_binding_const_length \
             test_binding_shadowing \
             test_binding_arr_shadow \
             test_binding_shrink_bag \
             test_binding_shrink_jagged \
             test_binding_shrink_shadow \
             test_binding_shrink_arr_of_structs \
             test_binding_widths \
             test_binding_let_arr \
             test_binding_use_path

# Tests that should FAIL (exit non-zero) — hegel detects the bug
TESTS_FAIL = test_shrink_boundary \
             test_shrink_overflow \
             test_shrink_square \
             test_strategy_shrink \
             test_span_tree_json_shrinks

# Tests that should FAIL via crash isolation (exit non-zero)
TESTS_CRASH = test_crash_segfault \
              test_crash_abort \
              test_crash_stackoverflow \
              test_binding_unresolved \
              test_binding_overflow \
              test_binding_width_mismatch \
              test_binding_use_at_no_iter

# Tests that should fail via a HEGEL HEALTH-CHECK panic (e.g.
# filter_too_much, large_base_example) — must exit non-zero AND
# emit a recognizable "Health check failure" message to stderr.
# Stricter than the plain TESTS_FAIL category because we also
# check the message is visible to the user.
#
# The _suite variants additionally verify that the suite ran a
# passing test BEFORE the failing one (via a "ran_passes_first
# ok" marker the test prints).
TESTS_HEALTH = test_filter_too_much \
               test_filter_too_much_suite \
               test_overflow_too_much \
               test_overflow_too_much_suite \
               test_schema_depth_health

ALL_TESTS = $(TESTS_PASS) $(TESTS_FAIL) $(TESTS_CRASH) $(TESTS_HEALTH)

.PHONY: help all clean test hegel-c-lib check-license

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

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && cargo build --release; \
	fi

test_%: test_%.c hegel-c-lib
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
	echo "==== Results: $$passed/$$total passed, $$errors errors ===="; \
	if [ $$errors -ne 0 ]; then exit 1; fi

check-license:
	@missing=0; \
	for f in $(REPO_ROOT)/hegel_c.h \
	         $(REPO_ROOT)/rust-version/src/lib.rs \
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
