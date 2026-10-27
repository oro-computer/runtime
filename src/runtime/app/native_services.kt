package oro.runtime.app

import android.content.Context
import android.os.CancellationSignal
import android.os.OperationCanceledException
import android.util.Base64
import androidx.annotation.Keep
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** SDK-only service dispatch. No IPC route, method lookup, Activity or product DTO is accepted here. */
@Keep
internal object NativeServices {
  private fun encode(value: ByteArray) = Base64.encodeToString(value, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
  private fun decode(value: String): ByteArray {
    require(value.length <= 1398102 && Regex("[A-Za-z0-9_-]*").matches(value))
    return Base64.decode(value, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING).also { require(encode(it) == value) }
  }
  private fun revision(value: JSONObject, key: String = "revision"): Long {
    val text = value.getString(key)
    require(Regex("0|[1-9][0-9]{0,18}").matches(text))
    return text.toLong()
  }
  private fun changes(value: JSONObject): Map<String, ByteArray?> {
    require(value.length() <= 128)
    return value.keys().asSequence().associateWith { key -> if (value.isNull(key)) null else decode(value.getString(key)) }
  }
  private fun snapshot(value: NativeRecordSnapshot): JSONObject = try {
    JSONObject().put("revision", value.revision.toString()).put("commitId", value.commitId ?: "")
      .put("records", JSONObject(value.records.mapValues { encode(it.value) }))
  } finally { value.records.values.forEach { it.fill(0) } }
  private fun <T> withChanges(input: JSONObject, action: (Map<String, ByteArray?>) -> T): T {
    val values = changes(input)
    try { return action(values) } finally { values.values.forEach { it?.fill(0) } }
  }
  private fun state(value: NativeCoordinationState?): Any = value?.let {
    JSONObject().put("ticket", JSONObject().put("scope", it.ticket.scope).put("slot", it.ticket.slot)
      .put("generation", it.ticket.generation).put("revision", it.ticket.revision.toString()))
      .put("operationRef", it.operationRef).put("phase", it.phase).put("ownerProcessMatches", it.ownerProcessMatches)
      .put("receipt", encode(it.receipt ?: byteArrayOf()))
  } ?: JSONObject.NULL
  private fun records(context: Context, action: String, options: JSONObject): Any {
    val identity = options.getJSONObject("identity")
    val store = NativeRecords.open(context, options.getString("name"), NativeRecordIdentity(identity.getString("authority"),
      identity.getString("account"), identity.getString("identity")))
    val input = options.getJSONObject("input")
    val coordinator = NativeCoordinator(store)
    val ticket = input.optJSONObject("ticket")?.let {
      NativeCoordinationTicket(it.getString("scope"), it.getString("slot"), it.getString("generation"), revision(it))
    }
    return when(action) {
      "read" -> snapshot(store.read())
      "status" -> store.status().let { JSONObject().put("revision", it.revision.toString()).put("commitId", it.commitId ?: "")
        .put("keyPresent", it.keyPresent).put("requiresRecovery", it.requiresRecovery) }
      "apply" -> withChanges(input.getJSONObject("changes")) { updates ->
        snapshot(store.apply(revision(input), updates, input.getString("commitId").ifEmpty { UUID.randomUUID().toString() }))
      }
      "migrate" -> {
        withChanges(input.getJSONObject("changes")) { updates ->
          require(updates.values.all { it != null })
          snapshot(store.migrate(input.getString("migrationId"), input.getString("sourceSha256"), revision(input), updates.mapValues { it.value!! }))
        }
      }
      "get" -> state(coordinator.get(input.getString("slot")))
      "prepare" -> state(coordinator.prepare(input.getString("slot"), input.getString("operationRef"), revision(input)))
      "begin" -> state(coordinator.begin(requireNotNull(ticket)))
      "unknown" -> state(coordinator.unknown(requireNotNull(ticket)))
      "recover" -> state(coordinator.recover(requireNotNull(ticket)))
      "abort" -> state(coordinator.abort(requireNotNull(ticket)))
      "commit" -> withChanges(input.getJSONObject("changes")) { updates ->
        val receipt = decode(input.getString("receipt"))
        try { state(coordinator.commit(requireNotNull(ticket), updates, receipt)) } finally { receipt.fill(0) }
      }
      "resolve" -> withChanges(input.getJSONObject("changes")) { updates ->
        val receipt = decode(input.getString("receipt"))
        try { state(coordinator.resolve(requireNotNull(ticket), input.getBoolean("applied"), updates, receipt)) } finally { receipt.fill(0) }
      }
      "release" -> snapshot(coordinator.release(requireNotNull(ticket)))
      else -> throw NativeStateException("ENOSYS")
    }
  }
  fun call(context: Context, source: String, cancellation: CancellationSignal?): String = try {
    require(source.toByteArray(Charsets.UTF_8).size <= 2097152)
    cancellation?.throwIfCanceled()
    val request = JSONObject(source)
    val options = request.getJSONObject("options")
    val result: Any = when(request.getString("service")) {
      "records.v1" -> records(context, request.getString("action"), options)
      "https" -> {
        require(request.getString("action") == "request")
        val headerValues = options.getJSONObject("headers")
        val headers = headerValues.keys().asSequence().associateWith { headerValues.getString(it) }
        val body = if (options.isNull("body")) null else decode(options.getString("body"))
        try {
          val response = NativeHttps.requestOwned(context, options.getString("url"), options.getString("method"), headers, body,
            requireNotNull(cancellation), options.getInt("timeoutMs"), options.getInt("maximumResponseBytes"))
          try { JSONObject().put("status", response.status).put("body", encode(response.body)) } finally { response.body.fill(0) }
        } finally { body?.fill(0) }
      }
      "notifications" -> when(request.getString("action")) {
        "submit" -> NativeNotifications.submitNative(context, options.getJSONObject("presentation"), options.getString("targetRef"))
        "configure" -> NativeNotifications.configureChannels(context, options.getJSONArray("channels"))
        "status" -> NativeNotifications.status(context, options.getJSONArray("channels"))
        "active" -> NativeNotifications.active(context)
        "responses" -> JSONArray(NotificationReceipts.listNative(context))
        "acknowledge" -> NotificationReceipts.acknowledge(context, options.getString("receiptId"))
        "cancel" -> { if (!NativeNotifications.cancel(context, options.getString("id"), options.getString("tag")))
          throw PlatformFailure("EIO"); JSONObject.NULL }
        else -> throw PlatformFailure("ENOSYS")
      }
      else -> throw NativeStateException("ENOSYS")
    }
    JSONObject().put("data", result).toString()
  } catch (error: NativeStateException) { JSONObject().put("error", error.code).toString()
  } catch (error: NativeHttpsFailure) { JSONObject().put("error", if (error.code == "ECANCELLED") "ABORT_ERR" else error.code).toString()
  } catch (error: PlatformFailure) { JSONObject().put("error", error.code).toString()
  } catch (_: OperationCanceledException) { JSONObject().put("error", "ABORT_ERR").toString()
  } catch (_: IllegalArgumentException) { JSONObject().put("error", "EINVAL").toString()
  } catch (_: org.json.JSONException) { JSONObject().put("error", "EINVAL").toString()
  } catch (_: Exception) { JSONObject().put("error", "EIO").toString() }
}
