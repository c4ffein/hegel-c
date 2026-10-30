# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.
#
# experiments/schema-with-callbacks: the view-callbacks PoC
# (HEGEL_DEPENDS / HEGEL_DERIVED / HEGEL_LEN_OF / HEGEL_LEN) on top of
# the schema-with-let layer.  It is a superset: every schema-with-let
# test runs here too, against this hegel_gen.c.  See ../README.md.

EXP       = $(abspath .)
LET       = $(abspath ../schema-with-let)
TEST_DIRS = $(EXP)/tests $(LET)/tests

include $(LET)/Makefile.lists

TESTS_PASS  += test_view_depends test_view_len_of_shrink test_view_tree_json_passes \
               test_view_tree_limits test_view_nary_tree test_view_nested_draw
TESTS_FAIL  += test_view_tree_json_shrinks test_schema_tree_json_shrinks
TESTS_CRASH += test_view_read_before_drawn

# test_arch_sub_view (pin archSubSingle witness) is red by design on
# Scotch b4801dbf: run it by hand against that pin's worktree.
SCOTCH_TESTS = test_graph_part_view test_graph_part_prim

include ../common.mk
