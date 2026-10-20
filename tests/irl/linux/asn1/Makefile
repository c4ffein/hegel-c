# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# ASN.1 encoder x decoder round-trip IRL test.  Compiles real kernel
# lib/asn1_encoder.c and lib/asn1_decoder.c into userspace, links with
# the radix-tree harness's helper objects (linux.o, slab.o) for the
# kmalloc/kfree backing and with libhegel_c.a for property testing.
#
# We keep local copies of include/linux/asn1*.h to avoid pulling in
# include/linux/limits.h's dependency on <vdso/limits.h> (same trick
# parman uses).  asn1_compat.h pulls in linux/export.h before tools/
# include/linux/module.h's stub elides EXPORT_SYMBOL.

REPO_ROOT = $(abspath ../../../..)
PIN       = asn1
PIN_FILES = include/linux/asn1.h:pinned-include/linux/asn1.h \
            include/linux/asn1_encoder.h:pinned-include/linux/asn1_encoder.h \
            include/linux/asn1_decoder.h:pinned-include/linux/asn1_decoder.h \
            include/linux/asn1_ber_bytecode.h:pinned-include/linux/asn1_ber_bytecode.h
include ../pin.mk
URCU_PFX ?= $(HOME)/.local/urcu_extract/usr
URCU_INC  = $(URCU_PFX)/include/x86_64-linux-gnu
URCU_LIB  = $(URCU_PFX)/lib/x86_64-linux-gnu

INCLUDES = -I$(REPO_ROOT) \
           -I./pinned-include \
           -I$(LINUX)/tools/testing/shared \
           -I$(HARNESS) \
           -I$(LINUX)/tools/include \
           -I$(LINUX)/tools/arch/x86/include \
           -I$(URCU_INC)

# Pre-include shared.h (MODULE_LICENSE etc.) and asn1_compat.h
# (EXPORT_SYMBOL via linux/export.h) before lib/asn1_*.c is parsed.
ASN1_PRECLUDES = -include $(LINUX)/tools/testing/shared/shared.h \
                 -include asn1_compat.h

CFLAGS  ?= -O2 -g -Wall -Wextra -funwind-tables -fexceptions \
           -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS  = -L$(URCU_LIB) -Wl,-rpath,$(URCU_LIB) \
           -fsanitize=address -fsanitize=undefined

# Helper objects from the kernel's harness — built once by `make` in HARNESS.
HARNESS_OBJS = $(HARNESS)/linux.o $(HARNESS)/slab.o $(HARNESS_SUPPORT)

LIBS    = $(HARNESS_OBJS) asn1_encoder.o asn1_decoder.o \
          -L$(REPO_ROOT)/build \
          -lhegel_c -lurcu -lpthread -lm -ldl

TESTS = test_asn1_round_trip test_asn1_sequence_round_trip

all: $(TESTS)

asn1_encoder.o: $(LINUX)/lib/asn1_encoder.c asn1_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(ASN1_PRECLUDES) -c $< -o $@

asn1_decoder.o: $(LINUX)/lib/asn1_decoder.c asn1_compat.h $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) $(ASN1_PRECLUDES) -c $< -o $@

%: %.c $(HARNESS_OBJS) asn1_encoder.o asn1_decoder.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(LDFLAGS)

test: $(TESTS)
	cd $(REPO_ROOT) && for t in $(addprefix tests/irl/linux/asn1/, $(TESTS)); do \
	    echo "=== $$t ==="; ./$$t || exit 1; \
	done

clean: pinned-clean
	rm -f $(TESTS) probe_0x83_length_divergence asn1_encoder.o asn1_decoder.o

.PHONY: all test clean
