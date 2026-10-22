package oro.runtime.app

import android.content.Context
import android.os.CancellationSignal
import android.os.SystemClock
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import android.util.Base64
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.URI
import java.net.SocketTimeoutException
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import javax.crypto.KeyGenerator
import javax.crypto.Mac
import javax.crypto.SecretKey
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLException
import javax.net.ssl.X509TrustManager

/** WebView and compiled HTTPS share certificate SHA-256 pins and endpoint precedence. */
object NativeTlsPolicy {
  private const val ALIAS = "oro.runtime.tls.policy.hmac.v1"
  private const val LIMIT = 32768
  private val active = mutableSetOf<HttpsURLConnection>()

  private fun file (context: Context) = AtomicFile(File(context.noBackupFilesDir, "oro-tls-policy-v1.json"))

  private fun key (create: Boolean): SecretKey {
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    val existing = store.getKey(ALIAS, null)
    if (existing is SecretKey) return existing
    check(create)
    return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_HMAC_SHA256, "AndroidKeyStore").apply {
      init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
        .setDigests(KeyProperties.DIGEST_SHA256).build())
    }.generateKey()
  }

  private fun signature (value: String, create: Boolean): ByteArray = Mac.getInstance("HmacSHA256").run {
    init(key(create))
    doFinal(("oro.runtime.tls.policy.v1\n" + value).toByteArray(Charsets.UTF_8))
  }

  /** Reads the authenticated, backup-excluded policy; corruption or a lost key fails closed. */
  @JvmStatic @Synchronized
  fun value (context: Context): String {
    val record = file(context)
    if (!record.baseFile.exists() && !File(record.baseFile.path + ".bak").exists()) {
      val app = App.getInstance()
      return app.getUserConfigValue("webview_tls_pins").ifBlank { app.getUserConfigValue("webview.tls_pins") }
    }
    try {
      val bytes = record.openRead().use { stream ->
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(4096)
        while (true) {
          val count = stream.read(buffer)
          if (count < 0) break
          check(output.size() + count <= LIMIT * 3)
          output.write(buffer, 0, count)
        }
        output.toByteArray()
      }
      val envelope = JSONObject(String(bytes, Charsets.UTF_8))
      check(envelope.getInt("version") == 1)
      val value = envelope.getString("value")
      check(value.toByteArray(Charsets.UTF_8).size <= LIMIT)
      check(MessageDigest.isEqual(signature(value, false), Base64.decode(envelope.getString("mac"), Base64.NO_WRAP)))
      return value
    } catch (_: Exception) { throw NativeHttpsFailure("ETRUST") }
  }

  /** Replaces approved trust. Call only after the app's server-selection/approval decision. */
  @JvmStatic @Synchronized
  fun replace (context: Context, value: String) {
    require(value.toByteArray(Charsets.UTF_8).size <= LIMIT)
    val pins = parseTlsPins(value)
    require(pins.size <= 256)
    for (line in value.lineSequence()) {
      val parts = line.substringBefore('#').substringBefore(';').trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
      if (parts.isEmpty()) continue
      require(normaliseTlsPinEndpoint(parts.first()).isNotBlank() && normaliseTlsPinToken(parts.first()) == null)
      require(parts.size <= 33 && parts.drop(1).all { normaliseTlsPinToken(it) != null })
    }
    val record = file(context)
    val envelope = JSONObject().put("version", 1).put("value", value)
      .put("mac", Base64.encodeToString(signature(value, true), Base64.NO_WRAP))
    val bytes = envelope.toString().toByteArray(Charsets.UTF_8)
    require(bytes.size <= LIMIT * 3)
    val stream = record.startWrite()
    try {
      stream.write(bytes)
      stream.fd.sync()
      record.finishWrite(stream)
    } catch (error: Exception) { record.failWrite(stream); throw NativeHttpsFailure("ETRUST") }
    // Close every request established under the previous trust decision.
    for (connection in active.toList()) connection.disconnect()
  }

  /** Explicit deny entry for one HTTPS authority, including otherwise public-CA certificates. */
  @JvmStatic @Synchronized
  fun revoke (context: Context, authority: String) {
    val endpoint = NativeHttps.endpoint(URI("https://" + authority))
    require(!authority.contains('/') && !authority.contains('?') && !authority.contains('#'))
    val pins = parseTlsPins(value(context)).toMutableMap()
    pins[endpoint] = emptyList()
    replace(context, pins.entries.joinToString("\n") { (host, values) ->
      host + values.joinToString("") { " sha256/" + it }
    })
  }

  internal fun pins (context: Context) = parseTlsPins(value(context))

  @Synchronized internal fun begin (context: Context, connection: HttpsURLConnection): String {
    if (active.size >= 8) throw NativeHttpsFailure("ENOSPC")
    val value = value(context)
    active.add(connection)
    return value
  }

  @Synchronized internal fun current (context: Context, expected: String): Boolean = value(context) == expected
  @Synchronized internal fun end (connection: HttpsURLConnection) { active.remove(connection) }

  internal fun normaliseTlsPinHost (rawHost: String?): String {
    if (rawHost == null) {
      return ""
    }

    var host = rawHost.trim().lowercase(java.util.Locale.ROOT)
    if (host.isEmpty()) {
      return ""
    }

    val schemeIndex = host.indexOf("://")
    host = when {
      schemeIndex >= 0 -> host.substring(schemeIndex + 3)
      host.startsWith("//") -> host.substring(2)
      else -> host
    }

    val cut = host.indexOfAny(charArrayOf('/', '?', '#'))
    if (cut >= 0) {
      host = host.substring(0, cut)
    }

    val at = host.lastIndexOf('@')
    if (at >= 0) {
      host = host.substring(at + 1)
    }

    host = host.trim()
    if (host.isEmpty()) {
      return ""
    }

    while (host.endsWith(".")) {
      host = host.dropLast(1)
    }

    if (host.isEmpty()) {
      return ""
    }

    if (host.startsWith("[")) {
      val end = host.indexOf(']')
      if (end <= 1) {
        return ""
      }

      val inner = host.substring(1, end)
      val suffix = host.substring(end + 1)

      if (suffix.isEmpty()) {
        return inner
      }

      if (!suffix.startsWith(":")) {
        return ""
      }

      val port = suffix.substring(1)
      if (port.isEmpty() || !port.all { it in '0'..'9' }) {
        return ""
      }

      return inner
    }

    val colon = host.lastIndexOf(':')
    if (colon >= 0) {
      val prefix = host.substring(0, colon)
      val suffix = if (colon + 1 < host.length) host.substring(colon + 1) else ""
      if (!prefix.contains(':')) {
        if (prefix.isEmpty() || suffix.isEmpty()) {
          return ""
        }

        if (!suffix.all { it in '0'..'9' }) {
          return ""
        }

        host = prefix
        while (host.endsWith(".")) {
          host = host.dropLast(1)
        }

        if (host.isEmpty()) {
          return ""
        }
      }
    }

    return host
  }

  internal fun normaliseTlsPinEndpoint (rawHost: String?): String {
    if (rawHost == null) {
      return ""
    }

    var host = rawHost.trim().lowercase(java.util.Locale.ROOT)
    if (host.isEmpty()) {
      return ""
    }

    val schemeIndex = host.indexOf("://")
    host = when {
      schemeIndex >= 0 -> host.substring(schemeIndex + 3)
      host.startsWith("//") -> host.substring(2)
      else -> host
    }

    val cut = host.indexOfAny(charArrayOf('/', '?', '#'))
    if (cut >= 0) {
      host = host.substring(0, cut)
    }

    val at = host.lastIndexOf('@')
    if (at >= 0) {
      host = host.substring(at + 1)
    }

    host = host.trim()
    if (host.isEmpty()) {
      return ""
    }

    while (host.endsWith(".")) {
      host = host.dropLast(1)
    }

    if (host.isEmpty()) {
      return ""
    }

    if (host.startsWith("[")) {
      val end = host.indexOf(']')
      if (end <= 1) {
        return ""
      }

      val inner = host.substring(1, end)
      val suffix = host.substring(end + 1)

      if (suffix.isEmpty()) {
        return inner
      }

      if (!suffix.startsWith(":")) {
        return ""
      }

      val port = suffix.substring(1)
      if (port.isEmpty() || !port.all { it in '0'..'9' }) {
        return ""
      }

      return "[$inner]:$port"
    }

    val colon = host.lastIndexOf(':')
    if (colon >= 0) {
      val prefix = host.substring(0, colon)
      val suffix = if (colon + 1 < host.length) host.substring(colon + 1) else ""
      if (!prefix.contains(':')) {
        if (prefix.isEmpty() || suffix.isEmpty()) {
          return ""
        }

        if (!suffix.all { it in '0'..'9' }) {
          return ""
        }

        var base = prefix
        while (base.endsWith(".")) {
          base = base.dropLast(1)
        }

        if (base.isEmpty()) {
          return ""
        }

        return "$base:$suffix"
      }
    }

    return host
  }

  private fun normaliseTlsPinToken (rawToken: String?): String? {
    if (rawToken == null) {
      return null
    }

    var token = rawToken.trim()
    if (token.isEmpty()) {
      return null
    }

    if (token.startsWith("sha256/", ignoreCase = true)) {
      token = token.substring("sha256/".length)
    }

    token = token.replace('-', '+').replace('_', '/')

    val remainder = token.length % 4
    if (remainder != 0) {
      token += "=".repeat(4 - remainder)
    }

    val decoded = try {
      Base64.decode(token, Base64.DEFAULT)
    } catch (_: IllegalArgumentException) {
      null
    } ?: return null

    if (decoded.size != 32) {
      return null
    }

    return Base64.encodeToString(decoded, Base64.NO_WRAP)
  }

  internal fun parseTlsPins (raw: String): Map<String, List<String>> {
    val result = mutableMapOf<String, MutableList<String>>()

    raw.lineSequence().forEach { original ->
      var line = original.trim()
      if (line.isEmpty()) return@forEach

      val hashIndex = line.indexOf('#')
      val semiIndex = line.indexOf(';')
      val commentIndex = when {
        hashIndex < 0 -> semiIndex
        semiIndex < 0 -> hashIndex
        else -> minOf(hashIndex, semiIndex)
      }

      if (commentIndex >= 0) {
        line = line.substring(0, commentIndex).trim()
        if (line.isEmpty()) return@forEach
      }

      val parts = line.split(Regex("\\s+")).filter { it.isNotEmpty() }
      if (parts.isEmpty()) return@forEach

      val firstToken = parts[0].trim()
      // Guard against accidentally providing pin tokens where a host is expected.
      if (
        firstToken.startsWith("sha256/", ignoreCase = true) ||
        normaliseTlsPinToken(firstToken) != null
      ) {
        return@forEach
      }

      val host = normaliseTlsPinEndpoint(parts[0])
      if (host.isEmpty()) return@forEach

      val pins = result.getOrPut(host) { mutableListOf() }

      // Fail-closed semantics: a configured host with no valid pins should
      // reject all certificates when evaluated.
      if (parts.size < 2) return@forEach

      parts.drop(1).forEach { rawPin ->
        val pin = normaliseTlsPinToken(rawPin) ?: return@forEach
        if (!pins.contains(pin)) {
          pins.add(pin)
        }
      }
    }

    return result
  }

  internal fun isCertificateAllowedForHost (
    pins: Map<String, List<String>>,
    host: String?,
    certSha256: ByteArray
  ): Boolean {
    if (host == null || host.isEmpty()) {
      return true
    }

    val endpointKey = normaliseTlsPinEndpoint(host)
    if (endpointKey.isEmpty()) {
      return true
    }

    val hostKey = normaliseTlsPinHost(host)
    if (hostKey.isEmpty()) {
      return true
    }

    val hostPins = pins[endpointKey] ?: pins[hostKey] ?: return true

    if (certSha256.isEmpty()) {
      return false
    }

    if (hostPins.isEmpty()) {
      return false
    }

    val digestB64 = Base64.encodeToString(certSha256, Base64.NO_WRAP)
    return hostPins.any { it == digestB64 }
  }

  internal fun hasTlsPinsForHost (
    pins: Map<String, List<String>>,
    host: String?
  ): Boolean {
    if (host == null || host.isEmpty()) {
      return false
    }

    val endpointKey = normaliseTlsPinEndpoint(host)
    if (endpointKey.isEmpty()) {
      return false
    }

    val hostKey = normaliseTlsPinHost(host)
    if (hostKey.isEmpty()) {
      return false
    }

    return pins.containsKey(endpointKey) || pins.containsKey(hostKey)
  }

}

