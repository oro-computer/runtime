import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('Android buffered responses expose their full length before WebView reads them', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/webview/scheme_handlers.kt', import.meta.url), 'utf8')
  const helperStart = source.indexOf('internal class BufferedResponseInputStream')
  const helperEnd = source.indexOf('open class SchemeHandlers')
  const responseStart = source.indexOf('  open class Response (')
  const responseEnd = source.indexOf('\n  fun handleRequest (webResourceRequest:', responseStart)
  assert.ok(responseStart >= 0 && responseEnd > responseStart)
  const helper = helperStart >= 0 ? source.slice(helperStart, helperEnd) : ''
  const response = source.slice(responseStart, responseEnd)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-response-'))
  const filename = path.join(directory, 'Regression.kt')
  const jar = path.join(directory, 'regression.jar')
  writeFileSync(filename, `
    import java.io.*
    import java.util.Collections
    import java.util.concurrent.Semaphore
    import kotlin.concurrent.thread
    class Request { fun getCookieUrl() = "https://test/" }
    object App {
      fun getInstance() = this
      fun hasRuntimePermission(name: String) = false
    }
    object Build {
      object VERSION { const val SDK_INT = 34 }
      object VERSION_CODES { const val LOLLIPOP = 21 }
    }
    object CookieManager {
      fun getInstance() = this
      fun setCookie(url: String, value: String) {}
      fun flush() {}
    }
    class WebResourceResponse(type: String, encoding: String?, input: InputStream?) {
      private var input = input
      val data get() = input!!
      var responseHeaders = mutableMapOf<String, String>()
      fun setData(value: InputStream) { input = value }
      fun setStatusCodeAndReasonPhrase(code: Int, text: String) {}
      @JvmName("applyResponseHeaders")
      fun setResponseHeaders(value: Map<String, String>) { responseHeaders.putAll(value) }
      fun setMimeType(value: String) {}
      fun setEncoding(value: String?) {}
    }
    ${helper}
    ${response}
    fun main() {
      // WebView uses available() to derive Content-Length before its first read.
      // The IPC decoder uses that header to limit the returned binary buffer.
      val payload = ByteArray(65537) { (it % 256).toByte() }
      for (chunks in listOf(listOf(payload), listOf(
        byteArrayOf(), payload.copyOfRange(0, 512), byteArrayOf(),
        payload.copyOfRange(512, 3072), payload.copyOfRange(3072, payload.size)
      ))) {
        val response = Response(Request())
        chunks.forEach { response.write(it) }
        response.finish()
        response.waitForFinish()
        val input = response.response.data
        check(input.available() == payload.size) { "WebView sees truncated Content-Length: " + input.available() }
        check(input.read() == 0)
        check(input.available() == payload.size - 1)
        check(input.skip(255) == 255L)
        check(input.available() == payload.size - 256)
        val bytes = input.readBytes()
        check(bytes.contentEquals(payload.copyOfRange(256, payload.size)))
        check(input.available() == 0 && input.read() == -1)
        check(input.read(ByteArray(0), 0, 0) == 0)
        input.close()
        check(input.available() == 0)
      }
      val empty = Response(Request())
      empty.finish()
      empty.waitForFinish()
      check(empty.response.data.available() == 0)
      check(empty.response.data.read() == -1)
      println("Android response stream passed")
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
