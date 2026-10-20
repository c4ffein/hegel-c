# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# sg_split bug-rediscovery scaffold for 8b46fdaea819 — sg_split_phys
# reset the offset on every entry except the first, losing the original
# page offsets of subsequent scatterlist entries.  Builds lib/sg_split.c
# of the kernel pinned in ../pins/sg_split.conf (default: buggy, the
# fix's parent) plus the first half of its lib/scatterlist.c for
# sg_next / sg_nents / etc. (see scatterlist_shim.c).
#
# The campaign targets below (contract, cases, acid, ladder, repro) build
# our candidate variants of lib/sg_split.c: v7.1 + ../pins/sg_split-<v>.patch,
# v2 = the sg_split_v2 pin's fixed state, current = v7.1, buggy = the
# sg_split pin's buggy state.
#
# Included by ./Makefile.

# Kernel trees for the variants (resolved once, on first use).
WORKTREE  = $(REPO_ROOT)/scripts/pin-worktree.sh linux
V_BASE    = $(eval V_BASE := $$(shell $(WORKTREE) sg_split_v2 --state buggy))$(V_BASE)
V_V2      = $(eval V_V2 := $$(shell $(WORKTREE) sg_split_v2 --state fixed))$(V_V2)
V_BUGGY   = $(eval V_BUGGY := $$(shell $(WORKTREE) sg_split --state buggy))$(V_BUGGY)

# Variant sources (see the header).
sg_split_current_pinned.c: FORCE
	@cmp -s $(V_BASE)/lib/sg_split.c $@ || cp $(V_BASE)/lib/sg_split.c $@
sg_split_v2_pinned.c: FORCE
	@cmp -s $(V_V2)/lib/sg_split.c $@ || cp $(V_V2)/lib/sg_split.c $@
sg_split_buggy_pinned.c: FORCE
	@cmp -s $(V_BUGGY)/lib/sg_split.c $@ || cp $(V_BUGGY)/lib/sg_split.c $@
sg_split_%_pinned.c: ../pins/sg_split-%.patch
	patch -s -o $@ $(V_BASE)/lib/sg_split.c $<

sg_split_%.o: sg_split_%_pinned.c $(PINNED)
	gcc $(CFLAGS) $(INCLUDES) -c $< -o $@

# ---------------------------------------------------------------------------
# Contract-boundary hegel test (test_sg_split_contract.c): sizes drawn from
# [0, budget + slack] — zeros AND infeasible over-requests allowed — against
# the v2-fixed source.  Property: infeasible => rc != 0; feasible => rc == 0
# + per-split length/byte oracles.  FAILS on v2 (finds bug 4, the
# zero-at-exhaustion silent success); gates green once the bug-4 fix lands.
# ---------------------------------------------------------------------------

HEGEL_LIBS = scatterlist_shim.o -L$(REPO_ROOT)/build -lhegel_c -lpthread -lm -ldl

test_sg_split_contract: test_sg_split_contract.c sg_split_v2.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_v2.o $(HEGEL_LIBS) $(LDFLAGS)

# v3 candidate (-EINVAL on zero sizes) under the strict-semantics contract.
test_sg_split_contract_einval: test_sg_split_contract.c sg_split_v3.o scatterlist_shim.o
	gcc $(CFLAGS) -DSG_SPLIT_SEMANTICS_EINVAL $(INCLUDES) -o $@ $< sg_split_v3.o $(HEGEL_LIBS) $(LDFLAGS)

# v3b comparison variant (tolerate + remaining-sizes-zero walk) under tolerate semantics.
test_sg_split_contract_tolerate: test_sg_split_contract.c sg_split_v3b.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_v3b.o $(HEGEL_LIBS) $(LDFLAGS)

# Red control: v2 (no bug-4 fix) under tolerate semantics — must FAIL.
contract: test_sg_split_contract
	cd $(REPO_ROOT) && ./tests/irl/linux/sg_split/test_sg_split_contract

# Green deliverables: both v3 variants must pass their respective contracts.
contract-v3: test_sg_split_contract_einval test_sg_split_contract_tolerate
	@rc=0; cd $(REPO_ROOT) && for t in test_sg_split_contract_einval test_sg_split_contract_tolerate; do \
	    echo "=== $$t ==="; ./tests/irl/linux/sg_split/$$t || rc=1; echo; \
	done; exit $$rc

# ---------------------------------------------------------------------------
# Deterministic case suite (test_sg_split_cases.c).  Calls sg_split directly
# (no hegel) so we can build it under ASAN/UBSAN, against either the v2
# candidate (all fixes) or the current upstream (buggy) source, and in either
# arch model: default !NEED_SG_DMA_LENGTH, or NEED via -DCONFIG_NEED_SG_DMA_LENGTH.
# ---------------------------------------------------------------------------
SAN      = -fsanitize=address,undefined -fno-omit-frame-pointer -g
NEEDFLAG = -DCONFIG_NEED_SG_DMA_LENGTH

