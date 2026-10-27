import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { spawnSync } from 'node:child_process'
import path from 'node:path'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('native Web Push decrypts RFC 8291 section 5 and rejects altered ciphertext, keys, points and framing', {
  skip: !available ? 'Kotlin, a JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-push-crypto-'))
  const filename = path.join(directory, 'Crypto.kt')
  const jar = path.join(directory, 'crypto.jar')
  const source = readFileSync(new URL('../../src/runtime/app/push_crypto.kt', import.meta.url), 'utf8')
    .replace(/^import android\..*\n/gm, '')
  writeFileSync(filename, source + `
class PlatformFailure(val code: String): Exception(code)
object Base64 {
  const val URL_SAFE = 1; const val NO_WRAP = 2; const val NO_PADDING = 4
  fun encodeToString(value: ByteArray, flags: Int) = java.util.Base64.getUrlEncoder().withoutPadding().encodeToString(value)
  fun decode(value: String, flags: Int) = java.util.Base64.getUrlDecoder().decode(value)
}
object KeyProperties {
  const val KEY_ALGORITHM_AES = "AES"; const val PURPOSE_ENCRYPT = 1; const val PURPOSE_DECRYPT = 2
  const val BLOCK_MODE_GCM = "GCM"; const val ENCRYPTION_PADDING_NONE = "NoPadding"
}
class KeyGenParameterSpec: java.security.spec.AlgorithmParameterSpec {
  class Builder(alias: String, purposes: Int) {
    fun setBlockModes(mode: String) = this
    fun setEncryptionPaddings(padding: String) = this
    fun build() = KeyGenParameterSpec()
  }
}
fun main() {
  check(PushCryptography::class.java.getDeclaredMethod("open", String::class.java, String::class.java) != null)
  check(PushCryptography::class.java.getDeclaredMethod("seal", JSONObject::class.java, String::class.java, Boolean::class.javaPrimitiveType) != null)
  // Interoperability vector: https://www.rfc-editor.org/rfc/rfc8291.html#section-5
  val parameters = AlgorithmParameters.getInstance("EC").apply { init(ECGenParameterSpec("secp256r1")) }
    .getParameterSpec(ECParameterSpec::class.java)
  val private = KeyFactory.getInstance("EC").generatePrivate(java.security.spec.ECPrivateKeySpec(
    BigInteger(1, PushCryptography.decode("q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94")), parameters))
  val keys = JSONObject().put("privateKey", PushCryptography.encode(private.encoded))
    .put("auth", "BTBZMqHH6r4Tts7J_aSIgg")
    .put("p256dh", "BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4")
  val message = PushCryptography.decode(
    "DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27ml" +
    "mlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPT" +
    "pK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN")
  check(PushCryptography.decrypt(message, keys).toString(Charsets.UTF_8) == "When I grow up, I want to be a watermelon")
  val corrupt = message.copyOf().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
  check(runCatching { PushCryptography.decrypt(corrupt, keys) }.isFailure)
  check(runCatching { PushCryptography.decrypt(message.copyOf(102), keys) }.isFailure)
  check(runCatching { PushCryptography.decrypt(message.copyOf().also { it[20] = 64 }, keys) }.isFailure)
  check(runCatching { PushCryptography.decrypt(message.copyOf().also { it[21] = 0 }, keys) }.isFailure)
  check(runCatching { PushCryptography.decrypt(message.copyOf().also { it[19] = 1; it[18] = 0 }, keys) }.isFailure)
  check(runCatching { PushCryptography.decrypt(message, JSONObject(keys.toString()).put("auth", PushCryptography.encode(ByteArray(16)))) }.isFailure)
  check(runCatching { PushCryptography.validatePublicKey(PushCryptography.encode(byteArrayOf(4) + ByteArray(64))) }.isFailure)
  check(runCatching { PushCryptography.decode("not+base64url") }.isFailure)
}
`)
  const compiled = spawnSync('kotlinc', [filename, '-classpath', [stdlib, json].join(path.delimiter), '-d', jar], {
    encoding: 'utf8', timeout: 60000
  })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib, json].join(path.delimiter), 'oro.runtime.app.CryptoKt'], {
    encoding: 'utf8', timeout: 15000
  })
  assert.equal(result.status, 0, result.stderr)
})
