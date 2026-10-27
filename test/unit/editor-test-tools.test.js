import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import { tmpdir } from 'node:os'
import { execFileSync } from 'node:child_process'
import { runInNewContext } from 'node:vm'
import { stageEditorBackends } from '../../bin/editor-test-tools.js'
import { preflightSourceKit, resolveProgram } from '../../src/cli/tooling/servers.cjs'

const installer = new URL('../../bin/install-editor-test-tools.js', import.meta.url)
const compilerNames = ['clang', 'clang++', 'ld', 'swiftc']
const lineEndings = { LF: '\n', CRLF: '\r\n' }

function executable (directory, name, output) {
  fs.mkdirSync(directory, { recursive: true })
  const filename = path.join(directory, name)
  fs.writeFileSync(filename, `#!/bin/sh\nprintf '%s\\n' '${output}'\n`, { mode: 0o755 })
  return filename
}

async function installFixture (platform, arch, lineEnding) {
  const root = fs.realpathSync.native(fs.mkdtempSync(path.join(tmpdir(), 'oro editor tools ')))
  const directory = path.join(root, 'installation')
  const system = path.join(root, 'system bin')
  const llvm = path.join(root, 'LLVM installation')
  const xcode = path.join(root, 'Xcode installation', 'toolchain', 'bin')
  for (const name of compilerNames) {
    executable(system, name, `system ${name}`)
    executable(path.join(llvm, 'bin'), name, `unexpected LLVM ${name}`)
    executable(xcode, name, `unexpected Xcode ${name}`)
  }
  const sourcekit = executable(xcode, 'sourcekit-lsp', 'selected SourceKit --default-workspace-type --compilation-db-search-path')
  executable(path.join(llvm, 'bin'), 'clangd', 'selected clangd')
  const githubPath = path.join(root, 'github path')
  const environment = { ...process.env, PATH: system, GITHUB_PATH: githubPath }
  const source = fs.readFileSync(installer, 'utf8')
    .replace(/\r?\n/g, lineEnding)
    .replace(/^#![^\r\n]*(?:\r?\n|$)/, '')
    .replace(/^import [^\r\n]*(?:\r?\n|$)/gm, '')
    .replaceAll('import.meta.url', JSON.stringify(installer.href))
  await runInNewContext(`(async () => { ${source}\n })()`, {
    fs,
    path,
    Buffer,
    URL,
    stageEditorBackends,
    resolveProgram,
    preflightSourceKit,
    process: { platform, arch, argv: [process.execPath, installer.pathname, directory], execPath: process.execPath, env: environment },
    fetch: async () => ({ ok: true, arrayBuffer: async () => new ArrayBuffer(0) }),
    execute: async (command, args) => {
      if (command === process.execPath && args.includes('install')) {
        executable(path.join(directory, 'node_modules', '.bin'), 'typescript-language-server', 'selected TypeScript')
      } else if (command === 'xcrun' && args.join(' ') === '--find sourcekit-lsp') {
        return sourcekit
      } else if (command === 'brew' && args.join(' ') === '--prefix llvm') {
        return llvm
      } else if (command === 'brew' && args.join(' ') === 'install llvm') {
        return ''
      } else if (command === 'sudo' && args.join(' ') === 'apt-get update') {
        return ''
      } else if (command === 'sudo' && args.join(' ') === 'apt-get install -y clangd') {
        executable(system, 'clangd', 'selected clangd')
      } else if (command === 'unzip') {
        const bin = path.join(directory, 'clangd_19.1.2', 'bin')
        executable(bin, 'clangd', 'selected clangd')
        for (const name of compilerNames) executable(bin, name, `unexpected archive ${name}`)
      } else {
        throw new Error(`Unexpected installer command: ${command} ${args.join(' ')}`)
      }
      return ''
    }
  })
  const published = fs.readFileSync(githubPath, 'utf8').trim().split('\n')
  return { directory, system, published, environment: { ...environment, PATH: [...published].reverse().concat(system).join(path.delimiter) } }
}

test('native editor installation preserves compiler and linker selection', { skip: process.platform === 'win32' }, async t => {
  for (const platform of ['darwin', 'linux']) {
    for (const arch of ['x64', 'arm64']) {
      for (const [format, lineEnding] of Object.entries(lineEndings)) {
        await t.test(`${platform} ${arch} ${format}`, async () => {
          const { published, environment, system } = await installFixture(platform, arch, lineEnding)
          for (const name of compilerNames) {
            assert.equal(execFileSync(name, [], { env: environment, encoding: 'utf8' }).trim(), `system ${name}`)
            assert.equal(resolveProgram(name, environment), fs.realpathSync(path.join(system, name)))
            for (const directory of published) assert.equal(fs.existsSync(path.join(directory, name)), false)
          }
          for (const [name, expected] of [['clangd', 'selected clangd'], ['typescript-language-server', 'selected TypeScript']]) {
            assert.equal(execFileSync(name, [], { env: environment, encoding: 'utf8' }).trim(), expected)
          }
          if (platform === 'darwin') {
            assert.match(execFileSync('sourcekit-lsp', [], { env: environment, encoding: 'utf8' }), /^selected SourceKit /)
          }
        })
      }
    }
  }
})

test('Windows editor installation publishes only the web editor launchers', async t => {
  for (const [format, lineEnding] of Object.entries(lineEndings)) {
    await t.test(format, async () => {
      const { published, directory } = await installFixture('win32', 'x64', lineEnding)
      assert.deepEqual(published, [path.join(directory, 'node_modules', '.bin')])
    })
  }
})

test('backend staging rejects missing executables and compiler publication', () => {
  const directory = fs.mkdtempSync(path.join(tmpdir(), 'oro editor staging '))
  assert.equal(stageEditorBackends(directory, {}), null)
  assert.throws(() => stageEditorBackends(directory, { clangd: path.join(directory, 'missing') }), /ENOENT/)
  assert.throws(() => stageEditorBackends(directory, { 'clang++': process.execPath }), /Unsupported editor backend: clang\+\+/)
  assert.deepEqual(fs.readdirSync(directory), [])
})
