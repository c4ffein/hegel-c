# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# bunzip2 bug-rediscovery scaffold for d4dba3b9c03a — get_bits has
# undefined-behavior 32-bit shifts in its bit-extraction mask.  Builds
# lib/decompress_bunzip2.c of the kernel pinned in ../pins/bunzip2.conf
# (default: the buggy state, d4dba3b9c03a^) compiled as a pre-boot
# decompressor (STATIC defined → its own malloc/free, no kernel symbols
# needed); `make test-pin PIN=bunzip2 STATE=fixed` in tests/irl/linux
# runs the fixed one.

REPO_ROOT      = $(abspath ../../../..)
PIN            = bunzip2
PIN_FILES      = lib/decompress_bunzip2.c:decompress_bunzip2_pinned.c
include ../pin.mk

INCLUDES = -I$(REPO_ROOT) \
           -I$(LINUX)/include

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -Wno-unused-function -Wno-unused-parameter \
           -Wno-implicit-function-declaration \
           -fsanitize=address -fsanitize=undefined
LDFLAGS  = -fsanitize=address -fsanitize=undefined

LIBS    = bunzip_shim.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lpthread -lm -ldl \
          -l:libbz2.so.1.0

TESTS = test_smoke test_bunzip2

all: $(TESTS)

bunzip_shim.o: bunzip_shim.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c -o $@ bunzip_shim.c

%: %.c bunzip_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/bunzip2/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t; \
	done

clean: pinned-clean
	rm -f $(TESTS) bunzip_shim.o

.PHONY: all test clean
