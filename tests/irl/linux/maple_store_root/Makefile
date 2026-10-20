# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# maple_tree bug-rediscovery scaffold for 0ea120b278ad — mas_store_root
# misbehaves when storing NULL.  Builds against the kernel pinned in
# ../pins/maple_store_root.conf (default: the buggy state, 0ea120b278ad^);
# `make test-pin PIN=maple_store_root STATE=fixed` in tests/irl/linux runs
# the fixed one.

REPO_ROOT      = $(abspath ../../../..)
PIN            = maple_store_root
PIN_FILES      = lib/maple_tree.c:maple_tree_pinned.c
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

HARNESS_OBJS = $(HARNESS)/xarray-shared.o \
               $(HARNESS)/radix-tree.o $(HARNESS)/idr.o \
               $(HARNESS)/linux.o $(HARNESS)/slab.o \
               $(HARNESS_SUPPORT)

LIBS    = maple_buggy.o $(HARNESS_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_smoke

all: $(TESTS)

maple_buggy.o: local-shim.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c -o $@ local-shim.c

%: %.c maple_buggy.o $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/maple_store_root/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) maple_buggy.o

.PHONY: all test clean
