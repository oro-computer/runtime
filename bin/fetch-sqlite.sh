#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOWNLOAD_URL="https://sqlite.org/2025/sqlite-amalgamation-3500400.zip"
EXPECTED_SHA256="1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032"
ARCHIVE_NAME="sqlite-amalgamation-3500400.zip"
ARCHIVE_BASENAME="sqlite-amalgamation-3500400"
BUILD_DIR="$ROOT/build"
SQLITE_DIR="$BUILD_DIR/sqlite"
CACHE_DIR="$BUILD_DIR/downloads"
ARCHIVE_PATH="$CACHE_DIR/$ARCHIVE_NAME"
TMP_DIR="$BUILD_DIR/.sqlite-tmp"

cleanup() {
  rm -rf "$TMP_DIR"
}

trap cleanup EXIT

log() {
  printf '[fetch-sqlite] %s\n' "$1" >&2
}

die() {
  log "error: $1"
  exit 1
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "missing required command: $1"
  fi
}

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--force]

Downloads the SQLite amalgamation archive and prepares it under build/sqlite.
  --force       Re-download and overwrite any existing archive and output
USAGE
}

FORCE=0
while (( $# > 0 )); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -f|--force)
      FORCE=1
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
  shift
done

mkdir -p "$BUILD_DIR" "$CACHE_DIR"

require_cmd unzip

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    die "sha256sum or shasum is required to verify downloads"
  fi
}

if command -v curl >/dev/null 2>&1; then
  DOWNLOADER=(curl -fL)
elif command -v wget >/dev/null 2>&1; then
  DOWNLOADER=(wget -O -)
else
  die "either curl or wget is required"
fi

if [[ $FORCE -eq 1 ]]; then
  rm -f "$ARCHIVE_PATH"
fi

if [[ ! -f "$ARCHIVE_PATH" ]]; then
  log "downloading $DOWNLOAD_URL"
  if [[ "${DOWNLOADER[0]}" == "curl" ]]; then
    "${DOWNLOADER[@]}" "$DOWNLOAD_URL" -o "$ARCHIVE_PATH"
  else
    "${DOWNLOADER[@]}" "$DOWNLOAD_URL" > "$ARCHIVE_PATH"
  fi
else
  log "using cached archive $ARCHIVE_PATH"
fi

OBSERVED_SHA256="$(sha256_file "$ARCHIVE_PATH")"
if [[ "$OBSERVED_SHA256" != "$EXPECTED_SHA256" ]]; then
  die "checksum mismatch for $ARCHIVE_NAME: expected $EXPECTED_SHA256, found $OBSERVED_SHA256"
fi

mkdir -p "$TMP_DIR"

log "extracting archive"
unzip -q "$ARCHIVE_PATH" -d "$TMP_DIR"

EXTRACTED_DIR="$TMP_DIR/$ARCHIVE_BASENAME"
if [[ ! -d "$EXTRACTED_DIR" ]]; then
  die "expected directory $ARCHIVE_BASENAME missing after extraction"
fi

rm -rf "$SQLITE_DIR"
mkdir -p "$SQLITE_DIR"

log "preparing files in $SQLITE_DIR"
shopt -s dotglob
cp -R "$EXTRACTED_DIR"/* "$SQLITE_DIR"/
shopt -u dotglob

# The official amalgamation ships with SQLITE_OMIT_LOAD_EXTENSION enabled by
# default, which disables the loadable extension mechanism entirely. Oro's
# runtime needs loadable extensions (for example, cr-sqlite), so strip the
# local SQLITE_OMIT_LOAD_EXTENSION defines from the amalgamation headers.
patch_disable_omit_load_extension () {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    return
  fi

  # Replace any direct SQLITE_OMIT_LOAD_EXTENSION defines with a comment while
  # leaving conditional uses (#ifdef/#ifndef) intact so extension support code
  # is compiled in.
  if [[ "$(uname -s)" == "Darwin" ]]; then
    sed -i '' 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$file"
  else
    sed -i 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$file"
  fi
}

patch_disable_omit_load_extension "$SQLITE_DIR/sqlite3.c"
patch_disable_omit_load_extension "$SQLITE_DIR/sqlite3.h"

cleanup

if [[ ! -f "$SQLITE_DIR/sqlite3.c" ]]; then
  die "sqlite3.c not found in $SQLITE_DIR"
fi

log "SQLite amalgamation is ready at $SQLITE_DIR"
