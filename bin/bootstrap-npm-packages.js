#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import {
  cpSync,
  mkdtempSync,
  mkdirSync,
  rmSync,
  writeFileSync
} from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const registry = 'https://registry.npmjs.org'
const bootstrapVersion = '0.0.0-trusted-publishing-bootstrap.0'
const packages = [
  '@oro-computer/runtime-linux-x64',
  '@oro-computer/runtime-linux-arm64',
  '@oro-computer/runtime-darwin-x64',
  '@oro-computer/runtime-darwin-arm64',
  '@oro-computer/runtime-win32-x64',
  '@oro-computer/runtime-node',
  '@oro-computer/runtime'
]

const args = new Set(process.argv.slice(2))
if (args.has('--help') || args.has('-h')) {
  console.log(`Usage: node bin/bootstrap-npm-packages.js [--publish --yes]

Inspect whether the Oro Runtime npm package names already exist. With both
--publish and --yes, reserve missing names using the prerelease version
${bootstrapVersion} and the non-default "bootstrap" dist-tag.

This one-time account bootstrap uses the npm identity already authenticated on
the local machine. It is never called by GitHub Actions and never consumes a
release version.`)
  process.exit(0)
}

for (const arg of args) {
  if (arg !== '--publish' && arg !== '--yes') {
    console.error(`Unknown option: ${arg}`)
    process.exit(2)
  }
}

const publish = args.has('--publish')
if (publish && !args.has('--yes')) {
  console.error(
    'Refusing to publish package reservations without both --publish and --yes'
  )
  process.exit(2)
}
if (!publish && args.has('--yes')) {
  console.error('--yes is only valid together with --publish')
  process.exit(2)
}

const missing = []
for (const packageName of packages) {
  const result = runNpm(['view', packageName, 'name', '--json'], {
    stdio: ['ignore', 'pipe', 'pipe']
  })
  if (result.status === 0) {
    console.log(`exists - ${packageName}`)
  } else if (String(result.stderr).includes('E404')) {
    console.log(`missing - ${packageName}`)
    missing.push(packageName)
  } else {
    if (result.stderr) process.stderr.write(result.stderr)
    if (result.error) console.error(result.error.message)
    console.error(`Unable to inspect ${packageName}`)
    process.exit(result.status || 1)
  }
}

if (missing.length === 0) {
  console.log('ok - all Oro Runtime npm package names exist')
  process.exit(0)
}

if (!publish) {
  console.log(
    `Run again with --publish --yes to reserve ${missing.length} missing package name${missing.length === 1 ? '' : 's'}`
  )
  process.exit(1)
}

const identity = runNpm(['whoami'], {
  stdio: ['ignore', 'pipe', 'inherit']
})
if (identity.status !== 0) {
  console.error('Authenticate the intended npm maintainer with npm login first')
  process.exit(identity.status || 1)
}
console.log(`npm identity - ${String(identity.stdout).trim()}`)

const stagingRoot = mkdtempSync(path.join(os.tmpdir(), 'oro-npm-bootstrap-'))
try {
  for (const packageName of missing) {
    const packageRoot = path.join(
      stagingRoot,
      packageName.replace('@oro-computer/', '')
    )
    mkdirSync(packageRoot, { recursive: true })
    writeFileSync(
      path.join(packageRoot, 'package.json'),
      `${JSON.stringify(
        {
          name: packageName,
          version: bootstrapVersion,
          description: 'Package-name reservation for Oro Runtime trusted publishing',
          license: 'Apache-2.0',
          repository: {
            type: 'git',
            url: 'git+https://github.com/oro-computer/runtime.git'
          },
          publishConfig: {
            access: 'public',
            tag: 'bootstrap'
          }
        },
        null,
        2
      )}\n`
    )
    writeFileSync(
      path.join(packageRoot, 'README.md'),
      `# ${packageName}\n\nThis prerelease reserves the package name for Oro Runtime trusted publishing. Install a supported release version instead.\n`
    )
    cpSync(path.join(root, 'LICENSE.txt'), path.join(packageRoot, 'LICENSE'))

    console.log(`publishing reservation - ${packageName}@${bootstrapVersion}`)
    const result = runNpm(
      ['publish', packageRoot, '--access', 'public', '--tag', 'bootstrap'],
      { stdio: 'inherit' }
    )
    if (result.status !== 0) process.exitCode = result.status || 1
    if (process.exitCode) break
  }
} finally {
  rmSync(stagingRoot, { recursive: true, force: true })
}

if (!process.exitCode) {
  console.log(
    'ok - package names are reserved; configure trusted publishing before pushing v0.1.0'
  )
}

function runNpm (npmArgs, options) {
  return spawnSync(
    process.platform === 'win32' ? 'npm.cmd' : 'npm',
    [...npmArgs, '--registry', registry],
    {
      ...options,
      env: {
        ...process.env,
        npm_config_loglevel: 'error'
      }
    }
  )
}
