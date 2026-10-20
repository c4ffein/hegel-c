# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# btree IRL tests.  Builds lib/btree.c (real kernel code) into a
# userspace test binary using the radix-tree harness's helper .o files
# (linux.o, slab.o) plus local shims for mempool + MAX +
# kmem_cache_destroy (see btree_compat.h, include/linux/mempool.h).
#
# Local include/linux/btree.h is a copy of the kernel header (and
# btree-128.h, btree-type.h), so #include <linux/btree.h> resolves to
# our local copy whose mempool.h shim is in scope — without
# disturbing the kernel-tree includes for other targets.

REPO_ROOT = $(abspath ../../../..)
PIN       = btree
PIN_FILES = include/linux/btree.h:pinned-include/linux/btree.h \
            include/linux/btree-128.h:pinned-include/linux/btree-128.h \
            include/linux/btree-type.h:pinned-include/linux/btree-type.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

# -I./include comes BEFORE the tools include path so our local
# linux/btree.h, linux/mempool.h, etc. take precedence.
INCLUDES = -I$(REPO_ROOT) \
           -I./pinned-include \
           -I./include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_AUTHOR, MODULE_LICENSE no-ops)
# and btree_compat.h (gives MAX, kmem_cache_destroy, the
# module_init -> __constructor__ override).
BTREE_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                  -include btree_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) btree.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_btree_basic
NEG_TESTS = test_btree_neg

all: $(TESTS) $(NEG_TESTS)

btree.o: $(LINUX)/lib/btree.c btree_compat.h include/linux/mempool.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(BTREE_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) btree.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

# ASAN_OPTIONS=detect_leaks=0 because the radix-tree harness's
# kmem_cache parks up to ~10 freed objects per cache in an
# in-cache freelist (see btree_compat.h note on kmem_cache_destroy);
# our shim can't walk it safely, so ~640 bytes per process appear as
# a "leak".  All real allocations are tracked — keep AddressSanitizer
# overflow-detection on, only suppress LeakSanitizer.
test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/btree/, $(TESTS)); do \
	    echo "=== $$t ==="; ASAN_OPTIONS=detect_leaks=0 ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) btree.o

.PHONY: all test clean
