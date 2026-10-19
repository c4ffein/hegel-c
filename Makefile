# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

INSPIRATION_DIR = inspiration

# Sister hegel bindings + the canonical Agent Skill — `inspiration/hegel/`.
# NOTE: hegel-rust is NOT here — it is the one build-critical dependency
# and lives as a pinned submodule at deps/hegel-rust.  `make inspiration`
# symlinks it under inspiration/hegel/hegel-rust for discoverability.
HEGEL_GITHUB_REPOS = hegeldev/hegel-go \
                     hegeldev/hegel-cpp \
                     hegeldev/hegel-typescript \
                     hegeldev/hegel-java \
                     hegeldev/hegel-ocaml \
                     hegeldev/hegel-core \
                     hegeldev/hegel-skill

# Other PBT-in-C libraries we study for design comparison —
# `inspiration/existing-pbt-in-c/`
PBT_C_GITHUB_REPOS = silentbicycle/theft \
                     trailofbits/deepstate

# Real-world libraries we point hegel-c at to find bugs —
# `inspiration/targets/`
TARGET_GITLAB_REPOS = scotch/scotch

# Complementary (non-PBT) bug-finding systems aimed at the same targets —
# `inspiration/kernel-review/`.  Sashiko is static LLM patch review of
# Linux kernel changes (Apache-2.0); we study its per-stage review
# prompts as a bug-class checklist for hegel-bughunt.  Pinned SHA +
# stage-by-stage gap analysis live in docs/sashiko-gap-analysis.md —
# bump SASHIKO_PIN only after re-reading the stage prompts and
# refreshing that doc.
KERNEL_REVIEW_GITHUB_REPOS = sashiko-dev/sashiko
SASHIKO_PIN = a434adcd821bbc3767657f8c41e979082acf127a

# ---- Library build (pure C — no cargo) ----

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2
BUILD    = build
CORE_SRC = core/hegel_engine.c core/hegel_server.c core/hegel_runtime.c \
           core/hegel_runner.c core/hegel_stateful.c \
           hegel_gen.c
CORE_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(CORE_SRC)))

# Where the engine cdylib is built from.  hegel-rust is a pinned git
# submodule at deps/hegel-rust (the one build-critical dependency — it
# must match the runtime ABI, so it is version-locked in-tree rather
# than tracked loosely like the inspiration/ reference clones).
LIBHEGEL_CRATE_DIR = deps/hegel-rust
# The crate's lib is named `hegel_c` since hegeltest-c 0.4x, so cargo
# writes libhegel_c.so; we install it under the release-artifact name.
LIBHEGEL_BUILT     = $(LIBHEGEL_CRATE_DIR)/target/release/libhegel_c.so

.PHONY: help lib libhegel clean-lib inspiration clean-inspiration check-sashiko check-reuse clean-cores docs-check docs-fix test selftest-% from-hegel-rust-% scotch-% mpi-% bench-% docs-%

help:
	@echo "hegel-c Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help                          Show this help (default)"
	@echo "  inspiration                   Clone/update third-party repos into inspiration/{hegel,existing-pbt-in-c,targets,kernel-review}/"
	@echo "  clean-inspiration             Remove inspiration/"
	@echo "  check-sashiko                 Report whether upstream sashiko moved past our pinned SHA (see docs/sashiko-gap-analysis.md)"
	@echo "  check-reuse                   Lint per-file licensing against REUSE.toml + LICENSES/ (needs reuse or uvx)"
	@echo "  clean-cores                   Remove core dumps from repo root (core.*)"
	@echo ""
	@echo "Proxy targets (forwarded to sub-Makefiles):"
	@echo "  selftest-<target>             Run <target> in tests/selftest/"
	@echo "  from-hegel-rust-<target>      Run <target> in tests/from-hegel-rust/"
	@echo "  scotch-<target>               Run <target> in tests/irl/scotch/"
	@echo "  mpi-<target>                  Run <target> in tests/mpi/"
	@echo "  bench-<target>                Run <target> in tests/bench/"
	@echo ""
	@echo "Examples:"
	@echo "  make selftest-test            Run selftest suite"
	@echo "  make selftest-clean           Clean selftest binaries"
	@echo "  make from-hegel-rust-test     Run from-hegel-rust suite"
	@echo "  make from-hegel-rust-verify   Verify C tests match Rust originals (requires claude)"
	@echo "  make scotch-test-local        Run Scotch tests with inspiration/targets/scotch (built from source)"
	@echo "  make scotch-test-deb          Run Scotch tests with system libscotch-dev (CI)"
	@echo "  make scotch-test-pins         Run each Scotch bug pin (tests/irl/scotch/pins/) buggy + fixed"
	@echo "  make mpi-test                 Run MPI tests (requires mpicc/mpiexec)"
	@echo "  make bench-bench              Run fork-vs-nofork benchmarks (see docs/benchmarking.md)"
	@echo "  make docs-check               Verify transcluded snippets in docs/*.md match source"
	@echo "  make docs-fix                 Regenerate transcluded snippets in place"
	@echo ""
	@echo "Meta target:"
	@echo "  make test                     Run the four always-available suites:"
	@echo "                                docs-check + docs-test + selftest-test + from-hegel-rust-test"
	@echo "                                (mpi-test and scotch-test need optional deps — run separately)"
	@echo ""
	@echo "Sub-Makefile help:"
	@echo "  make selftest-help"
	@echo "  make from-hegel-rust-help"
	@echo "  make scotch-help"
	@echo "  make mpi-help"
	@echo "  make bench-help"

