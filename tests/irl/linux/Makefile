# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Linux bug-hunt targets.  Each harness directory builds against a pinned
# kernel checkout (pins/<pin>.conf, materialised by scripts/pin-worktree.sh
# — no kernel source is committed here); this Makefile runs pins the same
# way tests/irl/scotch/ does.
#
#   make test-pin PIN=x [STATE=buggy|fixed|upstream|current]
#   make test-pins            every pin, every state it has
#   make fetch-pins           fetch + build every pin's checkouts (prefetch
#                             before going offline)

REPO_ROOT = $(abspath ../../..)
WORKTREE  = $(REPO_ROOT)/scripts/pin-worktree.sh linux
PINRUN    = $(REPO_ROOT)/scripts/pin-run.sh
PINS      = $(basename $(notdir $(wildcard pins/*.conf)))
# Undefined behaviour ends the run (the harnesses build with UBSan): a
# UB fix is rediscovered as a crash, not a line of log noise.
export PIN_RUN_ENV ?= UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
PIN      ?=
STATE    ?=

.PHONY: help test-pin test-pins fetch-pins list

help:
	@echo "tests/irl/linux Makefile"
	@echo ""
	@echo "  list              List pins"
	@echo "  test-pin PIN=x [STATE=...]"
	@echo "                    Check out the kernel at pin x's state (default: buggy, or"
	@echo "                    current for a pin with no known bug), build its harness"
	@echo "                    against it and run its tests: buggy must fail, fixed pass"
	@echo "  test-pins         test-pin for every pin and state"
	@echo "  fetch-pins        Fetch + build every pin's checkouts (then works offline)"
	@echo ""
	@echo "Pins: $(PINS)"

list:
	@$(WORKTREE) --list

test-pin:
	@[ -n "$(PIN)" ] || { echo "usage: make test-pin PIN=<pin> [STATE=...]  (pins: $(PINS))" >&2; exit 2; }
	@set -e; . pins/$(PIN).conf; \
	state="$(STATE)"; \
	[ -n "$$state" ] || state=$$($(WORKTREE) $(PIN) --states | head -1 | cut -d' ' -f1); \
	expect=$$($(WORKTREE) $(PIN) --states | awk -v s="$$state" '$$1 == s { print $$2 }'); \
	dir=$$($(WORKTREE) $(PIN) --state $$state); \
	hdir=$${PIN_DIR:-$(PIN)}; \
	echo ""; echo "==== Pin $(PIN) [$$state, expect $$expect] ===="; \
	log=$$hdir/build-pin.log; \
	$(MAKE) --no-print-directory -C $$hdir clean >/dev/null; \
	$(MAKE) --no-print-directory -C $$hdir LINUX=$$dir $$PIN_TESTS >$$log 2>&1 || \
		{ echo "  build failed (tests/irl/linux/$$log):"; grep -E "error|\*\*\*" $$log | head -20; exit 1; }; \
	bins=""; for t in $$PIN_TESTS; do bins="$$bins $(CURDIR)/$$hdir/$$t"; done; \
	PIN_RUN_ENV="$(PIN_RUN_ENV) $${PIN_ENV:-}" $(PINRUN) $$expect $$bins

test-pins:
	@errors=0; \
	for p in $(PINS); do for s in $$($(WORKTREE) $$p --states | cut -d' ' -f1); do \
		$(MAKE) --no-print-directory test-pin PIN=$$p STATE=$$s || errors=$$((errors + 1)); \
	done; done; \
	echo ""; echo "==== Pins: $$errors failing (pin, state) pair(s) ===="; \
	[ $$errors -eq 0 ]

fetch-pins:
	@for p in $(PINS); do for s in $$($(WORKTREE) $$p --states | cut -d' ' -f1); do \
		$(WORKTREE) $$p --state $$s >/dev/null || exit 1; \
	done; done; echo "all pins fetched and built"
