# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# nftables IRL tests.  These must run inside the qemu/Alpine guest
# (see .qemu/) — libnftables-dev and a kernel netfilter stack are
# required.  The dry-run test does not mutate kernel state.

REPO_ROOT  = $(abspath ../../../..)
HEGEL_LIB ?= /tmp/hegel-target/release
CFLAGS    ?= -O2 -Wall -Wextra -funwind-tables -fexceptions
INCLUDES   = -I$(REPO_ROOT)
LIBS       = -L$(HEGEL_LIB) -lhegel_c -lnftables -lpthread -lm -ldl

TESTS = test_roundtrip_dryrun

# test_canonical_roundtrip is known-blocked — see the comment at the top
# of test_canonical_roundtrip.c.  Build it explicitly with `make
# test_canonical_roundtrip` if you want to investigate.
TESTS_BLOCKED = test_canonical_roundtrip

all: $(TESTS)

%: %.c
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/nft/, $(TESTS)); do \
		echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS)

.PHONY: all test clean
