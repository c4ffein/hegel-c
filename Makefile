# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

INSPIRATION_DIR = inspiration

GITHUB_REPOS = hegeldev/hegel-rust \
               hegeldev/hegel-go \
               hegeldev/hegel-cpp

GITLAB_REPOS = scotch/scotch

.PHONY: help inspiration clean-inspiration selftest-% from-hegel-rust-% scotch-% mpi-% bench-%

help:
	@echo "hegel-c Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help                          Show this help (default)"
	@echo "  inspiration                   Clone/update hegel-rust, hegel-go, hegel-cpp into inspiration/"
	@echo "  clean-inspiration             Remove inspiration/"
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
	@echo "  make scotch-test-local        Run Scotch tests with inspiration/scotch (built from source)"
	@echo "  make scotch-test-deb          Run Scotch tests with system libscotch-dev (CI)"
	@echo "  make mpi-test                 Run MPI tests (requires mpicc/mpiexec)"
	@echo "  make bench-bench              Run fork-vs-nofork benchmarks (see docs/benchmarking.md)"
	@echo ""
	@echo "Sub-Makefile help:"
	@echo "  make selftest-help"
	@echo "  make from-hegel-rust-help"
	@echo "  make scotch-help"
	@echo "  make mpi-help"
	@echo "  make bench-help"

inspiration:
	@mkdir -p $(INSPIRATION_DIR)
	@for repo in $(GITHUB_REPOS); do \
		name=$$(basename $$repo); \
		if [ -d "$(INSPIRATION_DIR)/$$name" ]; then \
			echo "  $$name: pulling latest"; \
			cd "$(INSPIRATION_DIR)/$$name" && git pull --ff-only && cd ../..; \
		else \
			echo "  $$name: cloning"; \
			git clone --depth 1 "https://github.com/$$repo.git" "$(INSPIRATION_DIR)/$$name"; \
		fi; \
	done
	@for repo in $(GITLAB_REPOS); do \
		name=$$(basename $$repo); \
		if [ -d "$(INSPIRATION_DIR)/$$name" ]; then \
			echo "  $$name: pulling latest"; \
			cd "$(INSPIRATION_DIR)/$$name" && git pull --ff-only && cd ../..; \
		else \
			echo "  $$name: cloning"; \
			git clone --depth 1 "https://gitlab.inria.fr/$$repo.git" "$(INSPIRATION_DIR)/$$name"; \
		fi; \
	done

clean-inspiration:
	rm -rf $(INSPIRATION_DIR)

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
