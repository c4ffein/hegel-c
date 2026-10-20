# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# xarray IRL tests.  Builds lib/xarray.c (real kernel code) into a
# userspace test binary using the in-tree radix-tree harness's helper
# .o files plus our own minimal stubs for the libs that harness's
# Makefile expects but doesn't actually provide.  See
# inspiration/targets/linux/tools/testing/radix-tree/_stubs.c.

REPO_ROOT      = $(abspath ../../../..)
PIN            = xarray
include ../pin.mk
URCU_PFX      ?= $(HOME)/.local/urcu_extract/usr
URCU_INC       = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB       = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/xarray.o $(HARNESS)/xarray-shared.o \
               $(HARNESS)/radix-tree.o $(HARNESS)/idr.o \
               $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_xarray_basic

all: $(TESTS)

%: %.c $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/xarray/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS)

.PHONY: all test clean
