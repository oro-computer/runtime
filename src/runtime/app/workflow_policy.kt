package oro.runtime.app

import org.json.JSONArray
import org.json.JSONObject
import java.net.URI
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

internal data class WorkflowEvent(val key: String, val presentation: JSONObject)
internal data class WorkflowPlan(val events: List<WorkflowEvent>, val cursor: String?)

/** Failure before notification submission; retryable only for the fixed safe GET. */
internal class WorkflowReadFailure(val code: String, val retryable: Boolean = false) : Exception(code)

/** Five attempts within two minutes; callers reauthorize and reacquire TLS on every attempt. */
internal object WorkflowReadRetry {
  const val VERSION = "bounded-read-backoff-v1"
  const val MAX_ATTEMPTS = 5
  const val BUDGET_MS = 120000L
  const val COOLDOWN_MS = 60000L

  fun <T> execute(timeoutMs: Int, now: () -> Long, pause: (Long) -> Unit,
    pending: (Int, Long) -> Unit, read: (Int) -> T): T {
    val deadline = now() + BUDGET_MS
    for (attempt in 1..MAX_ATTEMPTS) {
      val remaining = deadline - now()
      if (remaining <= 0) throw WorkflowReadFailure("ETIMEDOUT", true)
      pending(attempt, 0)
      try {
        return read(minOf(timeoutMs.toLong(), remaining).toInt())
      } catch (failure: WorkflowReadFailure) {
        if (!failure.retryable || attempt == MAX_ATTEMPTS) throw failure
        val delay = 1000L shl (attempt - 1)
        if (deadline - now() <= delay) throw failure
        pending(attempt, delay)
        pause(delay)
      }
    }
    throw WorkflowReadFailure("ETIMEDOUT")
  }
}

