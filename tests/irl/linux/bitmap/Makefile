# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Bitmap parselist IRL tests.  Builds lib/bitmap-str.c (real kernel code)
# into a userspace test binary using the radix-tree harness's helper .o
# files (linux.o, slab.o etc.) plus the same _stubs.c as parman/xarray.
#
# bitmap-str.c needs lib/kstrtox.c for _parse_integer.  It also pulls in
# <linux/hex.h> (not in tools/include/) — bitmap_compat.h provides a tiny
# hex_to_bin shim.

REPO_ROOT = $(abspath ../../../..)
PIN       = bitmap
PIN_FILES = include/linux/kstrtox.h:pinned-include/linux/kstrtox.h
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

# bitmap-str.c includes <linux/hex.h>; the userspace harness has no such
# header.  bitmap_compat.h provides hex_to_bin and offset_in_page shims.
BITMAP_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                   -include bitmap_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o

LIBS    = $(HARNESS_OBJS) bitmap-str.o parse_integer.o $(HARNESS_SUPPORT) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_bitmap_parselist

all: $(TESTS)

bitmap-str.o: $(LINUX)/lib/bitmap-str.c bitmap_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(BITMAP_PRECLUDES) -c $< -o $@

parse_integer.o: parse_integer.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) bitmap-str.o parse_integer.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bitmap/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) bitmap-str.o parse_integer.o

.PHONY: all test clean
