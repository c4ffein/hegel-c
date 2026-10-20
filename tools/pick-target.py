#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Pick a Linux kernel file to point hegel-c PBT at, weighted by
# (PBT-amenability) × (attention-inverse).  Without a --seed, picks
# differ between sessions — defeats the bias of any one assistant always
# choosing the same "obvious" target (lib/maple_tree.c, lib/xarray.c).
#
# Each entry has:
#   file:     path under inspiration/targets/linux/
#   weight:   relative pick probability (higher = more likely)
#   why:      one-liner on what makes this PBT-shaped
#   property: a concrete property idea to test
#
# Weights bias toward less-fuzzed code with rich state-machine surface:
#   - Already-done targets get weight 0 (xarray)
#   - Heavily syzkaller'd code (rbtree, sort, bitmap) gets low weights
#   - Lesser-known data structures (parman, objagg, lru_cache, btree,
#     interval_tree, scatterlist, kfifo) get high weights
#   - Multi-operation, refcount, or state-machine code gets a bonus
#
# Usage:
#   tools/pick-target.py                     # one random pick
#   tools/pick-target.py --seed 42           # reproducible pick
#   tools/pick-target.py --top                # print all sorted by weight
#   tools/pick-target.py --json              # machine-readable

import argparse
import json
import os
import random
import sys

TARGETS = [
    # already-done — weight 0 keeps them in the catalog for reference
    {
        "file": "lib/xarray.c",
        "weight": 0,
        "why": "DONE — see project_xarray_pbt.md",
        "property": "xa_store/load round-trip vs shadow map",
    },

    # high-value, low-attention data structures
    {
        "file": "lib/maple_tree.c",
        "weight": 8,
        "why": "newer sparse-map (replaces rbtree in mm/), rich state machine, less syzkaller exposure than xarray",
        "property": "store/erase/find round-trip vs interval shadow; mas_walk consistency under mas_store_range",
    },
    {
        "file": "lib/parman.c",
        "weight": 0,
        "why": "DONE 2026-04-30 — see project_parman_pbt.md (600 cases, no bug; uncovered: sparse prios, late prio_init, N>16)",
        "property": "after any sequence of insert/remove with priorities, items sorted by (prio,index)",
    },
    {
        "file": "lib/objagg.c",
        "weight": 9,
        "why": "object aggregator with hierarchy graph (mlxsw), graph invariants under add/remove",
        "property": "after add/remove sequence, parent edges form a forest; refcounts match traversal",
    },
    {
        "file": "lib/lru_cache.c",
        "weight": 9,
        "why": "drbd-introduced LRU with extents, refcounts, change-events — under-tested",
        "property": "lc_get/lc_put never returns evicted-but-still-held entry; LRU order matches access order",
    },
    {
        "file": "lib/btree.c",
        "weight": 8,
        "why": "B+ tree with explicit memory pool, lookup_smallest/largest, less common than rbtree",
        "property": "btree_lookup of inserted key returns it; range queries return sorted keys",
    },
    {
        "file": "lib/interval_tree.c",
        "weight": 8,
        "why": "interval-tree augmented rbtree (mm/userfaultfd uses it); overlap queries are PBT-shaped",
        "property": "interval_tree_iter returns exactly the intervals overlapping [lo,hi]",
    },
    {
        "file": "lib/idr.c",
        "weight": 6,
        "why": "ID allocator built on xarray; cyclic alloc edge cases; same harness as xarray",
        "property": "ida_alloc/free preserves uniqueness under random alloc/free; cyclic doesn't repeat early",
    },
    {
        "file": "lib/genalloc.c",
        "weight": 7,
        "why": "general-purpose memory pool (best-fit/first-fit), bitmap-backed; allocator invariants",
        "property": "no overlap between live allocations; gen_pool_avail consistent with alloc/free history",
    },
    {
        "file": "lib/kfifo.c",
        "weight": 7,
        "why": "lockless ring-buffer; in/out pointers, wraparound, partial reads — easy to model",
        "property": "FIFO order under random in/out chunks; never reads past producer",
    },
    {
        "file": "lib/plist.c",
        "weight": 7,
        "why": "priority-sorted list (rt-mutex inheritance); add/del invariants",
        "property": "plist always sorted by prio; ties broken by insertion order",
    },
    {
        "file": "lib/llist.c",
        "weight": 6,
        "why": "lock-less linked list (cmpxchg-based); single-threaded model still tractable",
        "property": "LIFO order; llist_del_all returns exactly all currently-added entries",
    },
    {
        "file": "lib/textsearch.c",
        "weight": 6,
        "why": "textsearch state machine (KMP/BM/FSM backends); offset-correctness",
        "property": "textsearch_find returns same offset as a naïve memmem oracle for any (haystack, needle)",
    },
    {
        "file": "lib/scatterlist.c",
        "weight": 6,
        "why": "scatterlist construction/iteration; sg_chain edge cases; allocator-adjacent",
        "property": "sg_init_table + sg_set_buf + iteration sums to total length; sg_pcopy round-trips bytes",
    },
    {
        "file": "lib/iov_iter.c",
        "weight": 5,
        "why": "user/kernel IO vector iteration; bounce-buffer paths; intricate state",
        "property": "copy_to_iter then copy_from_iter round-trips for any byte sequence + iov layout",
    },
    {
        "file": "lib/asn1_decoder.c",
        "weight": 2,
        "why": "PARTIAL 2026-04-30 — primitive round-trip done; SEQUENCE/recode/tagged-encode NOT covered (the more interesting bug surface)",
        "property": "encoder.recode + length-back-patching round-trip via decoder",
    },
    {
        "file": "lib/asn1_encoder.c",
        "weight": 2,
        "why": "PARTIAL 2026-04-30 — see asn1_decoder.c (paired round-trip)",
        "property": "encoder.recode + length-back-patching round-trip via decoder",
    },
    {
        "file": "lib/bch.c",
        "weight": 0,
        "why": "DONE 2026-04-30 — 200 cases (m=14, t=4), no bug; doc-gap finding on padding bits in trailing ecc byte",
        "property": "for k random bytes + t flipped bits (t≤BCH_T), bch_decode returns the original message",
    },
    {
        "file": "lib/lzo/lzo1x_compress.c",
        "weight": 0,
        "why": "DONE 2026-04-30 — 500 cases, 1..64KiB sizes, 4 entropy patterns, no bug",
        "property": "lzo decompress(compress(x)) == x for any input ≤ buffer cap",
    },
    {
        "file": "lib/glob.c",
        "weight": 5,
        "why": "kernel glob matcher (used by tracing); pattern-matching state machine",
        "property": "glob_match agrees with a Python-fnmatch oracle on character-class + wildcard inputs",
    },
    {
        "file": "lib/dynamic_queue_limits.c",
        "weight": 7,
        "why": "BQL (Byte Queue Limits) state machine; throttle/credit accounting",
        "property": "after any (queued, completed) sequence, dql_avail never lets us exceed dql_limit",
    },
    {
        "file": "lib/ts_kmp.c",
        "weight": 5,
        "why": "Knuth–Morris–Pratt textsearch backend; algorithmic, easy to oracle against memmem",
        "property": "ts_kmp.find returns same offset as memmem for any (haystack, needle)",
    },

    # heavily-fuzzed but still has edge cases — keep low
    {
        "file": "lib/rbtree.c",
        "weight": 2,
        "why": "extensively syzkaller-fuzzed but augmented variants (rb_*_augmented) less so",
        "property": "rb_augment recompute matches subtree-walk on random insert/erase sequences",
    },
    {
        "file": "lib/sort.c",
        "weight": 1,
        "why": "very stable, but stable=true ordering edge cases possible",
        "property": "sort_r is stable when cmp returns 0; output is permutation of input; no OOB on weird sizes",
    },
    {
        "file": "lib/bitmap.c",
        "weight": 2,
        "why": "well-tested but bitmap_parselist/bitmap_print_to_pagebuf round-trip is interesting",
        "property": "parse(print(bitmap)) == bitmap for any random sparse bitmap",
    },
]


