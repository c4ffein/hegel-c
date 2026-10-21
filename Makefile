# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

INSPIRATION_DIR = inspiration

# Sister hegel bindings + the canonical Agent Skill — `inspiration/hegel/`.
# NOTE: the Hegel monorepo (hegeldev/hegel — libhegel + the Rust and
# Python frontends; it absorbed hegel-rust in 2026-10) is NOT here — it is
# the one build-critical dependency and lives as a pinned submodule at
# deps/hegel.  `make inspiration` symlinks it as inspiration/hegel/hegel,
# and its Rust frontend as inspiration/hegel/hegel-rust (the path the
# from-hegel-rust RUST_SOURCE comments and `make verify` resolve against).
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

# Where the engine cdylib is built from.  The Hegel monorepo
# (hegeldev/hegel: libhegel + the Rust and Python frontends) is a pinned
# git submodule at deps/hegel (the one build-critical dependency — it
# must match the runtime ABI, so it is version-locked in-tree rather
# than tracked loosely like the inspiration/ reference clones).  cargo
# resolves `-p hegeltest-c` through the workspace, so this is the root.
LIBHEGEL_CRATE_DIR = deps/hegel
# The Rust frontend inside it — what tests/from-hegel-rust ports from.
HEGEL_RUST_FRONTEND_DIR = deps/hegel/frontends/rust
# The crate's lib is named `hegel_c` since hegeltest-c 0.4x, so cargo
# writes libhegel_c.so; we install it under the release-artifact name.
LIBHEGEL_BUILT     = $(LIBHEGEL_CRATE_DIR)/target/release/libhegel_c.so

# The engine version — must be what deps/hegel is checked out at
# (tag libhegel/<version>; `make check-libhegel` verifies).  Bumping it:
# check out the new tag in deps/hegel, update the version and the two
# checksums below (the release's .sha256 assets), rerun the suites.
LIBHEGEL_VERSION   = 0.48.0
LIBHEGEL_RELEASE   = https://github.com/hegeldev/hegel/releases/download/libhegel/$(LIBHEGEL_VERSION)
LIBHEGEL_SHA256_linux-amd64 = 7662636b658065f08962ead44d0cd4e5d1e93dde09bc4117f7cee0320f42561e
LIBHEGEL_SHA256_linux-arm64 = ef654e8f552a09a5adfe26cb2bc270a3efa3160327763e38d9077407cec4a879
LIBHEGEL_PLATFORM  = $(shell uname -s | tr A-Z a-z)-$(shell uname -m | sed 's/x86_64/amd64/; s/aarch64/arm64/')
# Set PREBUILT=1 to download the release artifact (checksum-verified)
# instead of building with cargo.
PREBUILT ?=
# Engine builds are tagged with version and origin; build/libhegel.so
# (the path the runtime loader looks at) is a symlink to the one in use.
LIBHEGEL_FLAVOR    = $(if $(PREBUILT),prebuilt,source)
LIBHEGEL_TAGGED    = $(BUILD)/libhegel-$(LIBHEGEL_VERSION)-$(LIBHEGEL_FLAVOR).so

.PHONY: FORCE help lib libhegel clean-lib inspiration clean-inspiration check-sashiko check-libhegel check-reuse clean-cores docs-check docs-fix test selftest-% from-hegel-rust-% scotch-% mpi-% bench-% docs-%

help:
	@echo "hegel-c Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  help                          Show this help (default)"
	@echo "  inspiration                   Clone/update third-party repos into inspiration/{hegel,existing-pbt-in-c,targets,kernel-review}/"
	@echo "  clean-inspiration             Remove inspiration/"
	@echo "  check-libhegel                Check the engine pin: submodule, installed .so, newer upstream releases"
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

# -MMD -MP: each object records the headers it included (build/*.d), so
# editing any header rebuilds exactly the objects that use it.
DEPFLAGS = -MMD -MP

lib: $(BUILD)/libhegel_c.a

$(BUILD)/libhegel_c.a: $(CORE_OBJ)
	ar rcs $@ $^

# An object whose .d is missing (first build, or built before -MMD) is
# rebuilt once, so a header edit is never missed.
$(BUILD)/%.o: core/%.c $(BUILD)/%.d
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -I. -c -o $@ $<

$(BUILD)/hegel_gen.o: hegel_gen.c $(BUILD)/hegel_gen.d
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -I. -c -o $@ $<

$(CORE_OBJ:.o=.d): ;
.PRECIOUS: $(CORE_OBJ:.o=.d)

-include $(CORE_OBJ:.o=.d)

FORCE:

