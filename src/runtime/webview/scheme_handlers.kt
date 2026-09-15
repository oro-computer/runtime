// vim: set sw=2:
package oro.runtime.webview

import java.io.ByteArrayInputStream
import java.io.InputStream
import java.io.SequenceInputStream
import java.util.Collections
import java.util.concurrent.Semaphore

import android.os.Build
import android.webkit.CookieManager
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse

import oro.runtime.app.App
import oro.runtime.app.AppActivity
import oro.runtime.bridge.Bridge
import oro.runtime.debug.console
import oro.runtime.ipc.Message

// WebView derives Content-Length from available(). All response chunks are
// buffered before finish(), so report the entire body rather than a pipe's
// current capacity. Keep chunks separate to avoid copying the complete body.
internal class BufferedResponseInputStream (buffers: List<ByteArray>) : InputStream() {
  private val stream = SequenceInputStream(Collections.enumeration(
    buffers.map { ByteArrayInputStream(it) }
  ))
  private var remaining = buffers.sumOf { it.size.toLong() }

  @Synchronized
  override fun available (): Int = remaining.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()

  @Synchronized
  override fun read (): Int {
    val value = stream.read()
    if (value >= 0) remaining--
    return value
  }

  @Synchronized
  override fun read (buffer: ByteArray, offset: Int, length: Int): Int {
    if (offset < 0 || length < 0 || offset > buffer.size - length) {
      throw IndexOutOfBoundsException()
    }
    if (length == 0) return 0
    val count = stream.read(buffer, offset, length)
    if (count > 0) remaining -= count
    return count
  }

  @Synchronized
  override fun skip (count: Long): Long {
    val skipped = stream.skip(count)
    remaining -= skipped
    return skipped
  }

  @Synchronized
  override fun close () {
    stream.close()
    remaining = 0
  }
}

open class SchemeHandlers (val bridge: Bridge) {
  open class Request (val bridge: Bridge, val request: WebResourceRequest) {
    val response = Response(this)
    val body: ByteArray? by lazy {
      try {
        val seq = this.getHeader("runtime-xhr-seq")
          ?: this.request.url.getQueryParameter("seq")

        if (seq != null && this.bridge.buffers.contains(seq)) {
          val buffer = this.bridge.buffers[seq]
          if (request.method == "POST" || request.method == "PUT" || request.method == "PATCH") {
            this.bridge.buffers.remove(seq)
          }
          buffer
        } else {
          null
        }
      } catch (_: Exception) {
        null
      }
    }

    fun getScheme (): String {
      val url = this.request.url
      val app = App.getInstance()
      val bundleIdentifier = app.getUserConfigValue("meta_bundle_identifier")

      if (
        (url.scheme == "https" || url.scheme == "http") &&
        url.host == bundleIdentifier
      ) {
        return "oro"
      }

      return this.request.url.scheme ?: ""
    }

    fun getMethod (): String {
      return this.request.method ?: ""
    }

    fun getHostname (): String {
      return this.request.url.host ?: ""
    }

    fun getPathname (): String {
      return this.request.url.path ?: ""
    }

    fun getQuery (): String {
      return this.request.url.query ?: ""
    }

    fun getCookieUrl (): String {
      val url = this.request.url
      val app = App.getInstance()
      val bundleIdentifier = app.getUserConfigValue("meta_bundle_identifier")
      val host = url.host ?: ""

      if (host.isBlank() || bundleIdentifier.isBlank()) {
        return url.toString()
      }

      if (!host.equals(bundleIdentifier, ignoreCase = true)) {
        return url.toString()
      }

      return when (url.scheme) {
        "http", "https" -> url.toString()
        "oro" -> url.buildUpon().scheme("https").build().toString()
        else -> url.toString()
      }
    }

    fun getHeaders (): String {
      val builder = StringBuilder()
      for (entry in request.requestHeaders) {
        builder.append(entry.key)
        builder.append(": ")
        builder.append(entry.value)
        builder.append("\n")
      }

      val app = App.getInstance()
      if (getScheme() != "ipc" && app.hasRuntimePermission("cookies")) {
        val hasCookieHeader = request.requestHeaders.keys.any { it.equals("cookie", ignoreCase = true) }
        if (!hasCookieHeader) {
          val cookieValue = CookieManager.getInstance().getCookie(getCookieUrl())
          if (!cookieValue.isNullOrBlank()) {
            builder.append("Cookie: ")
            builder.append(cookieValue)
            builder.append("\n")
          }
        }
      }

      return builder.toString()
    }

