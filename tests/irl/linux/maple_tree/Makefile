# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# maple_tree IRL tests.  Builds against the in-tree radix-tree harness's
# helper objects (linux.o, maple.o, slab.o, etc.).  Same shape as
# tests/irl/xarray/Makefile; only differs in which harness .o is linked
# (maple.o instead of xarray.o + xarray-shared.o).
#
# maple.o is built by `make main` in the harness directory; if missing,
# this Makefile asks the harness to build first.

REPO_ROOT      = $(abspath ../../../..)
PIN            = maple_tree
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
HARNESS_OBJS = $(HARNESS)/maple.o \
               $(HARNESS)/xarray-shared.o \
               $(HARNESS)/radix-tree.o $(HARNESS)/idr.o \
               $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_maple_basic

all: $(TESTS)

%: %.c $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/maple_tree/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS)

.PHONY: all test clean
