# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# ts_kmp differential test against memmem.  Compiles real kernel
# lib/ts_kmp.c via a single-TU adapter (#include of the source) so we
# can reach its static kmp_ops directly without compiling textsearch.c
# (which would drag in rculist/rcupdate/spinlock).
#
# Reuses the radix-tree harness's _stubs (kmalloc/kfree/find_*_bit/etc.)
# and includes — the same recipe as parman.

REPO_ROOT = $(abspath ../../../..)
PIN       = ts_kmp
PIN_FILES = include/linux/textsearch.h:pinned-include/linux/textsearch.h
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
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Inject MODULE_*/THIS_MODULE shim before parsing the kernel source.
TS_KMP_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                   -include ts_kmp_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) ts_kmp_adapter.o ctype.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_ts_kmp_vs_memmem test_ts_kmp_vs_memmem_drawbytes

all: $(TESTS)

# Adapter is a single-TU compile that #include's lib/ts_kmp.c.
# We need -I. so the adapter's own includes resolve, and the precludes
# above so the kernel module macros expand to nothing.
ts_kmp_adapter.o: ts_kmp_adapter.c ts_kmp_compat.h $(LINUX)/lib/ts_kmp.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -I$(LINUX)/lib $(TS_KMP_PRECLUDES) -c ts_kmp_adapter.c -o $@

# Compile lib/ctype.c (provides the _ctype lookup table that
# tools/include/linux/ctype.h's __toupper inline references).
ctype.o: $(LINUX)/lib/ctype.c
	gcc $(CFLAGS) $(INCLUDES) -include $(LINUX)/tools/testing/shared/shared.h -c $< -o $@

%: %.c $(HARNESS_OBJS) ts_kmp_adapter.o ctype.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/ts_kmp/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) ts_kmp_adapter.o ctype.o

.PHONY: all test clean