def normalize(targets):
    pool = [t for t in targets if t["weight"] > 0]
    total = sum(t["weight"] for t in pool)
    return pool, total


def pick(targets, rng):
    pool, total = normalize(targets)
    r = rng.uniform(0, total)
    acc = 0
    for t in pool:
        acc += t["weight"]
        if r <= acc:
            return t
    return pool[-1]


def main():
    p = argparse.ArgumentParser(description="Pick a Linux kernel file for hegel-c PBT")
    p.add_argument("--seed", type=int, default=None,
                   help="reproducible pick (default: os.urandom-seeded)")
    p.add_argument("--top", action="store_true",
                   help="print all targets sorted by weight (no pick)")
    p.add_argument("--json", action="store_true",
                   help="machine-readable output")
    args = p.parse_args()

    if args.top:
        ranked = sorted(TARGETS, key=lambda t: -t["weight"])
        if args.json:
            print(json.dumps(ranked, indent=2))
        else:
            for t in ranked:
                marker = "  " if t["weight"] > 0 else "x "
                print(f"{marker}[{t['weight']:>2}] {t['file']:<36} {t['why']}")
        return

    seed = args.seed
    if seed is None:
        seed = int.from_bytes(os.urandom(4), "little")
    rng = random.Random(seed)
    chosen = pick(TARGETS, rng)

    out = dict(chosen)
    out["seed"] = seed

    if args.json:
        print(json.dumps(out, indent=2))
    else:
        print(f"seed:     {seed}")
        print(f"file:     {chosen['file']}")
        print(f"weight:   {chosen['weight']}")
        print(f"why:      {chosen['why']}")
        print(f"property: {chosen['property']}")


if __name__ == "__main__":
    main()