/** Only a fixed error code is retained; provider exceptions, URLs and credentials are excluded. */
class NativeHttpsFailure (val code: String) : Exception(code)

/** A bounded native response; headers/cookies are not persisted or exposed by this transport. */
data class NativeHttpsResponse (val status: Int, val body: ByteArray)

/** HTTPS for compiled executors. Approval and application authorization remain the caller's responsibility. */
object NativeHttps {
  private val deadlines = java.util.concurrent.ScheduledThreadPoolExecutor(1,
    java.util.concurrent.ThreadFactory { operation ->
      Thread(operation, "oro-native-https-deadline").apply { isDaemon = true }
    }).apply { removeOnCancelPolicy = true }

  internal fun endpoint (uri: URI): String {
    require(uri.scheme.equals("https", true) && uri.rawUserInfo == null && uri.rawFragment == null)
    val host = uri.host?.lowercase(java.util.Locale.ROOT) ?: throw IllegalArgumentException()
    require(host.isNotBlank() && (uri.port == -1 || uri.port in 1..65535))
    val normalized = NativeTlsPolicy.normaliseTlsPinHost(host)
    require(normalized.isNotBlank())
    return (if (normalized.contains(':')) "[$normalized]" else normalized) + ":" + (if (uri.port == -1) 443 else uri.port)
  }

