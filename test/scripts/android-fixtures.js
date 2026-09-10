import { execFileSync } from 'node:child_process'

/**
 * Extract fixtures as the application UID so tests can modify them.
 * @param {string} adb
 * @param {string} id
 * @param {string} fixtures
 * @returns {void}
 */
export function stageAndroidFixtures (adb, id, fixtures) {
  const destination = 'cache/oro-test-fixtures'
  execFileSync(adb, ['shell', 'run-as', id, 'mkdir', '-p', destination], {
    stdio: 'inherit'
  })
  const archive = execFileSync('tar', ['-cf', '-', '-C', fixtures, '.'], {
    maxBuffer: 16 * 1024 * 1024
  })
  execFileSync(adb, ['shell', '-T', 'run-as', id, 'tar', '-xf', '-', '-C', destination], {
    input: archive,
    stdio: ['pipe', 'inherit', 'inherit']
  })
}
