# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# bch IRL tests.  Builds lib/bch.c (real kernel code) into a userspace
# test binary using the radix-tree harness's helper .o files plus a
# small local stub for the bitrev byte_rev_table that bch.c references
# via bitrev8 (when swap_bits=true).  We don't exercise swap_bits, but
# the symbol must be linkable since bitrev.h declares it extern.

REPO_ROOT = $(abspath ../../../..)
PIN       = bch
PIN_FILES = include/linux/bch.h:pinned-include/linux/bch.h include/linux/bitrev.h:pinned-include/linux/bitrev.h
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

# Auto-include shared.h so MODULE_LICENSE etc. work; bch_compat.h fills
# in EXPORT_SYMBOL_GPL and `swap` which the harness's tools/include
# headers don't provide.
BCH_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                -include bch_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) bch.o bch_stubs.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_bch_correct_under_t_errors

all: $(TESTS)

bch.o: $(LINUX)/lib/bch.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(BCH_PRECLUDES) -c $< -o $@

bch_stubs.o: bch_stubs.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) bch.o bch_stubs.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bch/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) bch.o bch_stubs.o

.PHONY: all test clean
