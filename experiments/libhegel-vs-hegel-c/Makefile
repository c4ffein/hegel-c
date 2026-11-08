# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# libhegel (raw C ABI) vs hegel-c, side by side.  Not part of `make test`.
#
#   make run    LIBHEGEL_SO=/path/to/libhegel.so   build all, run all, print the table
#   make build  LIBHEGEL_SO=...                    build only
#   make loc                                       line counts only
#
# Three versions of each pair (see <pair>.md): lite_* (shortest correct
# libhegel), raw_* (careful libhegel, upstream examples' style), hc_*.
#
# Both sides run on the SAME engine file: raw_* link LIBHEGEL_SO directly
# (rpath to its directory); hc_* link hegel-c's libhegel_c.a and run with
# HEGEL_LIBHEGEL_PATH=$(LIBHEGEL_SO).
#
#   HEGEL_C_ROOT   tree whose hegel_c.h + build/libhegel_c.a are measured
#                  (default: this repo; it needs hegel_alloc and
#                  hegel_draw_recursive — run `make lib` there first)
#   SEEDS          seeds each program runs with (HEGEL_SEED), default 1..5

EXP          := $(abspath .)
REPO_ROOT    := $(abspath ../..)
HEGEL_C_ROOT ?= $(REPO_ROOT)
LIBHEGEL_SO  ?= $(REPO_ROOT)/build/libhegel.so
SEEDS        ?= 1 2 3 4 5
OUT          := build

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g -I$(EXP)
RAW_CFLAGS = $(CFLAGS) -I$(EXP)/include
HC_CFLAGS  = $(CFLAGS) -I$(HEGEL_C_ROOT)
LIBHEGEL_REAL := $(realpath $(LIBHEGEL_SO))
RAW_LDFLAGS = $(LIBHEGEL_REAL) -Wl,-rpath,$(dir $(LIBHEGEL_REAL))
HC_LDFLAGS  = -L$(HEGEL_C_ROOT)/build -lhegel_c -lpthread -lm -ldl

PAIRS   = minimal crash graph tree twobugs misuse
RAW     = $(addprefix raw_,$(PAIRS)) raw_crash_fork raw_replay
# lite_*: the shortest correct libhegel version of each pair (no shared
# helpers, no return-code checks beyond what correctness needs, no frees)
LITE    = $(addprefix lite_,$(PAIRS)) lite_replay
HC      = $(addprefix hc_,$(PAIRS)) hc_crash_iso
# hcn_*: the same hc_*.c under the nofork runner (no source change: the
# fork runner's name is redefined on the command line)
HCN     = $(addprefix hcn_,$(PAIRS))
SOURCES = $(addsuffix .c,$(LITE) $(RAW) $(HC))
SHARED  = common.h sut.h raw_check.h
LEAK_PROGS = raw_graph raw_tree hcn_graph hcn_tree

.PHONY: all build run loc leaks clean check-license FORCE

$(HEGEL_C_ROOT)/build/libhegel_c.a: FORCE
	@$(MAKE) --no-print-directory -C $(HEGEL_C_ROOT) lib

FORCE:

all: build

build: $(addprefix $(OUT)/,$(LITE) $(RAW) $(HC) $(HCN))

$(OUT)/raw_%: raw_%.c $(SHARED) include/hegel.h
	@mkdir -p $(OUT)
	$(CC) $(RAW_CFLAGS) -o $@ $< $(RAW_LDFLAGS)

$(OUT)/lite_%: lite_%.c common.h sut.h include/hegel.h
	@mkdir -p $(OUT)
	$(CC) $(RAW_CFLAGS) -o $@ $< $(RAW_LDFLAGS)

$(OUT)/hc_%: hc_%.c common.h sut.h $(HEGEL_C_ROOT)/hegel_c.h $(HEGEL_C_ROOT)/build/libhegel_c.a
	@mkdir -p $(OUT)
	$(CC) $(HC_CFLAGS) -o $@ $< $(HC_LDFLAGS)

$(OUT)/hcn_%: hc_%.c common.h sut.h $(HEGEL_C_ROOT)/hegel_c.h $(HEGEL_C_ROOT)/build/libhegel_c.a
	@mkdir -p $(OUT)
	$(CC) $(HC_CFLAGS) -Dhegel_run_test_n=hegel_run_test_nofork_n -o $@ $< $(HC_LDFLAGS)

run: build $(addprefix $(OUT)/asan/,$(LEAK_PROGS))
	@LIBHEGEL_SO=$(LIBHEGEL_REAL) OUT=$(EXP)/$(OUT) SEEDS="$(SEEDS)" sh $(EXP)/run.sh
	@$(MAKE) --no-print-directory leaks

loc:
	@sh $(EXP)/loc.sh $(SOURCES)

# Leak check of the generators that allocate (graph, tree): ASan +
# LeakSanitizer builds, hegel-c under nofork so the body's heap is the
# process's own (a fork child's leaks die with it).  hegel-c's library
# itself is not instrumented; LSan still sees every malloc.
ASAN = -fsanitize=address -fno-omit-frame-pointer

$(OUT)/asan/raw_%: raw_%.c $(SHARED) include/hegel.h
	@mkdir -p $(OUT)/asan
	$(CC) $(RAW_CFLAGS) $(ASAN) -o $@ $< $(RAW_LDFLAGS)

$(OUT)/asan/hcn_%: hc_%.c common.h sut.h $(HEGEL_C_ROOT)/hegel_c.h $(HEGEL_C_ROOT)/build/libhegel_c.a
	@mkdir -p $(OUT)/asan
	$(CC) $(HC_CFLAGS) $(ASAN) -Dhegel_run_test_n=hegel_run_test_nofork_n -o $@ $< $(HC_LDFLAGS)

leaks: $(addprefix $(OUT)/asan/,$(LEAK_PROGS))
	@echo; echo "Leak check (ASan + LSan, seed 1; hegel-c under nofork)"
	@for p in $(LEAK_PROGS); do \
	  out=$$(HEGEL_LIBHEGEL_PATH=$(LIBHEGEL_REAL) ASAN_OPTIONS=detect_leaks=1 \
	         HEGEL_SEED=1 $(OUT)/asan/$$p 2>&1); \
	  leak=$$(echo "$$out" | grep -E "^SUMMARY: AddressSanitizer" || echo "no leak reported"); \
	  printf "  %-12s %s\n" $$p "$$leak"; \
	done

check-license:
	@for f in $(SOURCES) $(SHARED) Makefile run.sh loc.sh README.md $(wildcard *.md); do \
	  grep -q "SPDX-License-Identifier[:] MIT" $$f || { echo "missing SPDX: $$f"; exit 1; }; \
	done; echo "license headers OK"

clean:
	rm -rf $(OUT)
