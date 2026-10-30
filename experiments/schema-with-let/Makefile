# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# experiments/schema-with-let: the schema layer as main had it up to
# 3881d33 (HEGEL_STRUCT, LET / USE bindings, unions, arrays, SELF).
# See ../README.md.  `make test`; `make scotch-test SCOTCH_INC=... SCOTCH_LIB=...`.

EXP       = $(abspath .)
TEST_DIRS = $(EXP)/tests

include $(EXP)/Makefile.lists

# test_graph_order_shrink (pin hgraphOrderCp witness) is red by design on
# Scotch before 064292127f: run it by hand against that pin's worktree.
# test_array_inline_orphan_repro is a hand-run historical demo.
SCOTCH_TESTS = test_graph_part_schema

include ../common.mk
