#!/usr/bin/env bash

CLI_BIN="${ORO_CLI_BIN:-oroc}"

if ! command -v "$CLI_BIN" >/dev/null 2>&1; then
  echo "Version check failed: 'oroc' is not available on PATH" >&2
  exit 1
fi

VERSION_CLI=$("$CLI_BIN" -v | head -n 1)

VERSION_TXT=$(cat VERSION.txt)
VERSION_GIT=$(git rev-parse --short=8 HEAD)
VERSION_EXPECTED="$VERSION_TXT ($VERSION_GIT)"

if [ "$VERSION_CLI" = "$VERSION_EXPECTED" ]; then
  echo "Version check has passed"
else
  echo "Version check has failed"
  echo "Expected: $VERSION_EXPECTED"
  echo "Got: $VERSION_CLI"
  exit 1
fi

BASE_LIST=($(echo $VERSION_TXT | tr '.' ' '))

V_MAJOR=${BASE_LIST[0]}
V_MINOR=${BASE_LIST[1]}

function assert_version_match () {
  local package="$1"
  local observed="$2"
  if [ "$observed" != "$VERSION_TXT" ]; then
    echo "Version of $package is not in sync with Oro Runtime";
    echo "Expected: $VERSION_TXT";
    echo "$package version: $observed";
    exit 1;
  fi
}

VERSION_NODE_PRIMARY=$(npm show ./npm/packages/@orocomputer/runtime-node version)

assert_version_match "@orocomputer/runtime-node" "$VERSION_NODE_PRIMARY"
