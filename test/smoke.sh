#!/bin/bash
PLATFORM="$(uname -s)"

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
source "$root/bin/functions.sh"

echo "TAP version 13"
echo "1..N"

TEST=true ./bin/install.sh ios
die $? "the cli tool was built"

mkdir -p test/tmp
cd test/tmp

echo # the init command creates dirs and ouputs files

quiet ../../bin/cli init

quiet stat src/index.html
die $? "the index.html file exists"

quiet stat oro.toml
die $? "the Oro config file exists"

quiet ../../bin/cli compile .
die $? "the compile command executed"

#
# MacOS
#
if [ "$PLATFORM" == "Darwin" ]; then
  quiet stat build/beepboop-dev.app/Contents/MacOS/boop-dev
  die $? "the compile command created a binary in the correct location"

  pathToApp=$PWD/build/beepboop-dev.app

  osascript <<EOD
    set app to POSIX file "$pathToApp" as alias
    tell application app to activate
    delay 1
    tell application app to quit
EOD
fi

#
# Linux
#
if [ "$PLATFORM" = "Linux" ]; then
  echo "TODO"
  exit 1
fi

#
# Windows (via WSL)
#
case "$PLATFORM" in
  MINGW*|MSYS*|CYGWIN*)
  echo "TODO"
  exit 1
  ;;
esac

cd ../..
rm -rf test/tmp
