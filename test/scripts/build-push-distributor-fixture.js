import { mkdir, mkdtemp, readFile, writeFile, copyFile, rm } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import distributor from '../../src/cli/push-distributor.cjs'

// Compile the connector with the unmodified native RFC 8291 implementation.
// It is a separate public fixture APK, never a production runtime component.
const root = fileURLToPath(new URL('../../', import.meta.url))
const controller = new AbortController()
let interrupted = 0
function stop (signal) {
  if (interrupted) return
  interrupted = signal === 'SIGINT' ? 130 : 143
  controller.abort(new Error('Public fixture build interrupted'))
}
process.on('SIGINT', stop)
process.on('SIGTERM', stop)

async function main () {
  const options = distributor.parseOptions(process.argv.slice(2))
  if (options.install) throw new Error('Build the public fixture, then use test-push-distributor.js to install both test APKs')
  if (!process.argv.some(value => value === '--output' || value.startsWith('--output='))) {
    options.output = path.join(options['data-dir'], 'oro-push-public-check.apk')
  }
  await mkdir(options['data-dir'], { recursive: true, mode: 0o700 })
  const source = await mkdtemp(path.join(options['data-dir'], 'fixture-'))
  try {
    for (const name of ['settings.gradle', 'build.gradle']) {
      const text = await readFile(path.join(root, 'src/cli/push-distributor', name), 'utf8')
      await writeFile(path.join(source, name), text.replaceAll("'dev.oro.push'", "'dev.oro.push.qualification'"))
    }
    for (const name of ['AndroidManifest.xml', 'Check.kt', 'EnableDistributor.kt']) {
      await copyFile(path.join(root, 'test/fixtures/android-push-distributor', name), path.join(source, name))
    }
    await copyFile(path.join(root, 'src/runtime/app/push_crypto.kt'), path.join(source, 'PushCryptography.kt'))
    await copyFile(path.join(root, 'src/cli/push-distributor/Protocol.kt'), path.join(source, 'Protocol.kt'))
    await distributor.build({ ...options, sourceDirectory: source }, controller.signal)
  } finally {
    await rm(source, { recursive: true, force: true })
  }
}

try { await main() } catch (error) {
  if (!interrupted) console.error(error.message)
  process.exitCode = interrupted || 1
} finally {
  process.removeListener('SIGINT', stop)
  process.removeListener('SIGTERM', stop)
}
