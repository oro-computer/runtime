import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

function runCli (command, args, options = {}) {
  return spawnSync(command, args, {
    encoding: 'utf8',
    shell: process.platform === 'win32',
    ...options
  })
}

const runOroc = (args, options = {}) => runCli('oroc', args, options)

function assert (cond, msg) {
  if (!cond) {
    console.error('Assertion failed:', msg)
    process.exit(1)
  }
}

function createTempProject (prefix) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), prefix))
  return root
}

function getLinuxResourcesDir (cwd) {
  const { status, stdout } = runOroc(['print-build-dir', '--platform=linux'], {
    cwd
  })
  assert(status === 0, 'print-build-dir --platform=linux should exit 0')
  const dir = (stdout || '').trim()
  assert(dir.length > 0, 'print-build-dir should return a non-empty path')
  return dir
}

// 1) Quick-build: no src/, root index.html + index.js + index.css, ignore others
{
  const projectRoot = createTempProject('oro-implicit-root-html-')

  fs.writeFileSync(
    path.join(projectRoot, 'index.html'),
    '<!doctype html><html><head><meta charset="utf-8"><title>implicit</title></head><body><h1>hello</h1></body></html>',
    'utf8'
  )
  fs.writeFileSync(
    path.join(projectRoot, 'index.js'),
    'console.log("implicit index.js")\n',
    'utf8'
  )
  fs.writeFileSync(
    path.join(projectRoot, 'index.css'),
    'body { background: pink; }\n',
    'utf8'
  )
  fs.writeFileSync(
    path.join(projectRoot, 'ignored.txt'),
    'this file should not be copied\n',
    'utf8'
  )

  const build = runOroc(['build', '--platform=linux', '-o'], {
    cwd: projectRoot
  })
  assert(
    build.status === 0,
    'implicit root build with index.html should exit 0'
  )

  const resourcesDir = getLinuxResourcesDir(projectRoot)
  const expect = (rel) => fs.existsSync(path.join(resourcesDir, rel))

  assert(expect('index.html'), 'index.html should be copied into resources dir')
  assert(expect('index.js'), 'index.js should be copied into resources dir')
  assert(expect('index.css'), 'index.css should be copied into resources dir')
  assert(
    !expect('ignored.txt'),
    'non-index file in root should not be copied by implicit quick-build'
  )
}

// 2) Quick-build: no src/, index.js + index.css only, implicit HTML loads both
{
  const projectRoot = createTempProject('oro-implicit-root-js-css-')

  fs.writeFileSync(
    path.join(projectRoot, 'index.js'),
    'console.log("implicit js only")\n',
    'utf8'
  )
  fs.writeFileSync(
    path.join(projectRoot, 'index.css'),
    'body { color: red; }\n',
    'utf8'
  )

  const build = runOroc(['build', '--platform=linux', '-o'], {
    cwd: projectRoot
  })
  assert(
    build.status === 0,
    'implicit root build with index.js + index.css should exit 0'
  )

  const resourcesDir = getLinuxResourcesDir(projectRoot)
  const htmlPath = path.join(resourcesDir, 'index.html')
  const jsPath = path.join(resourcesDir, 'index.js')
  const cssPath = path.join(resourcesDir, 'index.css')

  assert(fs.existsSync(htmlPath), 'implicit index.html should be generated')
  assert(fs.existsSync(jsPath), 'index.js should be copied into resources dir')
  assert(
    fs.existsSync(cssPath),
    'index.css should be copied into resources dir'
  )

  const html = fs.readFileSync(htmlPath, 'utf8')
  assert(
    html.includes('type="module"') && html.includes('src="./index.js"'),
    'implicit HTML should load index.js as a module'
  )
  assert(
    html.includes('rel="stylesheet"') && html.includes('href="./index.css"'),
    'implicit HTML should link index.css when present'
  )
}

// 3) Quick-build error: no src/, only index.css should fail (no entry point)
{
  const projectRoot = createTempProject('oro-implicit-root-css-only-')

  fs.writeFileSync(
    path.join(projectRoot, 'index.css'),
    'body { color: blue; }\n',
    'utf8'
  )

  const build = runOroc(['build', '--platform=linux', '-o'], {
    cwd: projectRoot
  })
  assert(
    build.status !== 0,
    'implicit root build with only index.css should fail (no entry point)'
  )
  const stderr = (build.stderr || '').toLowerCase()
  assert(
    stderr.includes("expected 'index.html' or 'index.js'") ||
      stderr.includes("expected 'index.html' or 'index.js'".toLowerCase()),
    'error should mention missing index.html/index.js entry'
  )
}

// 4) --copy overlay: quick-build root with index.html and extra.txt
{
  const projectRoot = createTempProject('oro-implicit-root-copy-')

  fs.writeFileSync(
    path.join(projectRoot, 'index.html'),
    '<!doctype html><html><head><meta charset="utf-8"><title>copy-flag</title></head><body></body></html>',
    'utf8'
  )
  fs.writeFileSync(
    path.join(projectRoot, 'extra.txt'),
    'extra content\n',
    'utf8'
  )

  const build = runOroc(
    ['build', '--platform=linux', '-o', '--copy', 'extra.txt:extra.txt'],
    { cwd: projectRoot }
  )
  assert(
    build.status === 0,
    'implicit root build with --copy extra.txt:extra.txt should exit 0'
  )

  const resourcesDir = getLinuxResourcesDir(projectRoot)
  const extraPath = path.join(resourcesDir, 'extra.txt')
  assert(
    fs.existsSync(extraPath),
    '--copy extra.txt:extra.txt should copy extra.txt into resources dir'
  )
}

console.log('cli implicit build tests passed')