# The engine cdylib.  The runtime loader looks at ./build/libhegel.so;
# that is a symlink to a tagged build, build/libhegel-<version>-<origin>.so:
#   - source (default): built from the deps/hegel submodule with cargo
#     (~30 s clean; needs the rust-version libhegel declares — newer than
#     some distros ship, e.g. Debian 13's 1.85).  cargo runs on every
#     `make libhegel` and decides itself whether the engine source
#     changed (~0.1 s when not); the tagged file is only replaced when
#     the result differs.  Without cargo or the submodule (CI test jobs,
#     which download the engine job's tagged file), an existing tagged
#     build is kept as is.
#   - prebuilt (PREBUILT=1): the release artifact for LIBHEGEL_VERSION,
#     checked against the sha256 pinned above; downloaded once.
# A plain file at build/libhegel.so (placed by hand, or from before
# tagging) is moved to build/libhegel-untagged.so.  To use an engine of
# your own without make touching it, point HEGEL_LIBHEGEL_PATH at it.
libhegel: $(LIBHEGEL_TAGGED)
	@if [ -f $(BUILD)/libhegel.so ] && [ ! -L $(BUILD)/libhegel.so ]; then \
		mv $(BUILD)/libhegel.so $(BUILD)/libhegel-untagged.so; \
		echo "build/libhegel.so was an untagged file: moved to build/libhegel-untagged.so"; \
	fi
	@if [ "$$(readlink $(BUILD)/libhegel.so)" != "$(notdir $(LIBHEGEL_TAGGED))" ]; then \
		ln -sfn $(notdir $(LIBHEGEL_TAGGED)) $(BUILD)/libhegel.so; \
		echo "build/libhegel.so -> $(notdir $(LIBHEGEL_TAGGED))"; \
	fi

$(BUILD)/libhegel-$(LIBHEGEL_VERSION)-prebuilt.so:
	@mkdir -p $(BUILD)
	@sum="$(LIBHEGEL_SHA256_$(LIBHEGEL_PLATFORM))"; \
	if [ -z "$$sum" ]; then \
		echo "ERROR: no prebuilt libhegel pinned for $(LIBHEGEL_PLATFORM)"; \
		echo "       (pinned: linux-amd64, linux-arm64) — build from source instead."; \
		exit 1; \
	fi; \
	echo "Downloading libhegel $(LIBHEGEL_VERSION) for $(LIBHEGEL_PLATFORM)..."; \
	curl -fsSL -o $@.tmp "$(LIBHEGEL_RELEASE)/libhegel-$(LIBHEGEL_PLATFORM).so" || \
		{ rm -f $@.tmp; echo "ERROR: download failed"; exit 1; }; \
	if ! echo "$$sum  $@.tmp" | sha256sum -c --status; then \
		rm -f $@.tmp; \
		echo "ERROR: checksum mismatch for libhegel-$(LIBHEGEL_PLATFORM).so $(LIBHEGEL_VERSION)"; \
		exit 1; \
	fi; \
	mv $@.tmp $@

$(BUILD)/libhegel-$(LIBHEGEL_VERSION)-source.so: FORCE
	@mkdir -p $(BUILD)
	@why=""; \
	if [ ! -f "$(LIBHEGEL_CRATE_DIR)/libhegel/Cargo.toml" ]; then \
		why="the engine source $(LIBHEGEL_CRATE_DIR) is missing (run: git submodule update --init deps/hegel)"; \
	else \
		need=$$(sed -n 's/^rust-version *= *"\(.*\)"/\1/p' $(LIBHEGEL_CRATE_DIR)/libhegel/Cargo.toml); \
		have=$$(cargo --version 2>/dev/null | cut -d' ' -f2); \
		if [ -z "$$have" ]; then \
			why="cargo not found — building libhegel needs Rust $$need or newer (install it with rustup)"; \
		elif [ -n "$$need" ] && [ "$$(printf '%s\n%s\n' "$$need" "$$have" | sort -V | head -1)" != "$$need" ]; then \
			why="cargo $$have is older than the Rust $$need libhegel needs (update with rustup; distro packages often lag)"; \
		fi; \
	fi; \
	if [ -n "$$why" ]; then \
		if [ -f $@ ]; then \
			[ -d "$(LIBHEGEL_CRATE_DIR)/libhegel" ] && echo "libhegel: keeping $(notdir $@), not rebuilt: $$why"; \
			exit 0; \
		fi; \
		echo "ERROR: cannot build libhegel: $$why."; \
		echo "       Or run 'make libhegel PREBUILT=1' to download the checksum-verified"; \
		echo "       release artifact instead."; \
		exit 1; \
	fi; \
	v=$$(sed -n 's/^version *= *"\(.*\)"/\1/p' $(LIBHEGEL_CRATE_DIR)/libhegel/Cargo.toml | head -1); \
	if [ "$$v" != "$(LIBHEGEL_VERSION)" ]; then \
		echo "ERROR: deps/hegel has libhegel $$v, the Makefile pins $(LIBHEGEL_VERSION)."; \
		echo "       Run 'git submodule update --init deps/hegel', or finish the bump"; \
		echo "       (LIBHEGEL_VERSION + the checksums)."; \
		exit 1; \
	fi; \
	(cd $(LIBHEGEL_CRATE_DIR) && cargo build -q -p hegeltest-c --release) || exit 1; \
	if [ ! -f "$(LIBHEGEL_BUILT)" ]; then \
		echo "ERROR: cargo build reported success but $(LIBHEGEL_BUILT) is absent."; \
		echo "       This is usually a stale cargo target cache — run 'cargo clean' in"; \
		echo "       $(LIBHEGEL_CRATE_DIR) and retry."; \
		exit 1; \
	fi; \
	if ! cmp -s $(LIBHEGEL_BUILT) $@; then \
		cp $(LIBHEGEL_BUILT) $@ && echo "libhegel: built $(notdir $@)"; \
	fi

