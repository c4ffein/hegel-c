# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# BQL (Byte Queue Limits) IRL stateful PBT.
# Compiles real lib/dynamic_queue_limits.c against a tiny shim
# (bql_compat.h) that supplies jiffies/time_after_eq/cacheline.
# No dependency on the radix-tree harness — BQL has no allocations
# or RCU, so we don't need linux.o/slab.o.

REPO_ROOT = $(abspath ../../../..)
PIN       = bql
PIN_FILES = include/linux/dynamic_queue_limits.h:pinned-include/linux/dynamic_queue_limits.h
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) \
           -I. \
           -I./pinned-include \
           -I./include \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include

PRECLUDES = -include bql_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -Wno-unused-parameter -Wno-sign-compare \
           -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined

LIBS    = dql.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lpthread -lm -ldl

TESTS = test_bql_stateful

all: $(TESTS)

dql.o: $(LINUX)/lib/dynamic_queue_limits.c bql_compat.h trace/events/napi.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(PRECLUDES) -c $< -o $@

%: %.c dql.o
	gcc $(CFLAGS) $(INCLUDES) $(PRECLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bql/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) dql.o

.PHONY: all test clean
