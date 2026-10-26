# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# lib/group_cpus.c of the kernel pinned in ../pins/group_cpus.conf, built
# without CONFIG_SMP against group_cpus_compat.h (no radix-tree harness).
# The kernel file's own #includes resolve to the empty headers in
# shim-include/.

REPO_ROOT = $(abspath ../../../..)
PIN       = group_cpus
PIN_FILES = lib/group_cpus.c:group_cpus_pinned.c
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) -I./shim-include

CFLAGS  ?= -O1 -g -Wall -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined

LIBS    = group_cpus.o -L$(REPO_ROOT)/build -lhegel_c -lpthread -lm -ldl

TESTS = test_group_cpus

all: $(TESTS)

group_cpus.o: group_cpus_pinned.c group_cpus_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -include group_cpus_compat.h -c $< -o $@

%: %.c group_cpus.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/group_cpus/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) group_cpus.o

.PHONY: all test clean
