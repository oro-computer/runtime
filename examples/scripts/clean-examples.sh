#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

while IFS= read -r dir; do
  [ -d "$dir" ] || continue

  while IFS= read -r nested; do
    rel_path="${nested#"$ROOT/"}"
    printf '[examples] pruning nested build directory: %s\n' "$rel_path"
    rm -rf "$nested"
  done < <(find "$dir" -type d -path '*/opt/*/build')

  find "$dir" -type f -name 'build-*.log' -print -delete | while read -r log; do
    rel_path="${log#"$ROOT/"}"
    printf '[examples] removing stale build log: %s\n' "$rel_path"
  done
done < <(find "$ROOT" -mindepth 2 -maxdepth 2 -type d -name build -prune -print)