# ASAN objects, !NEED and NEED models
scatterlist_shim.san.o: $(SUBSET_DEPS)
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -c scatterlist_shim.c -o $@
scatterlist_shim.need.san.o: $(SUBSET_DEPS)
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -c scatterlist_shim.c -o $@
sg_split_pinned.san.o: sg_split_pinned.c $(PINNED)
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -c $< -o $@
sg_split_pinned.need.san.o: sg_split_pinned.c $(PINNED)
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -c $< -o $@
sg_split_%.san.o: sg_split_%_pinned.c $(PINNED)
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -c $< -o $@
sg_split_%.need.san.o: sg_split_%_pinned.c $(PINNED)
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -c $< -o $@

# The case suite against the pinned lib/sg_split.c (this checkout's state).
test_cases: test_sg_split_cases.c sg_split_pinned.san.o scatterlist_shim.san.o
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -o $@ $^
test_cases_need: test_sg_split_cases.c sg_split_pinned.need.san.o scatterlist_shim.need.san.o
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -o $@ $^

test_cases_v2: test_sg_split_cases.c sg_split_v2.san.o scatterlist_shim.san.o
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -o $@ $^
test_cases_v2_need: test_sg_split_cases.c sg_split_v2.need.san.o scatterlist_shim.need.san.o
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -o $@ $^
test_cases_current: test_sg_split_cases.c sg_split_current.san.o scatterlist_shim.san.o
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -o $@ $^
test_cases_current_need: test_sg_split_cases.c sg_split_current.need.san.o scatterlist_shim.need.san.o
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -o $@ $^
test_cases_bugc: test_sg_split_cases.c sg_split_bugc.san.o scatterlist_shim.san.o
	gcc $(CFLAGS) $(SAN) $(INCLUDES) -o $@ $^
test_cases_bugc_need: test_sg_split_cases.c sg_split_bugc.need.san.o scatterlist_shim.need.san.o
	gcc $(CFLAGS) $(SAN) $(NEEDFLAG) $(INCLUDES) -o $@ $^

CASES_V2 = test_cases_v2 test_cases_v2_need

# Green deliverable: v2 must pass every case in both arch models.
cases: $(CASES_V2)
	@rc=0; for t in $(CASES_V2); do \
	    echo "=== $$t ==="; ./$$t || rc=1; echo; \
	done; exit $$rc

# Contrast: run the buggy upstream source.  EXPECTED to fail/abort (t8b
# ZERO_SIZE_PTR OOB caught by ASAN at sg_split_phys()).  Never gates `make cases`.
cases-contrast: test_cases_current test_cases_current_need
	-@for t in test_cases_current test_cases_current_need; do \
	    echo "=== $$t (expected to FAIL/ABORT) ==="; ./$$t; echo "exit=$$?"; echo; \
	done

# Bug-C isolation: overshoot + OOB guard applied, but NOT the bug-C ->length
# fix (== the scratch-v2 tree state).  t8b no longer crashes, so the silent
# length corruption is observable: on !NEED the unmapped multi-entry splits
# FAIL while their /mapped variants PASS (the mapped pass repairs ->length),
# and on NEED everything PASSES (separate dma_length field => immune).
cases-bugc: test_cases_bugc test_cases_bugc_need \
	      sg_split_v3.o sg_split_v3b.o test_sg_split_contract_einval test_sg_split_contract_tolerate
	-@for t in test_cases_bugc test_cases_bugc_need; do \
	    echo "=== $$t ==="; ./$$t; echo "exit=$$?"; echo; \
	done

campaign-clean:
	rm -f sg_split_*_pinned.c \
	      test_sg_split_contract test_sg_split_contract_einval test_sg_split_contract_tolerate \
	      test_cases test_cases_need test_cases_v2 test_cases_v2_need \
	      test_cases_current test_cases_current_need \
	      test_cases_bugc test_cases_bugc_need full_* ladder_*

clean: campaign-clean

.PHONY: cases cases-contrast cases-bugc contract contract-v3 campaign-clean

# ---------------------------------------------------------------------------
# COMPREHENSIVE contract suite (test_sg_split_contract_full.c) + acid matrix:
# must be GREEN on the v4 candidate (all five fixes) and RED, with shrunk
# counterexamples, on every historical variant.  Old variants run under the
# tolerate contract (their own semantics); v3/v4 under -EINVAL.
# ---------------------------------------------------------------------------

full_v4_einval: test_sg_split_contract_full.c sg_split_v4.o scatterlist_shim.o
	gcc $(CFLAGS) -DSG_SPLIT_SEMANTICS_EINVAL $(INCLUDES) -o $@ $< sg_split_v4.o $(HEGEL_LIBS) $(LDFLAGS)
full_v4a_einval: test_sg_split_contract_full.c sg_split_v4a.o scatterlist_shim.o
	gcc $(CFLAGS) -DSG_SPLIT_SEMANTICS_EINVAL $(INCLUDES) -o $@ $< sg_split_v4a.o $(HEGEL_LIBS) $(LDFLAGS)
