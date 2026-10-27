import { execFileSync } from 'node:child_process'

/**
 * Selects one connected device; ambiguous or unavailable selections fail before mutation.
 * @param {string} output Output from adb devices.
 * @param {string} [requested] Explicit ANDROID_SERIAL.
 * @returns {string}
 */
export function selectAndroidSerial (output, requested) {
  const devices = output.split(/\r?\n/).map(line => line.trim().split(/\s+/))
    .filter(fields => fields[1] === 'device').map(fields => fields[0])
  if (requested) {
    if (!devices.includes(requested)) throw new Error(`Android target ${requested} is not connected and authorized`)
    return requested
  }
  if (devices.length !== 1) throw new Error('Set ANDROID_SERIAL to one connected and authorized Android target')
  return devices[0]
}

/**
 * Extract fixtures as the application UID so tests can modify them.
 * @param {string} adb
 * @param {string} id
 * @param {string} fixtures
 * @param {string} serial Explicit device identity.
 * @returns {void}
 */
export function stageAndroidFixtures (adb, id, fixtures, serial) {
  if (!serial) throw new TypeError('Android fixture staging requires an explicit serial')
  const destination = 'cache/oro-test-fixtures'
  execFileSync(adb, ['-s', serial, 'shell', 'run-as', id, 'mkdir', '-p', destination], {
    stdio: 'inherit'
  })
  const archive = execFileSync('tar', ['-cf', '-', '-C', fixtures, '.'], {
    maxBuffer: 16 * 1024 * 1024
  })
  execFileSync(adb, ['-s', serial, 'shell', '-T', 'run-as', id, 'tar', '-xf', '-', '-C', destination], {
    input: archive,
    stdio: ['pipe', 'inherit', 'inherit']
  })
}
