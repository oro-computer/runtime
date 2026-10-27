'use strict'

const { execute } = require('./tooling/process.cjs')

/**
 * Read validated Android SDK metadata through the native CLI's private channel.
 * @param {string} executable
 * @param {import('./tooling/process.cjs').CommandOptions} [options]
 * @returns {Promise<object>}
 */
async function readAndroidSdk (executable, options = {}) {
  const output = await execute(executable, [], {
    cwd: options.cwd,
    env: {
      ...(options.env || process.env),
      ORO_INTERNAL_ANDROID_SDK_METADATA: '1',
      ORO_LOG_JSON: '',
      ORO_LOG_FILE: ''
    },
    timeout: 60000
  })
  let metadata
  try { metadata = JSON.parse(output) } catch (error) {
    throw new Error('Cannot read Android SDK metadata. Rebuild or reinstall the Oro CLI and SDK together.', { cause: error })
  }
  if (metadata?.schema !== 1 || !Array.isArray(metadata.sources) || !Array.isArray(metadata.apis)) {
    throw new Error('Unsupported Android SDK metadata. Rebuild or reinstall the Oro CLI and SDK together.')
  }
  return metadata
}

module.exports = { readAndroidSdk }
