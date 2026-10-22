#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

cli_source_root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

cli_digest() {
  if (( $# > 0 )); then
    cli_digest < "$1"
    return
  fi
  local result
  if command -v sha256sum >/dev/null 2>&1; then
    result="$(sha256sum)"
  else
    result="$(shasum -a 256)"
  fi
  printf '%s\n' "${result%% *}"
}

{
  for cli_input in "$cli_source_root"/src/cli/*; do
    case "$cli_input" in
      *.cc|*.hh)
        printf '%s:%s\n' "${cli_input#"$cli_source_root"/}" "$(cli_digest "$cli_input")"
        ;;
    esac
  done
} | cli_digest
