# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# interval_tree IRL tests.  Builds lib/interval_tree.c + lib/rbtree.c
# (real kernel code) into a userspace test binary using the radix-tree
# harness's helper .o files (linux.o, slab.o etc.).
#
# interval_tree.c is essentially just an INTERVAL_TREE_DEFINE macro
# expansion; rbtree.c provides rb_insert_augmented_cached /
# rb_erase_augmented_cached.  We compile both as our own .o files so we
# don't depend on any pre-existing harness object beyond linux.o/slab.o.

REPO_ROOT = $(abspath ../../../..)
PIN       = interval_tree
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_LICENSE, MODULE_AUTHOR, ...) before
# kernel .c files are parsed.
KERNEL_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) interval_tree.o rbtree.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_interval_tree_basic

all: $(TESTS)

interval_tree.o: $(LINUX)/lib/interval_tree.c
	gcc $(CFLAGS) $(INCLUDES) $(KERNEL_PRECLUDES) -c $< -o $@

rbtree.o: $(LINUX)/lib/rbtree.c
	gcc $(CFLAGS) $(INCLUDES) $(KERNEL_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) interval_tree.o rbtree.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/interval_tree/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) interval_tree.o rbtree.o

.PHONY: all test clean
