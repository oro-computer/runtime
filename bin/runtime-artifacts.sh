#!/usr/bin/env bash
# shellcheck shell=bash
#
# Shared helpers for naming runtime build artifacts. Source this file from
# bash scripts that need to know the canonical Oro Runtime artifact name.

if [[ -n "${ORO_RUNTIME_ARTIFACTS_SH_SOURCED:-}" ]]; then
  return 0 2>/dev/null || true
fi
ORO_RUNTIME_ARTIFACTS_SH_SOURCED=1

: "${ORO_RUNTIME_ARTIFACT_NAME:=oro-runtime}"

if [[ -z "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  ORO_RUNTIME_ARTIFACT_ALIASES=()
fi

function oro_runtime_all_artifact_names() {
  printf '%s\n' "$ORO_RUNTIME_ARTIFACT_NAME"
  for alias in "${ORO_RUNTIME_ARTIFACT_ALIASES[@]}"; do
    if [[ "$alias" != "$ORO_RUNTIME_ARTIFACT_NAME" ]]; then
      printf '%s\n' "$alias"
    fi
  done
}

function oro_runtime_canonical_lib_prefix() {
  printf 'lib%s' "$ORO_RUNTIME_ARTIFACT_NAME"
}

function oro_runtime_alias_lib_prefixes() {
  for alias in "${ORO_RUNTIME_ARTIFACT_ALIASES[@]}"; do
    if [[ "$alias" != "$ORO_RUNTIME_ARTIFACT_NAME" ]]; then
      printf 'lib%s\n' "$alias"
    fi
  done
}

return 0 2>/dev/null || true
