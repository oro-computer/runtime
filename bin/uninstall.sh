#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

declare PREFIX="${PREFIX:-"/usr/local"}"

declare host="$(uname -s)"
if [[ "$host" == *"MINGW64_NT"* ]] || [[ "$host" == *"MSYS_NT"* ]]; then
  host="Win32"
fi

declare default_runtime_home=""
if [[ "$host" == "Win32" ]]; then
  default_runtime_home="${LOCALAPPDATA:-"$HOME/AppData/Local"}/Programs/oro"
else
  default_runtime_home="${XDG_DATA_HOME:-"$HOME/.local/share"}/oro"
fi

declare runtime_home="${ORO_HOME:-$default_runtime_home}"

declare files=($(find "$runtime_home"/{bin,include,lib,objects,src,uv,share/man/man1,share/man/man3,share/man/man7,share/doc/oroc} -type f 2>/dev/null))
declare bins=("$PREFIX/bin/oroc")
declare manpages=("$PREFIX"/share/man/man1/oroc*.1)
declare api_manpages=("$PREFIX"/share/man/man3/oro*.3)
declare guide_manpages=("$PREFIX"/share/man/man7/oro*.7)
declare runtime_docs=("$PREFIX"/share/doc/oroc/*)

declare uninstalled=0

for file in "${files[@]}"; do
  if test -f "$file"; then
    echo "# removing $file"
    rm -f "$file"
    (( uninstalled++ ))
  fi
done

for bin in "${bins[@]}"; do
  if test -f "$bin"; then
    echo "# removing $bin"
    rm -f "$bin"
    (( uninstalled++ ))
  fi
done

for manpage in "${manpages[@]}"; do
  if test -f "$manpage"; then
    echo "# removing $manpage"
    rm -f "$manpage"
    (( uninstalled++ ))
  fi
done

for manpage in "${api_manpages[@]}"; do
  if test -f "$manpage"; then
    echo "# removing $manpage"
    rm -f "$manpage"
    (( uninstalled++ ))
  fi
done

for manpage in "${guide_manpages[@]}"; do
  if test -f "$manpage"; then
    echo "# removing $manpage"
    rm -f "$manpage"
    (( uninstalled++ ))
  fi
done

for doc in "${runtime_docs[@]}"; do
  if test -f "$doc"; then
    echo "# removing $doc"
    rm -f "$doc"
    (( uninstalled++ ))
  fi
done

if (( uninstalled > 0 )); then
  echo "ok - uninstalled $uninstalled files"
else
  exit 1
fi