# ---- Library build ----

lib: $(BUILD)/libhegel_c.a

$(BUILD)/libhegel_c.a: $(CORE_OBJ)
	ar rcs $@ $^

$(BUILD)/%.o: core/%.c core/hegel_internal.h core/hegel_engine.h hegel_c.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I. -c -o $@ $<

$(BUILD)/hegel_gen.o: hegel_gen.c hegel_gen.h hegel_c.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# The engine cdylib: build from the inspiration clone (needs cargo)
# and place it where the runtime loader looks (./build/libhegel.so).
# A prebuilt artifact from hegel-rust's GitHub releases works too —
# drop it at build/libhegel.so or point HEGEL_LIBHEGEL_PATH at it.
libhegel: $(BUILD)/libhegel.so

$(BUILD)/libhegel.so:
	@mkdir -p $(BUILD)
	@if [ ! -d "$(LIBHEGEL_CRATE_DIR)" ]; then \
		echo "ERROR: engine source $(LIBHEGEL_CRATE_DIR) is missing."; \
		echo "       Run 'make inspiration' first, or drop a prebuilt libhegel.so at $@."; \
		exit 1; \
	fi
	@echo "Building libhegel from $(LIBHEGEL_CRATE_DIR) (cargo)..."
	cd $(LIBHEGEL_CRATE_DIR) && cargo build -p hegeltest-c --release
	@if [ ! -f "$(LIBHEGEL_BUILT)" ]; then \
		echo "ERROR: cargo build reported success but $(LIBHEGEL_BUILT) is absent."; \
		echo "       This is usually a stale cargo target cache — run 'cargo clean' in"; \
		echo "       $(LIBHEGEL_CRATE_DIR) and retry."; \
		exit 1; \
	fi
	cp $(LIBHEGEL_BUILT) $@

clean-lib:
	rm -rf $(BUILD)

