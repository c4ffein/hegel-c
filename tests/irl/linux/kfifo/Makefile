# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# kfifo IRL tests.  Builds lib/kfifo.c (real kernel code) into a
# userspace test binary using the radix-tree harness's helper .o files
# (linux.o, slab.o etc.) plus our local kfifo_compat.h shim and a few
# in-tree-missing headers (dma-mapping.h, scatterlist.h, uaccess.h,
# array_size.h) under ./include/linux/.
#
# Local copy of include/linux/kfifo.h here mirrors the parman pattern —
# pulling in $(LINUX)/include leaks vdso/limits.h conflicts.

REPO_ROOT = $(abspath ../../../..)
PIN       = kfifo
PIN_FILES = include/linux/kfifo.h:pinned-include/linux/kfifo.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I. \
           -I./pinned-include \
           -I./include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_LICENSE etc.) and kfifo_compat.h
# (gives the missing kmalloc_array_node/min3/NUMA_NO_NODE).
KFIFO_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                  -include kfifo_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) kfifo.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_kfifo_stateful

all: $(TESTS)

kfifo.o: $(LINUX)/lib/kfifo.c kfifo_compat.h $(PINNED) \
         include/linux/array_size.h include/linux/dma-mapping.h \
         include/linux/scatterlist.h include/linux/uaccess.h
	gcc $(CFLAGS) $(INCLUDES) $(KFIFO_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) kfifo.o
	gcc $(CFLAGS) $(INCLUDES) $(KFIFO_PRECLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/kfifo/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) kfifo.o

.PHONY: all test clean
