# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# tests/docs/ — doc-dedicated tests.
#
# This directory hosts tests that exist purely to back
# documentation snippets (via the /include transclusion mechanism
# in scripts/check_doc_includes.py).  Content that already fits in
# tests/selftest/ or another existing test location stays there and
# is transcluded from its original location; only snippets with no
# natural home outside the docs belong here.
#
# Each .c file must:
#   - carry the SPDX header
#   - compile against libhegel_c.a
#   - exit 0 when run (demonstrations, not bug hunts)

REPO_ROOT    = $(abspath ../..)
HEGEL_C_LIB  = $(REPO_ROOT)/rust-version/target/release

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I$(REPO_ROOT) -funwind-tables -fexceptions
LDFLAGS  = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl

# Core-dump size limit (1K blocks).  Default 0 keeps the repo root
# clean of cores from any unexpected child crashes.  Override with
# `make HEGEL_TEST_ULIMIT=unlimited test` to keep cores when debugging.
HEGEL_TEST_ULIMIT ?= 0

TESTS = $(basename $(notdir $(wildcard $(CURDIR)/test_*.c)))

.PHONY: help all clean test hegel-c-lib check-license

help:
	@echo "tests/docs Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help              Show this help (default)"
	@echo "  all               Build all test binaries"
	@echo "  test              Build, run all tests (each must exit 0)"
	@echo "  hegel-c-lib       Build libhegel_c.a only"
	@echo "  check-license     Verify SPDX headers"
	@echo "  clean             Remove test binaries"
	@echo ""
	@echo "Variables:"
	@echo "  HEGEL_TEST_ULIMIT=N  Core-dump size limit (1K blocks) for test runs"
	@echo "                       Default 0 (suppress); set to 'unlimited' to keep cores"

all: hegel-c-lib $(TESTS)

hegel-c-lib:
	@if [ ! -f "$(HEGEL_C_LIB)/libhegel_c.a" ]; then \
		echo "Building libhegel_c.a..."; \
		cd $(REPO_ROOT)/rust-version && cargo build --release; \
	fi

test_%: test_%.c hegel-c-lib
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all check-license
	@if [ -z "$(TESTS)" ]; then \
		echo "(no tests/docs/*.c yet — nothing to run)"; \
		exit 0; \
	fi; \
	passed=0; errors=0; total=0; \
	echo ""; \
	echo "==== docs tests (must exit 0) ===="; \
	for t in $(TESTS); do \
		total=$$((total + 1)); \
		printf "  %-40s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && $(CURDIR)/$$t 2>&1); \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			echo "OK"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL (exit $$rc)"; \
			echo "    $$output" | head -5; \
			errors=$$((errors + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "==== Results: $$passed/$$total passed ===="; \
	if [ $$errors -ne 0 ]; then exit 1; fi

check-license:
	@if [ -z "$(wildcard $(CURDIR)/test_*.c)" ]; then \
		head -3 Makefile | grep -q "SPDX-License-Identifier: MIT"; \
		exit $$?; \
	fi; \
	missing=0; \
	for f in $(wildcard $(CURDIR)/test_*.c) Makefile; do \
		if ! head -3 "$$f" | grep -q "SPDX-License-Identifier: MIT"; then \
			echo "  MISSING: $$f"; \
			missing=$$((missing + 1)); \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then exit 1; fi; \
	echo "All files have SPDX headers."

clean:
	rm -f $(TESTS)
