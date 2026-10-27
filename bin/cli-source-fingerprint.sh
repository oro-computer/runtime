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
  while IFS= read -r cli_input; do
    if [[ ! -f "$cli_input" ]]; then
      printf >&2 'Missing CLI distribution input: %s\n' "$cli_input"
      exit 1
    fi
    printf '%s:%s\n' "${cli_input#"$cli_source_root"/}" "$(cli_digest "$cli_input")"
  done < <({ find "$cli_source_root/src/cli/" -type f \( \
    -name '*.cc' -o -name '*.hh' -o -name '*.cjs' -o -name '*.kt' -o \
    -name '*.xml' -o -name '*.gradle' -o -name '*.java' \) -print
    printf '%s\n' "$cli_source_root/bin/functions.sh" "$cli_source_root/bin/android-functions.sh" "$cli_source_root/bin/runtime-artifacts.sh"
  } | sort)
} | cli_digest
