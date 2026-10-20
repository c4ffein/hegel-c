# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# IDR bug-rediscovery harness.  Builds lib/idr.c of the kernel pinned in
# ../pins/idr_rediscovery.conf (default: the buggy state, c6e8e595a079^ —
# the commit immediately BEFORE the upstream fix landed); `make test-pin
# PIN=idr_rediscovery STATE=fixed` in tests/irl/linux runs the fixed one.
# All other harness objects come from the kernel's radix-tree test
# harness, built in the pinned checkout by pin-worktree.sh.

REPO_ROOT      = $(abspath ../../../..)
PIN            = idr_rediscovery
PIN_FILES      = lib/idr.c:idr_pinned.c
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

# Local buggy idr.o replaces $(HARNESS)/idr.o.  All other objects come
# from the harness build.
HARNESS_OBJS = $(HARNESS)/xarray.o $(HARNESS)/xarray-shared.o \
               $(HARNESS)/radix-tree.o \
               $(HARNESS)/linux.o $(HARNESS)/slab.o \
               $(HARNESS_SUPPORT)

LIBS    = idr_buggy.o $(HARNESS_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_idr_rediscovery

all: $(TESTS)

idr_buggy.o: idr_pinned.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c -o $@ $<

%: %.c idr_buggy.o $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/idr_rediscovery/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) test_smoke idr_buggy.o

.PHONY: all test clean
