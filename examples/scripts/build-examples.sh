#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

bash "$ROOT/scripts/clean-examples.sh"

printf '[examples] bundling UI assets…\n'
node "$ROOT/build.js"
