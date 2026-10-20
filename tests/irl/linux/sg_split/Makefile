# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Harness for lib/sg_split.c of the pinned kernel (../pins/), plus the
# first half of its lib/scatterlist.c for sg_next / sg_nents / etc. (see
# scatterlist_shim.c).  More targets in campaign.mk.

REPO_ROOT = $(abspath ../../../..)
PIN       = sg_split
PIN_FILES = lib/sg_split.c:sg_split_pinned.c lib/scatterlist.c:scatterlist_pinned.c \
            include/linux/scatterlist.h:pinned-include/scatterlist.h
include ../pin.mk

# include/ comes FIRST so our local stubs override the kernel headers
# for highmem / bvec / uio / folio_queue / mmzone / scatterlist.
INCLUDES = -I$(REPO_ROOT) \
           -I./include \
           -I$(LINUX)/tools/testing/shared \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE \
           -Wno-unused-function -Wno-unused-parameter \
           -Wno-implicit-function-declaration \
           -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
           -Wno-discarded-qualifiers
LDFLAGS  =

LIBS = sg_split_pinned.o scatterlist_shim.o \
       -L$(REPO_ROOT)/build \
       -lhegel_c -lpthread -lm -ldl

TESTS = test_smoke

all: $(TESTS)

# The head of the pinned scatterlist.c, verbatim, up to sg_zero_buffer's
# EXPORT_SYMBOL (see scatterlist_shim.c).
scatterlist_head_pinned.c: scatterlist_pinned.c
	@grep -q '^EXPORT_SYMBOL(sg_zero_buffer);' $< || \
	    { echo "$<: no 'EXPORT_SYMBOL(sg_zero_buffer);' line to cut at" >&2; exit 1; }
	awk '{ print } /^EXPORT_SYMBOL\(sg_zero_buffer\);/ { exit }' $< > $@

SUBSET_DEPS = scatterlist_shim.c scatterlist_head_pinned.c $(PINNED) \
              include/linux/scatterlist.h include/linux/highmem.h \
              include/linux/bvec.h include/linux/uio.h \
              include/linux/folio_queue.h include/linux/mmzone.h

scatterlist_shim.o: $(SUBSET_DEPS)
	gcc $(CFLAGS) $(INCLUDES) -c scatterlist_shim.c -o $@

sg_split_pinned.o: sg_split_pinned.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.c sg_split_pinned.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/sg_split/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) test_sg_split *.o scatterlist_head_pinned.c

.PHONY: all test clean

-include campaign.mk