/** Immutable packaged policy; JSON pointers and fixed mappings only, never executable expressions. */
internal class WorkflowPolicy private constructor(
  val id: String,
  val revision: String,
  val url: String,
  val approvedPath: String?,
  val allowApprovedBasePath: Boolean,
  val timeoutMs: Int,
  val maximumResponseBytes: Int,
  val pollIntervalMs: Int?,
  val credentialKey: String?,
  val items: String,
  val eventId: String,
  val checks: List<Pair<String, String>>,
  val cursor: String?,
  val cursorParameter: String?,
  private val notification: String,
  val channels: String
) {
  val kind get() = "workflow.$id"
  companion object {
    fun basePath(value: String): String {
      if (value.isEmpty() || value == "/") return ""
      require(value.length <= 1024 && value.startsWith("/"))
      val path = value.removeSuffix("/")
      require(path.substring(1).split('/').all { segment ->
        segment !in setOf(".", "..") && Regex("[A-Za-z0-9._~-]{1,128}").matches(segment)
      })
      return path
    }
    private fun keys(value: JSONObject, allowed: Set<String>) {
      require(value.keys().asSequence().all { it in allowed })
    }
    private fun text(value: JSONObject, key: String, maximum: Int): String {
      require(value.opt(key) is String)
      return value.getString(key).also { require(it.length in 1..maximum && it.none { c -> c.code < 32 } && Charsets.UTF_8.newEncoder().canEncode(it)) }
    }
    private fun pointer(value: String): String {
      require(value.length in 1..256 && value.startsWith("/"))
      require(!Regex("~(?![01])").containsMatchIn(value))
      return value
    }
    private fun bounded(source: String) {
      var depth = 0
      var quoted = false
      var escaped = false
      var tokens = 0
      for (character in source) {
        if (quoted) {
          if (escaped) escaped = false
          else if (character.code == 92) escaped = true
          else if (character == '"') quoted = false
        } else {
          if (character == '{' || character == '[' || character == ',' || character == ':') { tokens++; require(tokens <= 16384) }
          when(character) {
          '"' -> quoted = true
          '{', '[' -> { depth++; require(depth <= 32) }
          '}', ']' -> { depth--; require(depth >= 0) }
          }
        }
      }
      require(depth == 0 && !quoted)
    }
    private fun number(value: JSONObject, key: String, fallback: Int): Int {
      if (!value.has(key)) return fallback
      val number = value.get(key)
      require(number is Number && number.toDouble() == number.toInt().toDouble())
      return number.toInt()
    }
    fun parse(source: String): List<WorkflowPolicy> {
      require(source.toByteArray(Charsets.UTF_8).size <= 65536)
      bounded(source)
      val root = JSONObject(source)
      keys(root, setOf("schema", "workflows"))
      require(number(root, "schema", 0) == 1)
      val definitions = root.getJSONArray("workflows")
      require(definitions.length() in 1..16)
      val ids = mutableSetOf<String>()
      return (0 until definitions.length()).map { index ->
        val value = definitions.getJSONObject(index)
        keys(value, setOf("id", "request", "response", "notification", "channels", "authentication"))
        val id = text(value, "id", 48)
        require(Regex("[a-z][a-z0-9.-]{0,47}").matches(id) && ids.add(id))
        val request = value.getJSONObject("request")
        keys(request, setOf("url", "authority", "path", "allowApprovedBasePath", "timeoutMs", "maximumResponseBytes", "cursorParameter", "pollIntervalMs"))
        val dynamic = request.optString("authority") == "approved"
        require(request.has("url") != dynamic)
        val path = if (dynamic) text(request, "path", 4096) else null
        if (path != null) require(path.startsWith("/") && !path.startsWith("//"))
        else require(!request.has("authority") && !request.has("path"))
        val allowBasePath = request.opt("allowApprovedBasePath") ?: false
        require(allowBasePath is Boolean && (!allowBasePath || dynamic))
        val url = if (dynamic) "" else text(request, "url", 4096)
        val uri = URI(if (dynamic) "https://approved.invalid" + path else url)
        require(uri.scheme == "https" && uri.host != null && uri.rawUserInfo == null && uri.rawFragment == null &&
          (uri.port == -1 || uri.port in 1..65535))
        val timeout = number(request, "timeoutMs", 10000)
        val maximum = number(request, "maximumResponseBytes", 65536)
        require(timeout in 1..60000 && maximum in 1..1048576)
        val poll = if (request.has("pollIntervalMs")) number(request, "pollIntervalMs", 0) else null
        require(poll == null || poll in 5000..3600000)
        val auth = value.optJSONObject("authentication")
        val credentialKey = auth?.let {
          keys(it, setOf("type", "key"))
          require(it.getString("type") == "bearer")
          text(it, "key", 128).also { key -> require(Regex("[A-Za-z0-9_.:-]{1,128}").matches(key)) }
        }
        val response = value.getJSONObject("response")
        keys(response, setOf("items", "eventId", "checks", "cursor"))
        val items = pointer(response.getString("items"))
        val eventId = pointer(response.getString("eventId"))
        val checks = response.optJSONArray("checks") ?: JSONArray()
        require(checks.length() <= 8)
        val conditions = (0 until checks.length()).map {
          val check = checks.getJSONObject(it)
          keys(check, setOf("pointer", "equalsRef"))
          val ref = check.getString("equalsRef")
          require(ref in setOf("authorityRef", "accountRef", "targetRef"))
          pointer(check.getString("pointer")) to ref
        }
        if (credentialKey != null) require(conditions.any { it.second == "accountRef" })
        val cursor = if (response.has("cursor")) pointer(response.getString("cursor")) else null
        val parameter = if (request.has("cursorParameter")) text(request, "cursorParameter", 64) else null
        require((cursor == null) == (parameter == null))
        if (parameter != null) require(Regex("[A-Za-z][A-Za-z0-9_-]{0,63}").matches(parameter) &&
          uri.rawQuery?.split('&')?.none { it.substringBefore('=') == parameter } != false)
        val notification = value.getJSONObject("notification")
        keys(notification, setOf("title", "body", "channel", "publicVersion", "group", "groupAlertBehavior", "smallIcon"))
        pointer(notification.getString("title"))
        if (notification.has("body")) pointer(notification.getString("body"))
        text(notification, "channel", 128)
        val public = notification.getJSONObject("publicVersion")
        keys(public, setOf("title", "body"))
        text(public, "title", 160)
        if (public.has("body")) require(public.getString("body").length <= 512)
        val channels = value.getJSONArray("channels")
        require(channels.length() in 1..8)
        require((0 until channels.length()).any { channels.getJSONObject(it).optString("id") == notification.getString("channel") })
        val revision = MessageDigest.getInstance("SHA-256").digest(value.toString().toByteArray(Charsets.UTF_8))
          .joinToString("") { "%02x".format(it.toInt() and 255) }
        WorkflowPolicy(id, revision, url, path, allowBasePath, timeout, maximum, poll, credentialKey, items, eventId, conditions, cursor, parameter,
          notification.toString(), channels.toString())
      }
    }
  }
  private fun at(root: Any, pointer: String): Any {
    var value = root
    if (pointer.isEmpty()) return value
    for (encoded in pointer.substring(1).split('/')) {
      val key = encoded.replace("~1", "/").replace("~0", "~")
      value = when(value) {
        is JSONObject -> value.get(key)
        is JSONArray -> {
          require(Regex("0|[1-9][0-9]{0,8}").matches(key))
          value.get(key.toInt())
        }
        else -> throw NativeStateException("EBADMSG")
      }
    }
    return value
  }
  fun plan(body: ByteArray, binding: JSONObject): WorkflowPlan {
    require(body.size <= maximumResponseBytes)
    val text = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
      .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(body)).toString()
    bounded(text)
    val root = JSONObject(text)
    val records = at(root, items) as? JSONArray ?: throw NativeStateException("EBADMSG")
    require(records.length() <= 32)
    val seen = mutableSetOf<String>()
    val events = (0 until records.length()).map { index ->
      val record = records.getJSONObject(index)
      for ((pointer, ref) in checks) {
        val actual = at(record, pointer)
        if (actual !is String || actual != binding.getString(ref)) throw NativeStateException("EACCES")
      }
      val identity = at(record, eventId) as? String ?: throw NativeStateException("EBADMSG")
      require(identity.length in 1..256 && identity.none { it.code < 32 } && Charsets.UTF_8.newEncoder().canEncode(identity))
      val key = MessageDigest.getInstance("SHA-256").digest(identity.toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 255) }
      require(seen.add(key))
      val options = JSONObject(notification)
      val title = at(record, options.getString("title")) as? String ?: throw NativeStateException("EBADMSG")
      require(title.length in 1..160 && Charsets.UTF_8.newEncoder().canEncode(title))
      options.put("title", title)
      if (options.has("body")) {
        val content = at(record, options.getString("body")) as? String ?: throw NativeStateException("EBADMSG")
        require(content.length <= 512 && Charsets.UTF_8.newEncoder().canEncode(content))
        options.put("body", content)
      }
      WorkflowEvent(key, options)
    }
    val nextCursor = cursor?.let { at(root, it) as? String ?: throw NativeStateException("EBADMSG") }
    require(nextCursor == null || (nextCursor.length <= 2048 && Charsets.UTF_8.newEncoder().canEncode(nextCursor) &&
      (if (approvedPath == null) url.length else 512 + approvedPath.length) + 128 + java.net.URLEncoder.encode(nextCursor, "UTF-8").length <= 8192))
    return WorkflowPlan(events, nextCursor)
  }
}
