# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# bch bug-rediscovery scaffold for 010d7d984650 — build_mod8_tables has
# a signed shift overflow (i << 24 with i >= 128 is UB).  Builds lib/bch.c
# of the kernel pinned in ../pins/bch_mod8.conf (default: buggy, the fix's
# parent).
# Mirrors the recipe in tests/irl/bch/.

REPO_ROOT = $(abspath ../../../..)
PIN       = bch_mod8
PIN_FILES = lib/bch.c:bch_pinned.c include/linux/bch.h:pinned-include/linux/bch.h include/linux/bitrev.h:pinned-include/linux/bitrev.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I./pinned-include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

BCH_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                -include bch_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) bch_buggy.o bch_stubs.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_smoke

all: $(TESTS)

bch_buggy.o: bch_pinned.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(BCH_PRECLUDES) -c $< -o $@

bch_stubs.o: bch_stubs.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) bch_buggy.o bch_stubs.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bch_mod8/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) bch_buggy.o bch_stubs.o

.PHONY: all test clean
