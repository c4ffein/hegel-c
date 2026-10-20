# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# bootconfig bug-rediscovery harness.  Builds lib/bootconfig.c of the
# kernel pinned in ../pins/bootconfig_rediscovery.conf (default: the buggy
# state, 39ebc8d7f561^ — the commit immediately BEFORE the upstream fix
# landed); `make test-pin PIN=bootconfig_rediscovery STATE=fixed` in
# tests/irl/linux runs the fixed one.
# Mirrors the build recipe of tools/bootconfig in the kernel tree: just
# compile bootconfig.c + the test against the userspace stub header.

REPO_ROOT      = $(abspath ../../../..)
PIN            = bootconfig_rediscovery
PIN_FILES      = lib/bootconfig.c:bootconfig_pinned.c
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) \
           -I$(LINUX)/tools/bootconfig/include \
           -idirafter $(LINUX)/include

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined

LIBS    = bootconfig_buggy.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lpthread -lm -ldl

TESTS = test_bootconfig_rediscovery

all: $(TESTS)

bootconfig_buggy.o: bootconfig_pinned.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c -o $@ $<

%: %.c bootconfig_buggy.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bootconfig_rediscovery/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) test_smoke bootconfig_buggy.o

.PHONY: all test clean
