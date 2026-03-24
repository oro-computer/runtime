#!/bin/sh

npm link @orocomputer/latica

version="${1:-"1.0.23-0"}"

rm -rf api/latica.js || exit $?
rm -rf api/latica || exit $?
cp -rf node_modules/@orocomputer/latica/src api/latica || exit $?
rm -rf node_modules/@orocomputer/{runtime,runtime-{darwin,linux,win32}*,latica} || exit $?

for file in $(find api/latica -type f); do
  sed -i '' -e "s/'oro:\(.*\)'/'..\/\1.js'/g" "$file" || exit $?
done

{
  echo "import def from './latica/index.js'"
  echo "export * from './latica/index.js'"
  echo "export default def"
} >> api/latica.js

tree api/latica
