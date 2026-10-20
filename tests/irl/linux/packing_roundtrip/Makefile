# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Round-trip PBT pilot for lib/packing.c (pack/unpack). Self-contained: the
# pinned lib/packing.c is compiled with a local compat.h shim standing in for
# the generic kernel headers — no radix-tree harness, no urcu.

REPO_ROOT = $(abspath ../../../..)
PIN       = packing_roundtrip
PIN_FILES = lib/packing.c:packing_pinned.c
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) -I.
CFLAGS  ?= -O2 -g -Wall -Wextra -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined
LIBS     = -L$(REPO_ROOT)/build -lhegel_c -lpthread -lm -ldl

TESTS = test_roundtrip

all: $(TESTS)

packing_pinned.o: packing_pinned.c compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -I$(LINUX)/include -include compat.h -c -o $@ $<

%: %.c packing_pinned.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< packing_pinned.o $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(TESTS); do \
	    echo "=== $$t ==="; ./tests/irl/linux/packing_roundtrip/$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) packing_pinned.o

.PHONY: all test clean
