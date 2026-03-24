#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
declare tmpdir=""

cd "$root" || exit $?

"$root/node_modules/.bin/tsc" --version >/dev/null 2>&1 || {
  echo "TypeScript compiler not found; run npm install first." >&2
  exit 1
}

cleanup() {
  if [ -n "$tmpdir" ] && [ -d "$tmpdir" ]; then
    rm -rf "$tmpdir"
  fi
}

trap cleanup EXIT

# Rebuild per-module declaration files in an isolated temp workspace so the
# checked-in api/*.d.ts files do not get pulled back into the compiler graph.
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/oro-tsc.XXXXXX")" || exit $?

mkdir -p "$tmpdir/api" || exit $?
cp "$root/tsconfig.json" "$tmpdir/tsconfig.json" || exit $?

while IFS= read -r -d '' source; do
  rel="${source#"$root/api/"}"
  dest="$tmpdir/api/$rel"
  mkdir -p "$(dirname "$dest")" || exit $?
  cp "$source" "$dest" || exit $?
done < <(find "$root/api" \( -name '*.js' -o -name 'global.d.ts' \) -print0)

(cd "$tmpdir" && "$root/node_modules/.bin/tsc" --emitDeclarationOnly --module es2022 --outDir out) || exit $?

(cd "$tmpdir" && "$root/node_modules/.bin/tsc" --emitDeclarationOnly --module es2022 --outFile out/index.tmp) || exit $?

while IFS= read -r -d '' generated; do
  rel="${generated#"$tmpdir/out/"}"
  dest="$root/api/$rel"
  mkdir -p "$(dirname "$dest")" || exit $?
  sed '/^declare namespace ___tmp_oro_tsc_.* { }$/d' "$generated" > "$dest" \
    || exit $?
done < <(find "$tmpdir/out" -name '*.d.ts' -print0)

rm -f "$root/api/index.d.ts" "$root/api/index.tmp.d.ts"

cat "$tmpdir/out/index.tmp.d.ts" "$root/api/global.d.ts"         \
  | sed 's/declare module "\(.*\)"/\ndeclare module "oro:\1"/g'   \
  | sed 's/from "\(.*\)"/from "oro:\1"/g'                        \
  | sed 's/import("\(.*\)")/import("oro:\1")/g'                  \
  | sed 's/namespace \_\_\_.*$//g'                               \
  | sed 's/[[:space:]]*$//' > api/index.d.ts                     \
  || exit $?
