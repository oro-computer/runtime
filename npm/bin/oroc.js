#!/usr/bin/env node

import { fork } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath, pathToFileURL } from 'node:url'

const modulePath = fileURLToPath(import.meta.url)
const moduleRealPath = (() => {
  try {
    return fs.realpathSync(modulePath)
  } catch {
    return modulePath
  }
})()
const dirname = path.dirname(modulePath)
const invokedDirectly = (() => {
  const scriptArg = process.argv[1]
  if (!scriptArg) {
    return false
  }
  try {
    const scriptRealPath = fs.realpathSync(scriptArg)
    return scriptRealPath === moduleRealPath
  } catch {
    // Fallback: compare resolved file URLs in case the runtime can't realpath
    try {
      return (
        pathToFileURL(scriptArg).href === pathToFileURL(moduleRealPath).href
      )
    } catch {
      return false
    }
  }
})()

let exiting = false

function isModuleNotFound (err) {
  return (
    err &&
    (err.code === 'ERR_MODULE_NOT_FOUND' ||
      /Cannot find module/.test(err.message || ''))
  )
}

export async function load () {
  const platform = os.platform()
  const arch = os.arch()
  const candidates = [`@oro-computer/runtime-${platform}-${arch}`]

  let lastError
  for (const specifier of candidates) {
    try {
      const info = await import(specifier)
      return info?.default ?? info ?? null
    } catch (err) {
      if (isModuleNotFound(err)) {
        lastError = err
        continue
      }
      throw err
    }
  }

  const attempted = candidates.join(', ')
  const error = new Error(
    `Unable to locate Oro Runtime binaries. Tried: ${attempted}`
  )
  error.cause = lastError
  throw error
}

export async function run () {
  const installation = await load()
  const args = process.argv.slice(2)
  const env = {
    ...installation.env,
    ...process.env
  }

  const runtimeApiDir = path.dirname(dirname)
  if (!env.ORO_HOME_API) {
    env.ORO_HOME_API = runtimeApiDir
  }

  if (
    typeof installation.firstTimeExperienceSetup === 'function' &&
    !(await installation.firstTimeExperienceSetup())
  ) {
    // FTE not completed satisfactorily, or 'setup' was run externally
    return
  }

  const child = fork(installation.bin['oroc-platform'], args, {
    env,
    stdio: 'inherit',
    windowsHide: true
  })

  child.once('exit', (code) => {
    if (!exiting) {
      exiting = true
      process.exit(code ?? 1)
    }
  })

  child.once('error', (err) => {
    console.error(err.message)
    if (!exiting) {
      exiting = true
      process.exit(1)
    }
  })

  process.on('SIGTERM', () => {
    child.kill('SIGTERM')
  })

  process.on('exit', () => {
    child.kill('SIGTERM')
  })
}

if (invokedDirectly) {
  run().catch((err) => {
    console.error(err)
    process.exit(1)
  })
}
