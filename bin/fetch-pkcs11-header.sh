#!/usr/bin/env bash
set -euo pipefail

runtime_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
header_directory="$runtime_root/build/include/pkcs11"
header_path="$header_directory/pkcs11.h"
header_sha256="eedac75650b065edb566e21eeedd2483abd4ced047fdd0589e54b05b914510d5"
header_url="https://raw.githubusercontent.com/p11-glue/p11-kit/0.25.5/common/pkcs11.h"

digest() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d ' ' -f 1
  else
    shasum -a 256 "$1" | cut -d ' ' -f 1
  fi
}

if [[ -e "$header_path" ]]; then
  if [[ "$(digest "$header_path")" != "$header_sha256" ]]; then
    echo >&2 "not ok - cached PKCS #11 header differs from the pinned p11-kit 0.25.5 input"
    exit 1
  fi
  exit 0
fi

mkdir -p "$header_directory"
header_temporary="$(mktemp "$header_directory/.pkcs11-XXXXXXXX")"
curl --proto '=https' --tlsv1.2 --fail --location --silent --show-error \
  --connect-timeout 15 --max-time 60 --max-filesize 131072 \
  --output "$header_temporary" "$header_url"
if [[ "$(digest "$header_temporary")" != "$header_sha256" ]]; then
  echo >&2 "not ok - PKCS #11 header SHA-256 mismatch"
  exit 1
fi
chmod 644 "$header_temporary"
mv -n "$header_temporary" "$header_path"
if [[ "$(digest "$header_path")" != "$header_sha256" ]]; then
  echo >&2 "not ok - concurrent PKCS #11 header acquisition differs from the pinned input"
  exit 1
fi
echo "ok - verified PKCS #11 headers from p11-kit 0.25.5"
