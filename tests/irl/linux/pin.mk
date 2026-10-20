# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
#
# Included by every harness Makefile under tests/irl/linux/.  Points the
# harness at its pinned kernel checkout and copies in the kernel files it
# compiles locally.  Set before including:
#
#   REPO_ROOT    the repo root
#   PIN          the pin name (pins/$(PIN).conf)
#   PIN_FILES    "<kernel path>:<local path>" pairs to copy in, e.g.
#                  lib/bch.c:bch_pinned.c
#                  include/linux/bch.h:pinned-include/linux/bch.h
#                (gitignored; recopied whenever the checkout changes)
#
# Provides: LINUX (the checkout; `make LINUX=...` overrides, which is what
# `make test-pin` in tests/irl/linux does for non-default states), HARNESS
# (its tools/testing/radix-tree), HARNESS_SUPPORT (that kernel version's
# support-object archive), PINNED (the copied files — make your objects
# depend on it), `pinned` (copy them in, nothing else) and `pinned-clean`.

LINUX ?= $(shell $(REPO_ROOT)/scripts/pin-worktree.sh linux $(PIN))
ifeq ($(strip $(LINUX)),)
  $(error cannot check out pin '$(PIN)' — see the pin-worktree error above)
endif
HARNESS         = $(LINUX)/tools/testing/radix-tree
HARNESS_SUPPORT = $(HARNESS)/libharness-support.a

PINNED :=
define pin_copy
$(2): FORCE
	@mkdir -p $$(dir $$@)
	@cmp -s $$(LINUX)/$(1) $$@ || cp $$(LINUX)/$(1) $$@
PINNED += $(2)
endef
$(foreach p,$(PIN_FILES),$(eval $(call pin_copy,$(firstword $(subst :, ,$(p))),$(lastword $(subst :, ,$(p))))))

pinned: $(PINNED)

pinned-clean:
	rm -f $(PINNED)
	rm -rf pinned-include

.PHONY: FORCE pinned pinned-clean
FORCE:
