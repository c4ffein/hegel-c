# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# plist IRL tests.  Builds lib/plist.c (real kernel code) into a
# userspace test binary using the radix-tree harness's helper .o files
# (linux.o, slab.o etc.) plus our local plist_compat.h shim.
#
# Local copies of include/linux/plist.h and include/linux/plist_types.h
# live under ./include/linux/ — same pattern as parman / kfifo (pulling
# in $(LINUX)/include leaks vdso/limits.h conflicts).
#
# plist_compat.h pulls in <asm/bug.h> so plist.c's `#include <linux/bug.h>`
# resolves through to the WARN_ON / WARN macros (tools/include's
# linux/bug.h only defines BUILD_BUG_ON_ZERO).

REPO_ROOT = $(abspath ../../../..)
PIN       = plist
PIN_FILES = include/linux/plist.h:pinned-include/linux/plist.h include/linux/plist_types.h:pinned-include/linux/plist_types.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I. \
           -I./pinned-include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_LICENSE etc.) and plist_compat.h
# (pulls in <asm/bug.h> for WARN_ON).
PLIST_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                  -include plist_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) plist.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_plist_stateful

all: $(TESTS)

plist.o: $(LINUX)/lib/plist.c plist_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(PLIST_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) plist.o
	gcc $(CFLAGS) $(INCLUDES) $(PLIST_PRECLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/plist/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) plist.o

.PHONY: all test clean
