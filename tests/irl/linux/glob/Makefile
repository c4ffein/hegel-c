# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Differential test: kernel lib/glob.c vs libc fnmatch().  glob.c is
# dependency-light (no kmalloc, no list, no rcu) so we skip the whole
# radix-tree harness — just a tiny compat header (-include) and a
# single-TU adapter that #includes the kernel source.

REPO_ROOT = $(abspath ../../../..)
PIN       = glob
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) \
           -I$(LINUX)/include \
           -I$(LINUX)/lib

GLOB_PRECLUDES = -include glob_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined

LIBS    = glob_adapter.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lpthread -lm -ldl

TESTS = test_glob_vs_fnmatch

all: $(TESTS)

glob_adapter.o: glob_adapter.c glob_compat.h $(LINUX)/lib/glob.c
	gcc $(CFLAGS) $(INCLUDES) $(GLOB_PRECLUDES) -c glob_adapter.c -o $@

%: %.c glob_adapter.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/glob/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) glob_adapter.o

.PHONY: all test clean
