import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const dependenciesAvailable = ['acorn', 'acorn-walk'].every(name => {
  try {
    import.meta.resolve(name)
    return true
  } catch {
    return false
  }
})

function snapshot (directory) {
  return readdirSync(directory, { recursive: true }).sort().flatMap(name => {
    const filename = path.join(directory, name)
    const stat = statSync(filename)
    return stat.isFile() ? [[name, stat.mtimeMs, createHash('sha256').update(readFileSync(filename)).digest('hex')]] : []
  })
}

test('documentation checks detect missing, stale, and obsolete output without writing files', {
  skip: dependenciesAvailable ? false : 'Documentation checks require installed acorn dependencies'
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-docs-check-'))
  for (const name of ['api', 'include/oro', 'include/iroh', 'src/cli/templates.hh', 'VERSION.txt', 'npm/packages/@oro-computer/runtime-node/index.js']) {
    const destination = path.join(directory, name)
    mkdirSync(path.dirname(destination), { recursive: true })
    cpSync(path.join(root, name), destination, { recursive: true })
  }
  const run = (...args) => spawnSync(process.execPath, [path.join(root, 'bin/generate-docs.js'), ...args], {
    cwd: directory,
    encoding: 'utf8',
    env: { ...process.env, DOCS_CLI_NAME: 'oroc' },
    timeout: 60000
  })

  const missing = run('--check')
  assert.equal(missing.status, 1, missing.stderr)
  assert.ok(missing.stderr.includes(path.join('share/man/man7/oro-ipc-routes.7')), missing.stderr)
  assert.equal(existsSync(path.join(directory, 'share')), false, 'check does not create output directories')

  const generated = run()
  assert.equal(generated.status, 0, generated.stderr)
  const clean = snapshot(directory)
  const current = run('--check')
  assert.equal(current.status, 0, current.stderr)
  assert.deepEqual(snapshot(directory), clean, 'successful checks do not rewrite files')

  const implementation = path.join(directory, 'api/mcp/index.js')
  writeFileSync(implementation, '\n' + readFileSync(implementation, 'utf8'))
  writeFileSync(path.join(directory, 'api/README.md'), 'outdated documentation\n')
  writeFileSync(path.join(directory, 'share/man/man1/oroc-obsolete.1'), 'obsolete page\n')
  writeFileSync(path.join(directory, 'share/man/man1/local-notes.txt'), 'keep local notes\n')
  const before = snapshot(directory)
  const stale = run('--check')
  assert.equal(stale.status, 1, stale.stderr)
  for (const filename of ['api/README.md', 'share/man/man7/oro-ipc-routes.7', 'share/man/man1/oroc-obsolete.1']) {
    assert.ok(stale.stderr.includes(path.normalize(filename)), stale.stderr)
  }
  assert.match(stale.stderr, /npm run gen:docs/)
  assert.deepEqual(snapshot(directory), before, 'failed checks preserve all files and modification times')

  const repaired = run()
  assert.equal(repaired.status, 0, repaired.stderr)
  assert.equal(existsSync(path.join(directory, 'share/man/man1/oroc-obsolete.1')), false)
  assert.equal(readFileSync(path.join(directory, 'share/man/man1/local-notes.txt'), 'utf8'), 'keep local notes\n')
  const final = run('--check')
  assert.equal(final.status, 0, final.stderr)
})
