# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein — see hegel/LICENSE for terms.

INSPIRATION_DIR = inspiration

REPOS = hegeldev/hegel-rust \
        hegeldev/hegel-go

.PHONY: inspiration clean-inspiration

inspiration:
	@mkdir -p $(INSPIRATION_DIR)
	@for repo in $(REPOS); do \
		name=$$(basename $$repo); \
		if [ -d "$(INSPIRATION_DIR)/$$name" ]; then \
			echo "  $$name: pulling latest"; \
			cd "$(INSPIRATION_DIR)/$$name" && git pull --ff-only && cd ../..; \
		else \
			echo "  $$name: cloning"; \
			git clone --depth 1 "https://github.com/$$repo.git" "$(INSPIRATION_DIR)/$$name"; \
		fi; \
	done

clean-inspiration:
	rm -rf $(INSPIRATION_DIR)
