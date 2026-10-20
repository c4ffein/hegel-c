# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# scatterlist IRL tests.  Builds a SUBSET of lib/scatterlist.c of the
# kernel pinned in ../pins/scatterlist.conf — its DMA-agnostic first half,
# up to sg_zero_buffer — into a userspace test binary.  We carve out the
# bottom half because the iov_iter / folio_queue / xarray-folio machinery
# there pulls in 30+ unstubbed kernel symbols (struct iov_iter, bio_vec,
# ITER_*, vmalloc_to_page, xas_for_each, rcu_read_lock, etc.) that we
# don't intend to test.  This is captured in scatterlist_shim.c's leading
# comment.
#
# Friction note: the radix-tree harness recipe (compile real lib/foo.c
# in full + helper .o files) does NOT generalize to scatterlist.c.
# Surface area too large.
#
# The "subset" generation step keeps the original source byte-for-byte
# — an awk cut at a marker line, no copy-paste, no patches.

REPO_ROOT = $(abspath ../../../..)
PIN       = scatterlist
PIN_FILES = lib/scatterlist.c:scatterlist_pinned.c \
            include/linux/scatterlist.h:pinned-include/scatterlist.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

# include/ comes FIRST so our local stubs override the kernel
# headers for highmem / bvec / uio / folio_queue / mmzone / scatterlist.
INCLUDES = -I$(REPO_ROOT) \
           -I./include \
           -I$(LINUX)/tools/testing/shared \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(URCU_INC)

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE \
           -Wno-unused-function -Wno-unused-parameter \
           -Wno-implicit-function-declaration \
           -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
           -Wno-discarded-qualifiers
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB)

LIBS = scatterlist_shim.o \
       -L$(REPO_ROOT)/build \
       -lhegel_c -lpthread -lm -ldl

TESTS = test_sg_iterate_total_length

all: $(TESTS)

# The head of the pinned scatterlist.c, verbatim, up to sg_zero_buffer's
# EXPORT_SYMBOL (just before the iov_iter extract_*_to_sg block).
scatterlist_head_pinned.c: scatterlist_pinned.c
	@grep -q '^EXPORT_SYMBOL(sg_zero_buffer);' $< || \
	    { echo "$<: no 'EXPORT_SYMBOL(sg_zero_buffer);' line to cut at" >&2; exit 1; }
	awk '{ print } /^EXPORT_SYMBOL\(sg_zero_buffer\);/ { exit }' $< > $@

scatterlist_shim.o: scatterlist_shim.c scatterlist_head_pinned.c $(PINNED) \
                    include/linux/scatterlist.h \
                    include/linux/highmem.h \
                    include/linux/bvec.h \
                    include/linux/uio.h \
                    include/linux/folio_queue.h \
                    include/linux/mmzone.h
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.c scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/scatterlist/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) scatterlist_shim.o scatterlist_head_pinned.c

.PHONY: all test clean