clean-lib:
	rm -rf $(BUILD)

inspiration:
	@mkdir -p $(INSPIRATION_DIR)/hegel $(INSPIRATION_DIR)/existing-pbt-in-c $(INSPIRATION_DIR)/targets $(INSPIRATION_DIR)/kernel-review
	@# The monorepo lives as a pinned submodule at deps/hegel; expose it,
	@# and its Rust frontend, under inspiration/ for discoverability.  The
	@# symlinks are never committed (inspiration/ is gitignored), so they
	@# pose no cross-platform git-symlink hazard.  An existing symlink is
	@# repointed (the submodule moved from deps/hegel-rust in 2026-10).
	@for link in hegel:deps/hegel hegel-rust:$(HEGEL_RUST_FRONTEND_DIR); do \
		name=$${link%%:*}; target=$${link#*:}; \
		dest="$(INSPIRATION_DIR)/hegel/$$name"; \
		if [ -d "$$target" ] && { [ -L "$$dest" ] || [ ! -e "$$dest" ]; }; then \
			ln -sfn "../../$$target" "$$dest"; \
			echo "  hegel/$$name: symlink -> $$target"; \
		fi; \
	done
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

# The engine pin, three ways: is deps/hegel at libhegel/$(LIBHEGEL_VERSION),
# does build/libhegel.so point at a build of that version, and
# has upstream released a newer libhegel?  Exit 1 if any is off.
check-libhegel:
	@rc=0; \
	if git -C $(LIBHEGEL_CRATE_DIR) tag --points-at HEAD 2>/dev/null | grep -qx "libhegel/$(LIBHEGEL_VERSION)"; then \
		echo "deps/hegel: at libhegel/$(LIBHEGEL_VERSION)"; \
	else \
		echo "deps/hegel: NOT at libhegel/$(LIBHEGEL_VERSION) (run: git submodule update --init deps/hegel)"; rc=1; \
	fi; \
	if [ ! -e $(BUILD)/libhegel.so ]; then \
		echo "build/libhegel.so: absent (run: make libhegel)"; \
	elif [ ! -L $(BUILD)/libhegel.so ]; then \
		echo "build/libhegel.so: untagged file, version unknown (run: make libhegel)"; \
	else \
		inst=$$(readlink $(BUILD)/libhegel.so); \
		case "$$inst" in \
			libhegel-$(LIBHEGEL_VERSION)-*.so) echo "build/libhegel.so -> $$inst";; \
			*) echo "build/libhegel.so -> $$inst, expected $(LIBHEGEL_VERSION) (run: make libhegel)"; rc=1;; \
		esac; \
	fi; \
	latest=$$(git ls-remote --tags https://github.com/hegeldev/hegel.git 'refs/tags/libhegel/*' \
		| sed -n 's|.*refs/tags/libhegel/\([0-9.]*\)$$|\1|p' | sort -V | tail -1); \
	if [ -z "$$latest" ]; then \
		echo "upstream: could not reach hegeldev/hegel"; rc=1; \
	elif [ "$$latest" = "$(LIBHEGEL_VERSION)" ]; then \
		echo "upstream: libhegel/$(LIBHEGEL_VERSION) is the latest release"; \
	else \
		echo "upstream: libhegel/$$latest is out (pinned: $(LIBHEGEL_VERSION)) — read"; \
		echo "   libhegel/CHANGELOG.md, then bump deps/hegel + LIBHEGEL_VERSION + the checksums"; \
		rc=1; \
	fi; \
	exit $$rc

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

# Licensing is per file (MIT for hegel-c, upstream licenses for the fix
# patches under tests/irl/*/pins/); see the License section of README.md.
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
