import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
const sodium = process.env.ORO_TEST_SODIUM_LIBRARY || path.join(libraries, 'libsodium.a')
const crypto = process.env.ORO_TEST_MBEDCRYPTO_LIBRARY || path.join(libraries, 'libmbedcrypto.a')
const sodiumHeaders = process.env.ORO_TEST_SODIUM_INCLUDE || path.join(root, 'build', `${architecture}-desktop/include`)
const cryptoHeaders = process.env.ORO_TEST_MBEDTLS_INCLUDE || path.join(root, 'build/include')
const compiler = process.env.ORO_TEST_CXX || 'clang++'
const available = fs.existsSync(crypto) && fs.existsSync(sodium) && fs.existsSync(path.join(cryptoHeaders, 'mbedtls/ecp.h'))

test('native Web Push matches the RFC 8291 vector and rejects tampering, malformed keys and records', {
  skip: available || process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS ? false : 'Requires libsodium and Mbed TLS development artifacts',
  timeout: 60000
}, () => {
  assert.ok(available)
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-web-push-'))
  const binary = path.join(directory, process.platform === 'win32' ? 'web-push.exe' : 'web-push')
  const built = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-DORO_RUNTIME_ENABLE_MBEDTLS=1',
    '-I' + root, '-I' + path.join(root, 'include'), '-I' + sodiumHeaders, '-I' + cryptoHeaders,
    path.join(root, 'src/runtime/native/web_push.cc'), path.join(root, 'test/fixtures/native-records/WebPush.cc'),
    sodium, crypto, '-pthread', '-o', binary
  ], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(binary, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /rfc8291-vector-and-authentication-rejection-passed/)
})
