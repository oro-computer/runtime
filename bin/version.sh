#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
current_version="$(tr -d '[:space:]' < "$root/VERSION.txt")"
IFS='.' read -r major minor patch <<< "$current_version"
suggested_version="$major.$minor.$((patch + 1))"
next_version="${1:-}"

if [[ -z "$next_version" ]]; then
  printf 'Enter a version number [%s]: ' "$suggested_version"
  read -r next_version
fi

next_version="${next_version:-$suggested_version}"
next_version="${next_version#v}"

node "$root/bin/set-release-version.js" "$next_version"
node "$root/bin/check-release-version.js" "$next_version"

printf 'Updated Oro Runtime version from %s to %s.\n' "$current_version" "$next_version"
