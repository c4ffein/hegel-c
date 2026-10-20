# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# parman IRL tests.  Builds lib/parman.c (real kernel code) into a
# userspace test binary using the radix-tree harness's helper .o files
# (linux.o, slab.o etc.) plus the same _stubs.c as xarray.  parman.c
# uses two list iterators absent from tools/include/linux/list.h —
# list_is_head and list_for_each_entry_from_reverse — provided in
# parman_compat.h via -include.
#
# Local include/linux/parman.h is a copy of the kernel header, kept here
# so we don't need to add -I.../include (which leaks vdso/limits.h
# conflicts).

REPO_ROOT = $(abspath ../../../..)
PIN       = parman
PIN_FILES = include/linux/parman.h:pinned-include/linux/parman.h
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

# Auto-include shared.h (gives MODULE_LICENSE, MODULE_AUTHOR, ...) and
# parman_compat.h (gives the missing list iterators) before parman.c is
# parsed.
PARMAN_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                   -include parman_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) parman.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_parman_invariant

all: $(TESTS)

parman.o: $(LINUX)/lib/parman.c parman_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(PARMAN_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) parman.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/parman/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) parman.o

.PHONY: all test clean
