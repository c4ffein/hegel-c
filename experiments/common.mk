# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# Shared build + runner for experiments/<name>/Makefile.  An experiment
# sets, before including this file:
#   EXP          its directory (absolute)
#   TEST_DIRS    directories holding its test sources
#   TESTS_PASS / TESTS_FAIL / TESTS_CRASH / TESTS_HEALTH / TESTS_SHRINK
#                as in tests/selftest and tests/from-hegel-rust
#                (SHRINK: exits non-zero and "Draw 1:" equals the
#                file's EXPECTED_SHRINK)
#   SCOTCH_TESTS (optional) Scotch tests under $(EXP)/irl/scotch, must
#                exit 0; build with SCOTCH_INC / SCOTCH_LIB set
# Each experiment compiles its own hegel_gen.c and links main's core-only
# libhegel_c.a.  Binaries go to $(EXP)/build/.

REPO_ROOT   = $(abspath $(EXP)/../..)
HEGEL_C_LIB = $(REPO_ROOT)/build
OUT         = $(EXP)/build

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I$(EXP) -I$(REPO_ROOT)
LDFLAGS  = -L$(HEGEL_C_LIB) -lhegel_c -lpthread -lm -ldl
SCOTCH_LDFLAGS = $(SCOTCH_LIB) -lscotch -lscotcherr -lz -lm -lrt

HEGEL_TEST_ULIMIT ?= 0

ALL_TESTS = $(TESTS_PASS) $(TESTS_FAIL) $(TESTS_CRASH) $(TESTS_HEALTH) $(TESTS_SHRINK)

vpath %.c $(TEST_DIRS)

.PHONY: FORCE all test scotch-test clean check-license

all: $(addprefix $(OUT)/,$(ALL_TESTS))

$(HEGEL_C_LIB)/libhegel_c.a: FORCE
	@$(MAKE) --no-print-directory -C $(REPO_ROOT) lib libhegel

FORCE:

$(OUT)/hegel_gen.o: $(EXP)/hegel_gen.c $(EXP)/hegel_gen.h $(REPO_ROOT)/hegel_c.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/test_%: test_%.c $(OUT)/hegel_gen.o $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) -o $@ $< $(OUT)/hegel_gen.o $(LDFLAGS)

$(OUT)/scotch_%: $(EXP)/irl/scotch/%.c $(OUT)/hegel_gen.o $(HEGEL_C_LIB)/libhegel_c.a
	$(CC) $(CFLAGS) $(SCOTCH_INC) -o $@ $< $(OUT)/hegel_gen.o $(LDFLAGS) $(SCOTCH_LDFLAGS)

# run CATEGORY TESTS: one line per test, totals in $(OUT)/.results
define run
	@for t in $(2); do \
		printf "  %-50s" "$$t"; \
		output=$$(cd $(REPO_ROOT) && ulimit -c $(HEGEL_TEST_ULIMIT) && timeout 300 $(OUT)/$$t 2>&1); \
		rc=$$?; ok=0; \
		case "$(1)" in \
		PASS)   [ $$rc -eq 0 ] && ok=1 ;; \
		FAIL|CRASH) [ $$rc -ne 0 ] && ok=1 ;; \
		HEALTH) [ $$rc -ne 0 ] && echo "$$output" | grep -q "Health check failure" \
			&& { ! echo "$$t" | grep -q "_suite$$" || echo "$$output" | grep -q "ran_passes_first ok"; } \
			&& ok=1 ;; \
		SHRINK) draw=$$(echo "$$output" | grep -oP 'Draw 1: \K[^ ]+' | head -1); \
			src=$$(for d in $(TEST_DIRS); do [ -f $$d/$$t.c ] && echo $$d/$$t.c; done | head -1); \
			want=$$(grep 'EXPECTED_SHRINK:' $$src | head -1 | sed 's/.*EXPECTED_SHRINK: *//'); \
			[ $$rc -ne 0 ] && [ "$$draw" = "$$want" ] && ok=1 ;; \
		esac; \
		if [ $$ok -eq 1 ]; then echo "OK ($(1), exit $$rc)"; echo ok >> $(OUT)/.results; \
		else echo "FAIL ($(1), exit $$rc)"; echo fail >> $(OUT)/.results; \
			echo "$$output" | tail -3 | sed 's/^/      /'; fi; \
	done
endef

test: all check-license
	@rm -f $(OUT)/.results; touch $(OUT)/.results
	@echo "==== $(notdir $(EXP)) ===="
	$(call run,PASS,$(TESTS_PASS))
	$(call run,FAIL,$(TESTS_FAIL))
	$(call run,CRASH,$(TESTS_CRASH))
	$(call run,HEALTH,$(TESTS_HEALTH))
	$(call run,SHRINK,$(TESTS_SHRINK))
	@ok=$$(grep -c ok $(OUT)/.results); bad=$$(grep -c fail $(OUT)/.results); \
	echo "==== $(notdir $(EXP)): $$ok/$$((ok + bad)) as expected ===="; [ $$bad -eq 0 ]

scotch-test: $(addprefix $(OUT)/scotch_,$(SCOTCH_TESTS))
	@[ -n "$(SCOTCH_INC)" ] || { echo "set SCOTCH_INC / SCOTCH_LIB (see tests/irl/scotch)" >&2; exit 2; }
	@rm -f $(OUT)/.results; touch $(OUT)/.results
	$(call run,PASS,$(addprefix scotch_,$(SCOTCH_TESTS)))
	@[ $$(grep -c fail $(OUT)/.results) -eq 0 ]

check-license:
	@missing=0; for f in $$(find $(EXP) -path $(OUT) -prune -o \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name Makefile \) -print); do \
		head -5 $$f | grep -q "SPDX-License-Identifier" || { echo "missing SPDX header: $$f"; missing=1; }; \
	done; [ $$missing -eq 0 ]

clean:
	rm -rf $(OUT)
