# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# llist IRL tests.  Builds lib/llist.c (real kernel code) into a
# userspace test binary using the radix-tree harness's helper .o files
# (linux.o, slab.o etc.) plus our local llist_compat.h shim and a local
# copy of <linux/llist.h>.
#
# SINGLE-THREADED MODEL: llist's cmpxchg / xchg paths are exercised
# trivially (no contention).  We test LIFO order, list traversal,
# del_first / del_all, llist_empty, llist_reverse_order — NOT
# concurrency.  See llist_compat.h.

REPO_ROOT = $(abspath ../../../..)
PIN       = llist
PIN_FILES = include/linux/llist.h:pinned-include/linux/llist.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I. \
           -I./pinned-include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_LICENSE etc.) and llist_compat.h
# (try_cmpxchg / xchg single-thread shims, WARN_ON via asm/bug.h).
LLIST_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                  -include llist_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) llist.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_llist_stateful

all: $(TESTS)

llist.o: $(LINUX)/lib/llist.c llist_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(LLIST_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) llist.o
	gcc $(CFLAGS) $(INCLUDES) $(LLIST_PRECLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/llist/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) llist.o

.PHONY: all test clean
