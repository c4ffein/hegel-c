# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# xarray bug-rediscovery scaffold for c9ba5249ef8b — xas_pause() forgets
# to mask xa_index under node->shift, skipping indices on a multi-index
# entry.  Builds lib/xarray.c of the kernel pinned in
# ../pins/xarray_pause.conf (default: the buggy state, c9ba5249ef8b^);
# `make test-pin PIN=xarray_pause STATE=fixed` in tests/irl/linux runs
# the fixed one.
#
# Replaces $(HARNESS)/xarray-shared.o with a local xarray_buggy.o whose
# contents are produced from a local-shim.c that #includes xarray_pinned.c.

REPO_ROOT      = $(abspath ../../../..)
PIN            = xarray_pause
PIN_FILES      = lib/xarray.c:xarray_pinned.c
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

# Local xarray.o replaces $(HARNESS)/xarray-shared.o.  The other harness
# objects come from the pinned checkout's tools/testing/radix-tree.  The
# pin sets PIN_BUILD="" because at this kernel version the harness's own
# full build (CORE_OFILES) fails — lib/test_xarray.c there needs
# <kunit/test.h> — so only the shared objects linked here are built, by
# the rule below.
HARNESS_LIBOBJS = $(addprefix $(HARNESS)/,slab.o find_bit.o bitmap.o hweight.o vsprintf.o)
HARNESS_OBJS = $(HARNESS)/radix-tree.o $(HARNESS)/idr.o \
               $(HARNESS)/linux.o $(HARNESS_LIBOBJS)

LIBS    = xarray_buggy.o $(HARNESS_OBJS) \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_smoke

all: $(TESTS)

$(HARNESS_OBJS) &:
	C_INCLUDE_PATH="$(URCU_PFX)/include:$(URCU_INC)" $(MAKE) -C $(HARNESS) \
	    generated/map-shift.h generated/bit-length.h generated/autoconf.h \
	    $(notdir $(HARNESS_OBJS)) >/dev/null

xarray_buggy.o: local-shim.c $(PINNED) | $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -c -o $@ local-shim.c

%: %.c xarray_buggy.o $(HARNESS_OBJS)
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/xarray_pause/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) test_xas_pause xarray_buggy.o

.PHONY: all test clean
