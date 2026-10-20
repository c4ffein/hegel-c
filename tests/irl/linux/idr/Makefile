# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# idr/ida IRL tests.  Builds against lib/idr.c (real kernel code,
# already compiled into idr.o by the in-tree radix-tree harness).
# Same build recipe as tests/irl/xarray/, plus we link idr.o.
# See inspiration/targets/linux/tools/testing/radix-tree/_stubs.c.

REPO_ROOT      = $(abspath ../../../..)
PIN            = idr
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

TESTS = test_ida_stateful

all: $(TESTS)

%: %.c $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/idr/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS)

.PHONY: all test clean
