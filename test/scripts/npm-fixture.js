import { mkdirSync, mkdtempSync, writeFileSync } from 'node:fs'
import path from 'node:path'

/**
 * Allow npm startup and filesystem work on hosted runners without permitting an
 * unbounded fixture command.
 * @type {number}
 */
export const NPM_FIXTURE_TIMEOUT = 120000

/**
 * Give local npm fixtures private configuration and cache without inherited
 * registry credentials, lifecycle settings, or network access.
 * @param {string} directory Parent directory for the private configuration.
 * @param {NodeJS.ProcessEnv} [source=process.env] Environment to copy.
 * @returns {NodeJS.ProcessEnv}
 */
export function createNpmFixtureEnvironment (directory, source = process.env) {
  const environment = Object.fromEntries(Object.entries(source).filter(([key]) => {
    return !/^(npm_config_|npm_package_)/i.test(key) &&
      !/^(NODE_AUTH_TOKEN|NODE_OPTIONS)$/i.test(key) &&
      !/^npm_.*(auth|token|password|username)/i.test(key)
  }))
  mkdirSync(directory, { recursive: true })
  const configuration = mkdtempSync(path.join(directory, 'npm-configuration-'))
  const userconfig = path.join(configuration, 'user.npmrc')
  const globalconfig = path.join(configuration, 'global.npmrc')
  writeFileSync(userconfig, '')
  writeFileSync(globalconfig, '')
  return {
    ...environment,
    npm_config_userconfig: userconfig,
    npm_config_globalconfig: globalconfig,
    npm_config_cache: path.join(configuration, 'cache'),
    npm_config_offline: 'true',
    npm_config_audit: 'false',
    npm_config_fund: 'false',
    npm_config_update_notifier: 'false',
    npm_config_ignore_scripts: 'false'
  }
}
