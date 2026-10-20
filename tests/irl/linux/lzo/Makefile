# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# LZO IRL tests.  Compiles real lib/lzo/lzo1x_compress.c and
# lib/lzo/lzo1x_decompress_safe.c into the test binary, linked against
# the radix-tree harness's helper objects (linux.o, slab.o etc.) plus
# libhegel_c.a.  Runs the round-trip property under ASAN+UBSAN.
#
# The kernel headers in linux/include leak vdso/ paths that conflict with
# host glibc; we keep a local copy of include/linux/lzo.h here.  lzo_compat.h
# is force-included before each lzo source file to bridge:
#   - glibc <endian.h> defines both __BIG_ENDIAN and __LITTLE_ENDIAN, which
#     trips lzodefs.h's "conflicting endian definitions" #error.
#   - CONFIG_X86_64 must be set for lzodefs.h to enable the 64-bit fast path.
#   - EXPORT_SYMBOL_GPL needs to be a no-op outside the kernel.

REPO_ROOT = $(abspath ../../../..)
PIN       = lzo
PIN_FILES = include/linux/lzo.h:pinned-include/linux/lzo.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I./pinned-include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib/lzo \
           -I$(URCU_INC)

LZO_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                -include lzo_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LZO_OBJS = lzo1x_compress.o lzo1x_decompress_safe.o

LIBS    = $(HARNESS_OBJS) $(LZO_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_lzo_round_trip

all: $(TESTS)

lzo1x_compress.o: $(LINUX)/lib/lzo/lzo1x_compress.c lzo_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(LZO_PRECLUDES) -c $< -o $@

lzo1x_decompress_safe.o: $(LINUX)/lib/lzo/lzo1x_decompress_safe.c lzo_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(LZO_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) $(LZO_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/lzo/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) $(LZO_OBJS)

.PHONY: all test clean
