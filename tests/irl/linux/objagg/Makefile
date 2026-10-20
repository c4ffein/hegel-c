# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# objagg IRL tests.  Builds lib/objagg.c (real kernel code) under
# hegel-c stateful PBT.  objagg has heavier deps than parman:
#   - rhashtable: provided by a flat-list shim under
#     ./include/linux/rhashtable.h (objagg only uses 5 fns, ~32 entries
#     per case — no need for real hashing)
#   - ida_alloc_range/ida_free/ida_init/ida_destroy: provided by
#     idr.o from the radix-tree harness
#   - sort(): no tools shim, ./objagg_compat.h maps to qsort
#   - kzalloc_objs / kzalloc_flex / bitmap_zalloc / bitmap_free:
#     newer kernel macros not in tools shim, ./objagg_compat.h provides
#   - trace_objagg_*: stubbed in ./include/trace/events/objagg.h

REPO_ROOT = $(abspath ../../../..)
PIN       = objagg
PIN_FILES = include/linux/objagg.h:pinned-include/linux/objagg.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

# Order matters: our shim include dir must come BEFORE
# tools/include so our rhashtable.h / objagg.h / trace shim wins.
INCLUDES = -I$(REPO_ROOT) \
           -I./pinned-include \
           -I./include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(LINUX)/lib \
           -I$(URCU_INC)

# Auto-include shared.h (gives MODULE_LICENSE etc.) and our compat
# (kzalloc_objs / sort / bitmap helpers) before objagg.c is parsed.
OBJAGG_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                   -include objagg_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS)/idr.o \
               $(HARNESS)/radix-tree.o $(HARNESS)/xarray-shared.o \
               $(HARNESS)/find_bit.o $(HARNESS)/bitmap.o $(HARNESS)/hweight.o \
               $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) objagg.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_objagg_invariant

all: $(TESTS)

# objagg.c gets the precludes (shared.h + objagg_compat.h) and the local
# include dir on the front of the path so our rhashtable shim wins.
objagg.o: $(LINUX)/lib/objagg.c objagg_compat.h \
          include/linux/rhashtable.h \
          include/trace/events/objagg.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(OBJAGG_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) objagg.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/objagg/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) objagg.o

.PHONY: all test clean
