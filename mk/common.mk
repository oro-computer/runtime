SHELL := /usr/bin/env bash
.DEFAULT_GOAL := help

define run
	@set -euo pipefail; \
	echo ">> $(1)"; \
	$(1)
endef

