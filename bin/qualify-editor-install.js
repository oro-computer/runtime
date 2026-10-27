#!/usr/bin/env node

import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { existsSync, readdirSync } from 'node:fs'
import { execute } from '../src/cli/tooling/process.cjs'

/**
 * Return the editor languages supported by this release on the selected host.
 * @param {string} [platform] Node.js host platform name.
 * @returns {string[]}
 */
export function editorQualificationLanguages (platform = process.platform) {
  return ['typescript', ...(platform === 'win32' ? [] : ['c', 'cpp']), ...(platform === 'darwin' ? ['objc', 'objcpp', 'swift'] : [])]
}

/** Qualify the installed CLI and native backends required for its host/SDK. */
export async function qualifyEditorInstall (executable, sdk, api, environment) {
  const root = fileURLToPath(new URL('../', import.meta.url))
  const env = { ...environment, OROC: executable, ORO_LANGUAGE_TEST_OROC: executable, ORO_HOME: sdk, ORO_HOME_API: api, ORO_NODE_EXECUTABLE: process.execPath }
  if (process.platform === 'win32') console.log('Windows C/C++ language-server support is deferred for this release; TypeScript qualification remains required.')
  for (const language of editorQualificationLanguages()) {
    await execute(process.execPath, [path.join(root, 'test/scripts/test-language-tooling.js'), language], { cwd: root, env, log: true, timeout: 600000 })
  }
  await execute(process.execPath, [path.join(root, 'test/scripts/test-language-tooling.js'), 'typescript', '--inferred'], { cwd: root, env, log: true })
  if (existsSync(path.join(sdk, 'lib')) && readdirSync(path.join(sdk, 'lib')).some(name => name.endsWith('-android'))) {
    await execute(process.execPath, [path.join(root, 'test/scripts/test-kotlin-compatibility.js')], { cwd: root, env, log: true })
    await execute(process.execPath, [path.join(root, 'test/scripts/test-language-server.js')], { cwd: root, env, log: true, timeout: 1800000 })
  }
}