  private val workers = java.util.concurrent.ThreadPoolExecutor(0, 8, 30L, TimeUnit.SECONDS,
    java.util.concurrent.SynchronousQueue(), java.util.concurrent.ThreadFactory { operation ->
      Thread(operation, "oro-native-https").apply { isDaemon = true }
    })

  /**
   * Runs on a caller-owned worker thread, with an absolute 1..60,000 ms deadline,
   * at most 1 MiB each direction and at most eight concurrent connections.
   * Redirects are rejected. Pinned leaf certificates identify the approved authority,
   * including a self-signed certificate. Unpinned authorities retain Android CA/hostname validation.
   * The cancellation signal belongs to this call until it returns.
   */
  @JvmStatic
  fun request (
    context: Context,
    url: String,
    method: String = "GET",
    headers: Map<String, String> = emptyMap(),
    body: ByteArray? = null,
    cancellation: CancellationSignal = CancellationSignal(),
    timeoutMs: Int = 30000,
    maximumResponseBytes: Int = 1048576
  ): NativeHttpsResponse {
    if (timeoutMs !in 1..60000) throw NativeHttpsFailure("EINVAL")
    if (cancellation.isCanceled) throw NativeHttpsFailure("ECANCELLED")
    val started = SystemClock.elapsedRealtime()
    val future = try {
      workers.submit<NativeHttpsResponse> {
        execute(context.applicationContext, url, method, headers, body, cancellation, timeoutMs, maximumResponseBytes, started)
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) { throw NativeHttpsFailure("ENOSPC") }
    try {
      while (true) {
        if (cancellation.isCanceled) throw NativeHttpsFailure("ECANCELLED")
        val remaining = timeoutMs - (SystemClock.elapsedRealtime() - started)
        if (remaining <= 0) throw NativeHttpsFailure("ETIMEDOUT")
        try {
          return future.get(minOf(remaining, 50L), TimeUnit.MILLISECONDS)
        } catch (_: java.util.concurrent.TimeoutException) { /* Recheck cancellation and the absolute deadline. */ }
      }
    } catch (error: Exception) {
      future.cancel(true)
      val code = when (error) {
        is NativeHttpsFailure -> error.code
        is java.util.concurrent.ExecutionException -> (error.cause as? NativeHttpsFailure)?.code ?: "EIO"
        is InterruptedException -> { Thread.currentThread().interrupt(); "ECANCELLED" }
        else -> "EIO"
      }
      throw NativeHttpsFailure(code)
    }
  }

  private fun execute (
    context: Context,
    url: String,
    method: String = "GET",
    headers: Map<String, String> = emptyMap(),
    body: ByteArray? = null,
    cancellation: CancellationSignal = CancellationSignal(),
    timeoutMs: Int = 30000,
    maximumResponseBytes: Int,
    started: Long
  ): NativeHttpsResponse {
    var connection: HttpsURLConnection? = null
    var timer: java.util.concurrent.ScheduledFuture<*>? = null
    var registered = false
    var approved: String? = null
    val expired = AtomicBoolean(false)
    try {
      require(timeoutMs in 1..60000 && maximumResponseBytes in 0..1048576 && url.length <= 8192)
      require(method in setOf("GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS"))
      require(body == null || (body.size <= 1048576 && method in setOf("POST", "PUT", "DELETE", "OPTIONS")))
      require(headers.size <= 64 && headers.entries.sumOf { it.key.length + it.value.length } <= 32768)
      for ((name, value) in headers) {
        require(Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]{1,128}").matches(name) && value.length <= 8192)
        require(value.none { it < ' ' || it == '\u007f' })
        require(name.lowercase(java.util.Locale.ROOT) !in setOf("host", "connection", "content-length", "transfer-encoding",
          "upgrade", "proxy-authorization", "accept-encoding"))
      }
      cancellation.throwIfCanceled()
      val uri = URI(url)
      val authority = endpoint(uri)
      val request = uri.toURL().openConnection() as HttpsURLConnection
      connection = request
      request.instanceFollowRedirects = false
      request.useCaches = false
      request.connectTimeout = timeoutMs
      request.readTimeout = timeoutMs
      request.requestMethod = method
      request.setRequestProperty("Accept-Encoding", "identity")
      for ((name, value) in headers) request.setRequestProperty(name, value)
      val policy = NativeTlsPolicy.begin(context, request)
      registered = true
      approved = policy
      val pins = NativeTlsPolicy.parseTlsPins(policy)
      if (NativeTlsPolicy.hasTlsPinsForHost(pins, authority)) {
        val trust = object : X509TrustManager {
          override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
          override fun checkClientTrusted(chain: Array<out X509Certificate>?, type: String?) { throw CertificateException() }
          override fun checkServerTrusted(chain: Array<out X509Certificate>?, type: String?) {
            val leaf = chain?.firstOrNull() ?: throw CertificateException()
            val digest = MessageDigest.getInstance("SHA-256").digest(leaf.encoded)
            if (!NativeTlsPolicy.isCertificateAllowedForHost(pins, authority, digest)) throw CertificateException()
          }
        }
        request.sslSocketFactory = SSLContext.getInstance("TLS").apply { init(null, arrayOf(trust), null) }.socketFactory
        request.hostnameVerifier = javax.net.ssl.HostnameVerifier { hostname, session ->
          NativeTlsPolicy.normaliseTlsPinHost(hostname) == NativeTlsPolicy.normaliseTlsPinHost(uri.host) && try {
            val leaf = session.peerCertificates.firstOrNull() as? X509Certificate
            leaf != null && NativeTlsPolicy.isCertificateAllowedForHost(pins, authority,
              MessageDigest.getInstance("SHA-256").digest(leaf.encoded))
          } catch (_: Exception) { false }
        }
      }
      cancellation.setOnCancelListener { request.disconnect() }
      timer = deadlines.schedule({ expired.set(true); request.disconnect() },
        maxOf(1L, timeoutMs - (SystemClock.elapsedRealtime() - started)), TimeUnit.MILLISECONDS)
      fun checkCurrent () {
        cancellation.throwIfCanceled()
        if (expired.get() || SystemClock.elapsedRealtime() - started >= timeoutMs) throw NativeHttpsFailure("ETIMEDOUT")
        if (!NativeTlsPolicy.current(context, policy)) throw NativeHttpsFailure("ETRUST")
      }
      checkCurrent()
      if (body != null) {
        request.doOutput = true
        request.setFixedLengthStreamingMode(body.size)
        request.outputStream.use { stream ->
          var offset = 0
          while (offset < body.size) {
            checkCurrent()
            val count = minOf(8192, body.size - offset)
            stream.write(body, offset, count)
            offset += count
          }
        }
      }
      val status = request.responseCode
      checkCurrent()
      if (status in 300..399) throw NativeHttpsFailure("EREDIRECT")
      val length = request.contentLengthLong
      if (method != "HEAD" && length > maximumResponseBytes) throw NativeHttpsFailure("E2BIG")
      val output = ByteArrayOutputStream()
      val stream = if (status >= 400) request.errorStream else request.inputStream
      stream?.use {
        val buffer = ByteArray(8192)
        while (true) {
          checkCurrent()
          val count = it.read(buffer, 0, minOf(buffer.size, maximumResponseBytes - output.size() + 1))
          if (count < 0) break
          if (output.size() + count > maximumResponseBytes) throw NativeHttpsFailure("E2BIG")
          output.write(buffer, 0, count)
        }
      }
      checkCurrent()
      return NativeHttpsResponse(status, output.toByteArray())
    } catch (error: Exception) {
      val trustChanged = try { approved != null && !NativeTlsPolicy.current(context, approved) }
        catch (_: Exception) { true }
      val code = when {
        cancellation.isCanceled -> "ECANCELLED"
        expired.get() || SystemClock.elapsedRealtime() - started >= timeoutMs || error is SocketTimeoutException -> "ETIMEDOUT"
        trustChanged -> "ETRUST"
        error is NativeHttpsFailure -> error.code
        error is SSLException || error is CertificateException -> "ETRUST"
        error is IllegalArgumentException || error is java.net.URISyntaxException -> "EINVAL"
        else -> "EIO"
      }
      throw NativeHttpsFailure(code)
    } finally {
      timer?.cancel(false)
      cancellation.setOnCancelListener(null)
      connection?.let { if (registered) NativeTlsPolicy.end(it); it.disconnect() }
    }
  }
}
