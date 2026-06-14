# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

INSPIRATION_DIR = inspiration

# Sister hegel bindings + the canonical Agent Skill — `inspiration/hegel/`
HEGEL_GITHUB_REPOS = hegeldev/hegel-rust \
                     hegeldev/hegel-go \
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

# ---- Library build (pure C — no cargo) ----

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2
BUILD    = build
CORE_SRC = core/hegel_cbor.c core/hegel_engine.c core/hegel_runtime.c \
           core/hegel_runner.c core/hegel_gens.c core/hegel_stateful.c \
           hegel_gen.c
CORE_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(CORE_SRC)))

# Where the engine cdylib is built from (the upstream clone).
LIBHEGEL_CRATE_DIR = $(INSPIRATION_DIR)/hegel/hegel-rust
LIBHEGEL_BUILT     = $(LIBHEGEL_CRATE_DIR)/target/release/libhegel.so

.PHONY: help lib libhegel clean-lib inspiration clean-inspiration clean-cores docs-check docs-fix test selftest-% from-hegel-rust-% scotch-% mpi-% bench-% docs-%

help:
	@echo "hegel-c Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help                          Show this help (default)"
	@echo "  inspiration                   Clone/update third-party repos into inspiration/{hegel,existing-pbt-in-c,targets}/"
	@echo "  clean-inspiration             Remove inspiration/"
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

$(BUILD)/%.o: core/%.c core/hegel_internal.h core/hegel_engine.h core/hegel_cbor.h hegel_c.h
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
	@if [ ! -f "$(LIBHEGEL_BUILT)" ]; then \
		echo "Building libhegel from $(LIBHEGEL_CRATE_DIR) (cargo)..."; \
		cd $(LIBHEGEL_CRATE_DIR) && cargo build -p hegeltest-c --release; \
	fi
	cp $(LIBHEGEL_BUILT) $@

clean-lib:
	rm -rf $(BUILD)

inspiration:
	@mkdir -p $(INSPIRATION_DIR)/hegel $(INSPIRATION_DIR)/existing-pbt-in-c $(INSPIRATION_DIR)/targets
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