full_v3_einval: test_sg_split_contract_full.c sg_split_v3.o scatterlist_shim.o
	gcc $(CFLAGS) -DSG_SPLIT_SEMANTICS_EINVAL $(INCLUDES) -o $@ $< sg_split_v3.o $(HEGEL_LIBS) $(LDFLAGS)
full_v2_tol: test_sg_split_contract_full.c sg_split_v2.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_v2.o $(HEGEL_LIBS) $(LDFLAGS)
full_bugc_tol: test_sg_split_contract_full.c sg_split_bugc.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_bugc.o $(HEGEL_LIBS) $(LDFLAGS)
full_current_tol: test_sg_split_contract_full.c sg_split_current.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_current.o $(HEGEL_LIBS) $(LDFLAGS)
full_buggy_tol: test_sg_split_contract_full.c sg_split_buggy.o scatterlist_shim.o
	gcc $(CFLAGS) $(INCLUDES) -o $@ $< sg_split_buggy.o $(HEGEL_LIBS) $(LDFLAGS)

ACID_BINS = full_v4a_einval full_v4_einval full_v3_einval full_v2_tol full_bugc_tol full_current_tol full_buggy_tol

acid: $(ACID_BINS)
	@cd $(REPO_ROOT) && rc=0; \
	for spec in full_v4a_einval:GREEN full_v4_einval:RED full_v3_einval:RED full_v2_tol:RED \
	            full_bugc_tol:RED full_current_tol:RED full_buggy_tol:RED; do \
	    t=$${spec%%:*}; want=$${spec##*:}; \
	    ./tests/irl/linux/sg_split/$$t > /tmp/acid_$$t.log 2>&1; got=$$?; \
	    if [ "$$want" = GREEN ] && [ $$got -eq 0 ]; then verdict="OK (green)"; \
	    elif [ "$$want" = RED ] && [ $$got -ne 0 ]; then verdict="OK (red)"; \
	    else verdict="ACID FAIL (wanted $$want, exit $$got)"; rc=1; fi; \
	    echo "$$t: $$verdict"; \
	    grep -m1 'Property test failed' /tmp/acid_$$t.log | head -c 160; echo; \
	done; exit $$rc

.PHONY: acid

# Standalone repros for the two boundary findings (see file headers).
# Run each against any variant: make repro VARIANT=sg_split_v4a
VARIANT ?= sg_split_v4a
repro: $(VARIANT).o scatterlist_shim.o
	@for r in repro_skip_boundary repro_midzero; do \
	    gcc $(CFLAGS) $(INCLUDES) -o /tmp/$$r $$r.c $(VARIANT).o scatterlist_shim.o; \
	    echo "=== $$r vs $(VARIANT) ==="; (cd $(REPO_ROOT) && /tmp/$$r); echo; \
	done

.PHONY: repro

# ---------------------------------------------------------------------------
# LADDER: every historical variant judged under the ONE FINAL contract
# (-DSG_SPLIT_SEMANTICS_EINVAL + shape properties).  Only the fully-fixed
# version may pass; every earlier one must be RED, each for its own reason.
# This is the strongest statement of "the final fix, and nothing less, is
# correct" the suite can make.
# ---------------------------------------------------------------------------
LADDER_VARIANTS = sg_split_buggy sg_split_current sg_split_bugc sg_split_v2 \
                  sg_split_v3 sg_split_v4 sg_split_v4a

ladder: $(addsuffix .o,$(LADDER_VARIANTS)) scatterlist_shim.o
	@rc=0; \
	for v in $(LADDER_VARIANTS); do \
	    if ! gcc $(CFLAGS) -DSG_SPLIT_SEMANTICS_EINVAL $(INCLUDES) \
	        -o ladder_$$v test_sg_split_contract_full.c \
	        $$v.o scatterlist_shim.o \
	        -L$(REPO_ROOT)/build -lhegel_c -lpthread -lm -ldl 2>/tmp/ladder_$$v.build; then \
	        echo "BUILD FAILED for $$v:"; cat /tmp/ladder_$$v.build; rc=1; continue; fi; \
	    (cd $(REPO_ROOT) && ./tests/irl/linux/sg_split/ladder_$$v) > /tmp/ladder_$$v.log 2>&1; got=$$?; \
	    if [ "$$v" = sg_split_v4a ]; then want=0; label=GREEN; else want=1; label=RED; fi; \
	    if { [ $$want -eq 0 ] && [ $$got -eq 0 ]; } || { [ $$want -eq 1 ] && [ $$got -ne 0 ]; }; then tag="OK"; else tag="LADDER FAIL"; rc=1; fi; \
	    reason=$$(grep -m1 'Property test failed' /tmp/ladder_$$v.log | sed 's/Property test failed: //' | head -c 88); \
	    printf "%-20s want=%-5s got=%s  [%s]  %s\n" "$$v" "$$label" "$$([ $$got -eq 0 ] && echo GREEN || echo RED)" "$$tag" "$$reason"; \
	done; echo; [ $$rc -eq 0 ] && echo "LADDER OK: only sg_split_v4a passes the final contract" || echo "LADDER BROKEN"; exit $$rc

.PHONY: ladder
