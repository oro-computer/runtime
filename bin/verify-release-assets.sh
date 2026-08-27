#!/usr/bin/env bash

set -euo pipefail

if (( $# != 2 )); then
  echo >&2 "Usage: ./bin/verify-release-assets.sh <asset-directory> <release-version>"
  exit 2
fi

declare asset_dir="$1"
declare release_version="$2"

if [[ ! -d "$asset_dir" ]]; then
  echo >&2 "Release asset directory does not exist: $asset_dir"
  exit 1
fi

if [[ -z "$release_version" ]] || [[ "$release_version" == */* ]]; then
  echo >&2 "Invalid release version for asset verification: ${release_version:-<empty>}"
  exit 1
fi

asset_dir="$(cd "$asset_dir" && pwd)"

declare -a expected=(
  "linux-x64-desktop|tar.gz"
  "linux-x64-android-sdk|tar.gz"
  "linux-arm64-desktop|tar.gz"
  "macos-x64-desktop|tar.gz"
  "macos-x64-ios-sdk|tar.gz"
  "macos-arm64-desktop|tar.gz"
  "macos-arm64-ios-sdk|tar.gz"
  "windows-x64-desktop|zip"
)

shopt -s nullglob dotglob
declare -a archives=("$asset_dir"/*.tar.gz "$asset_dir"/*.zip)
declare -a checksums=("$asset_dir"/*.sha256)
declare -a sboms=("$asset_dir"/*.spdx.json)
declare -a files=("$asset_dir"/*)

if
  (( ${#archives[@]} != ${#expected[@]} )) ||
  (( ${#checksums[@]} != ${#expected[@]} )) ||
  (( ${#sboms[@]} != ${#expected[@]} )) ||
  (( ${#files[@]} != 24 ))
then
  echo >&2 "Expected exactly 8 archives, 8 checksums, and 8 SBOMs; found ${#archives[@]}, ${#checksums[@]}, ${#sboms[@]}, and ${#files[@]} total files"
  exit 1
fi

for entry in "${expected[@]}"; do
  declare artifact_id="${entry%%|*}"
  declare archive_ext="${entry#*|}"
  declare base="oro-runtime-$release_version-$artifact_id"
  declare archive="$base.$archive_ext"
  declare checksum="$archive.sha256"
  declare sbom="$base.spdx.json"

  for name in "$archive" "$checksum" "$sbom"; do
    if [[ ! -f "$asset_dir/$name" ]]; then
      echo >&2 "Missing expected release asset: $name"
      exit 1
    fi
  done

  declare -a checksum_lines=()
  mapfile -t checksum_lines < <(tr -d '\r' < "$asset_dir/$checksum")
  if (( ${#checksum_lines[@]} != 1 )); then
    echo >&2 "$checksum must contain exactly one checksum record"
    exit 1
  fi

  declare checksum_hash=""
  declare checksum_name=""
  declare checksum_extra=""
  read -r checksum_hash checksum_name checksum_extra <<< "${checksum_lines[0]}"
  if
    [[ ! "$checksum_hash" =~ ^[0-9a-fA-F]{64}$ ]] ||
    [[ "$checksum_name" != "$archive" ]] ||
    [[ -n "$checksum_extra" ]]
  then
    echo >&2 "$checksum does not contain the expected SHA-256 record for $archive"
    exit 1
  fi

  (cd "$asset_dir" && printf '%s\n' "${checksum_lines[0]}" | sha256sum --check -)
  jq -e \
    '(.spdxVersion | type == "string" and startswith("SPDX-")) and .SPDXID == "SPDXRef-DOCUMENT" and (.packages | type == "array")' \
    "$asset_dir/$sbom" > /dev/null
done

echo "ok - verified ${#expected[@]} release archives, checksums, and SPDX documents for $release_version"