    fun getHeader (name: String): String? {
      return request.requestHeaders.get(name)
    }

    fun getUrl (): String {
      val url = this.request.url
      val href = url.toString()

      val app = App.getInstance()
      val bundleIdentifier = app.getUserConfigValue("meta_bundle_identifier")
      val isBundleOrigin = (url.scheme == "https" || url.scheme == "http") && url.host == bundleIdentifier

      if (!isBundleOrigin) {
        return href
      }

      return when {
        href.startsWith("https:") -> "oro:" + href.removePrefix("https:")
        href.startsWith("http:") -> "oro:" + href.removePrefix("http:")
        else -> href
      }
    }

    fun getWebResourceResponse (): WebResourceResponse? {
      return this.response.response
    }

    fun waitForFinishedResponse () {
      this.response.waitForFinish()
    }
  }

  open class Response (val request: Request) {
    var mimeType = "application/octet-stream"
    val response = WebResourceResponse(
      mimeType,
      null,
      null
    )

    val headers = mutableMapOf<String, String>()
    val buffers = mutableListOf<ByteArray>()
    val semaphore = Semaphore(0)

    var pendingWrites = 0
    var finished = false

    fun setStatus (statusCode: Int, statusText: String) {
      val headers = this.headers
      val mimeType = this.mimeType

      this.response.apply {
        setStatusCodeAndReasonPhrase(statusCode, statusText)
        setResponseHeaders(headers)
        setMimeType(mimeType)
      }
    }

    fun setHeader (name: String, value: String) {
      if (name.lowercase() == "set-cookie") {
        runCatching {
          val app = App.getInstance()
          if (app.hasRuntimePermission("cookies")) {
            val cookieManager = CookieManager.getInstance()
            cookieManager.setCookie(this.request.getCookieUrl(), value)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
              cookieManager.flush()
            }
          }
        }
      }

      if (name.lowercase() == "content-type") {
        // WebResourceResponse expects the media type and charset separately.
        val parts = value.split(';')
        val charset = parts.drop(1).firstNotNullOfOrNull { parameter ->
          val pair = parameter.split('=', limit = 2)
          if (pair.size == 2 && pair[0].trim().equals("charset", ignoreCase = true)) {
            pair[1].trim().trim('"').takeIf { it.isNotEmpty() }
          } else {
            null
          }
        }
        this.mimeType = parts[0].trim().lowercase()
        this.response.setMimeType(this.mimeType)
        this.response.setEncoding(charset)
      } else if (name.lowercase() != "content-length") {
        this.headers.remove(name)

        if (this.response.responseHeaders != null) {
          this.response.responseHeaders.remove(name)
        }

        this.headers += mapOf(name to value)
        this.response.responseHeaders = this.headers
      }
    }

    fun write (bytes: ByteArray) {
      this.buffers += bytes
    }

    fun write (string: String) {
      this.write(string.toByteArray())
    }

    fun finish () {
      if (!this.finished) {
        this.response.setData(BufferedResponseInputStream(this.buffers))
        this.finished = true
        this.semaphore.release()
      }
    }

    fun waitForFinish () {
      this.semaphore.acquireUninterruptibly()
      this.semaphore.release()
    }
  }

  fun handleRequest (webResourceRequest: WebResourceRequest): WebResourceResponse? {
    val request = Request(this.bridge, webResourceRequest)

    try {
      if (this.handleRequest(this.bridge.index, request)) {
        request.waitForFinishedResponse()
        return request.getWebResourceResponse()
      }
    } catch (e: Exception) {
      console.debug(e.toString())
    }

    return null
  }

  fun hasHandlerForScheme (scheme: String): Boolean {
    return this.hasHandlerForScheme(this.bridge.index, scheme)
  }

  @Throws(Exception::class)
  external fun handleRequest (index: Int, request: Request): Boolean

  @Throws(Exception::class)
  external fun hasHandlerForScheme (index: Int, scheme: String): Boolean
}