inspiration:
	@mkdir -p $(INSPIRATION_DIR)/hegel $(INSPIRATION_DIR)/existing-pbt-in-c $(INSPIRATION_DIR)/targets $(INSPIRATION_DIR)/kernel-review
	@# hegel-rust lives as a pinned submodule at deps/hegel-rust; expose it
	@# under the old inspiration path for discoverability.  The symlink is
	@# never committed (inspiration/ is gitignored), so it poses no
	@# cross-platform git-symlink hazard.
	@if [ -d deps/hegel-rust ] && [ ! -e $(INSPIRATION_DIR)/hegel/hegel-rust ]; then \
		ln -s ../../deps/hegel-rust $(INSPIRATION_DIR)/hegel/hegel-rust; \
		echo "  hegel/hegel-rust: symlink -> deps/hegel-rust"; \
	fi
	@for repo in $(HEGEL_GITHUB_REPOS); do \
		name=$$(basename $$repo); \
		dest="$(INSPIRATION_DIR)/hegel/$$name"; \
		if [ -d "$$dest" ]; then \
			echo "  hegel/$$name: pulling latest"; \
			cd "$$dest" && git pull --ff-only && cd - >/dev/null; \
		else \
			echo "  hegel/$$name: cloning"; \
			git clone --depth 1 "https://github.com/$$repo.git" "$$dest"; \
		fi; \
	done
	@for repo in $(PBT_C_GITHUB_REPOS); do \
		name=$$(basename $$repo); \
		dest="$(INSPIRATION_DIR)/existing-pbt-in-c/$$name"; \
		if [ -d "$$dest" ]; then \
			echo "  existing-pbt-in-c/$$name: pulling latest"; \
			cd "$$dest" && git pull --ff-only && cd - >/dev/null; \
		else \
			echo "  existing-pbt-in-c/$$name: cloning"; \
			git clone --depth 1 "https://github.com/$$repo.git" "$$dest"; \
		fi; \
	done
	@for repo in $(TARGET_GITLAB_REPOS); do \
		name=$$(basename $$repo); \
		dest="$(INSPIRATION_DIR)/targets/$$name"; \
		if [ -d "$$dest" ]; then \
			echo "  targets/$$name: pulling latest"; \
			cd "$$dest" && git pull --ff-only && cd - >/dev/null; \
		else \
			echo "  targets/$$name: cloning"; \
			git clone --depth 1 "https://gitlab.inria.fr/$$repo.git" "$$dest"; \
		fi; \
	done
	@# kernel-review repos are PINNED, not pulled-latest: the gap analysis
	@# in docs/sashiko-gap-analysis.md is written against a specific SHA.
	@for repo in $(KERNEL_REVIEW_GITHUB_REPOS); do \
		name=$$(basename $$repo); \
		dest="$(INSPIRATION_DIR)/kernel-review/$$name"; \
		if [ ! -d "$$dest" ]; then \
			echo "  kernel-review/$$name: cloning"; \
			git clone "https://github.com/$$repo.git" "$$dest"; \
		fi; \
		echo "  kernel-review/$$name: checking out pin $(SASHIKO_PIN)"; \
		cd "$$dest" && git fetch origin "$(SASHIKO_PIN)" 2>/dev/null; \
		git checkout --detach "$(SASHIKO_PIN)" && cd - >/dev/null; \
	done

check-sashiko:
	@latest=$$(git ls-remote https://github.com/sashiko-dev/sashiko.git HEAD | cut -f1); \
	if [ -z "$$latest" ]; then \
		echo "check-sashiko: could not reach upstream"; exit 1; \
	elif [ "$$latest" = "$(SASHIKO_PIN)" ]; then \
		echo "sashiko pin $(SASHIKO_PIN) is upstream HEAD — gap analysis is current"; \
	else \
		echo "sashiko upstream HEAD is $$latest (pin: $(SASHIKO_PIN))"; \
		echo "-> re-read the stage prompts in src/worker/prompts.rs, refresh"; \
		echo "   docs/sashiko-gap-analysis.md, then bump SASHIKO_PIN in this Makefile."; \
		exit 1; \
	fi

# Licensing is per file (MIT for hegel-c, GPL/BSD/LGPL for the kernel
# excerpts under tests/irl/); see the License section of README.md.
check-reuse:
	@if command -v reuse >/dev/null; then reuse lint; else uvx reuse lint; fi

clean-inspiration:
	rm -rf $(INSPIRATION_DIR)

# Remove core dumps that accumulate in the repo root during test runs.
# Test runners cd to REPO_ROOT before exec'ing each binary, so cores
# from deliberate-crash tests (and any unexpected child crashes during
# shrinking) drop here.  HEGEL_TEST_ULIMIT in each test Makefile
# defaults to 0, suppressing cores at the source — this target cleans
# up any stragglers from older runs or hand-runs outside Make.
clean-cores:
	rm -f core.*

docs-check:
	python3 scripts/check_doc_includes.py

docs-fix:
	python3 scripts/check_doc_includes.py --fix

test: docs-check
	$(MAKE) -C tests/docs test
	$(MAKE) -C tests/selftest test
	$(MAKE) -C tests/from-hegel-rust test

selftest-%:
	$(MAKE) -C tests/selftest $*

from-hegel-rust-%:
	$(MAKE) -C tests/from-hegel-rust $*

scotch-%:
	$(MAKE) -C tests/irl/scotch $*

mpi-%:
	$(MAKE) -C tests/mpi $*

bench-%:
	$(MAKE) -C tests/bench $*

docs-%:
	$(MAKE) -C tests/docs $*
