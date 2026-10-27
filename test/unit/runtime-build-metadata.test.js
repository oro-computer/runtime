import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

test('incremental runtime builds refresh revision metadata and preserve dotted source paths', t => {
  const compiler = ['clang++', 'c++'].find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
  if (!compiler) return t.skip('A C++ compiler and Bash are required')
  const builder = readFileSync(new URL('../../bin/build-runtime-library.sh', import.meta.url), 'utf8')
  const start = builder.indexOf('      declare src_directory="$root/src"', builder.indexOf('function main ()'))
  const end = builder.indexOf('    } & pids+=($!)', start)
  assert.ok(start >= 0 && end > start)
  const compile = builder.slice(start, end)
  const archiveStart = builder.indexOf('  local build_static=0')
  const archiveEnd = builder.indexOf('  if (( build_static )); then', archiveStart)
  assert.ok(archiveStart > end && archiveEnd > archiveStart)
  const archiveCheck = builder.slice(archiveStart, archiveEnd).replaceAll('  local ', '  declare ')
  const root = mkdtempSync(path.join(tmpdir(), 'oro metadata .checkout.cc '))
  mkdirSync(path.join(root, 'src/runtime'), { recursive: true })
  writeFileSync(path.join(root, 'src/runtime/version.cc'), `
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
extern const char* revision;
const char* revision = STRINGIFY(ORO_RUNTIME_VERSION_HASH);
`)
  writeFileSync(path.join(root, 'src/runtime/version.cpp'), 'int publicOtherSource = 7;\n')
  writeFileSync(path.join(root, 'main.cc'), '#include <iostream>\n#include <cassert>\nextern const char* revision; extern int publicOtherSource; int main() {assert(publicOtherSource == 7); std::cout << revision;}\n')
  const script = `set -e
root="$1"
clang="$2"
clang_c="$2"
output_directory="$root/output"
d=""
force=0
syntax_only=0
newest_header_mtime=0
arch=host
platform=desktop
host=Linux
cflags=(-std=c++17)
stat_mtime() { printf '1'; }
onsignal() { exit 9; }
run_runtime_compiler() { printf 'compile\\n' >> "$root/compilations"; "$@"; }
runtime_metadata_cflags=(-DORO_RUNTIME_VERSION_HASH="$3" -DORO_RUNTIME_VERSION=0.1.8)
for source in "$root/src/runtime/version.cc" "$root/src/runtime/version.cpp"; do
${compile}
done
objects=("$output_directory/runtime/version.cc.o" "$output_directory/runtime/version.cpp.o")
static_library="$root/libpublic.a"
${archiveCheck}
if (( build_static )); then
  ar rcs "$static_library" "\${objects[@]}"
  printf '%s\\n' "$archive_inputs" > "$archive_inputs_file"
  printf 'archive\\n' >> "$root/archives"
fi
"$clang" "$root/main.cc" "$static_library" -o "$root/probe"
"$root/probe"
`
  const run = revision => spawnSync('bash', ['-s', '--', root, compiler, revision], { input: script, encoding: 'utf8', timeout: 60000 })
  const before = run('old_revision')
  assert.equal(before.status, 0, before.stdout + before.stderr)
  assert.ok(before.stdout.endsWith('old_revision'))
  const after = run('new_revision')
  assert.equal(after.status, 0, after.stderr)
  assert.ok(after.stdout.endsWith('new_revision'))
  const cached = run('new_revision')
  assert.equal(cached.status, 0, cached.stderr)
  assert.ok(cached.stdout.endsWith('new_revision'))
  assert.equal(readFileSync(path.join(root, 'compilations'), 'utf8'), 'compile\ncompile\ncompile\n')
  assert.equal(readFileSync(path.join(root, 'archives'), 'utf8'), 'archive\narchive\n')
})
