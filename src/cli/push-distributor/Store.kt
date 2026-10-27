package dev.oro.push

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/** Persist the receive cursor atomically with the unacknowledged ciphertext. */
object Store {
  fun preferences (context: Context) = context.getSharedPreferences("oro.push.development.v1", Context.MODE_PRIVATE)
  fun empty () = JSONObject().put("registrations", JSONArray()).put("pending", JSONArray())
    .put("recent", JSONArray()).put("cursor", "")

  @Synchronized
  fun read (context: Context): JSONObject = preferences(context).getString("state", null)?.let { JSONObject(it) } ?: empty()

  @Synchronized
  fun save (context: Context, state: JSONObject) {
    val encoded = state.toString()
    check(encoded.length <= 2 * 1024 * 1024 && preferences(context).edit().putString("state", encoded).commit()) {
      "Cannot persist distributor state"
    }
  }

  fun records (state: JSONObject, name: String): List<JSONObject> = state.getJSONArray(name).let { array ->
    (0 until array.length()).map { array.getJSONObject(it) }
  }
  fun registration (state: JSONObject, token: String) = records(state, "registrations").find { it.getString("token") == token }

  fun remove (state: JSONObject, token: String) {
    for (name in listOf("registrations", "pending")) {
      val records = state.getJSONArray(name)
      for (index in records.length() - 1 downTo 0) {
        if (records.getJSONObject(index).optString("token") == token) records.remove(index)
      }
    }
  }

  @Synchronized
  fun acknowledge (context: Context, token: String, id: String) {
    val state = read(context)
    val registration = registration(state, token) ?: return
    fun touch () { registration.put("lastAcknowledgedAt", System.currentTimeMillis()).put("expiryPingAt", 0) }
    if (id == registration.optString("endpointId")) {
      touch()
      save(context, state)
      return
    }
    val pending = state.getJSONArray("pending")
    for (index in pending.length() - 1 downTo 0) {
      val event = pending.getJSONObject(index)
      if (event.getString("token") == token && event.getString("id") == id) {
        touch()
        pending.remove(index)
        val recent = state.getJSONArray("recent").put(event.getString("topic") + ":" + id)
        while (recent.length() > 512) recent.remove(0)
        save(context, state)
        return
      }
    }
  }

  /** Ping an inactive connector before retiring its unacknowledged registration. */
  @Synchronized
  fun expire (context: Context, now: Long = System.currentTimeMillis()): List<Pair<String, JSONObject>> {
    val state = read(context)
    val events = mutableListOf<Pair<String, JSONObject>>()
    for (record in records(state, "registrations")) {
      if (now - record.optLong("lastAcknowledgedAt", now) < 30L * 24 * 60 * 60 * 1000) continue
      val ping = record.optLong("expiryPingAt")
      if (ping == 0L) {
        record.put("expiryPingAt", now).put("endpointId", java.util.UUID.randomUUID().toString())
        events.add("NEW_ENDPOINT" to record)
      } else if (now - ping >= 30000) {
        remove(state, record.getString("token"))
        events.add("UNREGISTERED" to record)
      }
    }
    if (events.isNotEmpty()) save(context, state)
    return events
  }

  @Synchronized
  fun receive (context: Context, message: JSONObject) {
    val state = read(context)
    val topic = message.getString("topic")
    val id = message.getString("id")
    require(Protocol.text(id, 100)) { "Invalid message identity" }
    val registration = records(state, "registrations").find { it.getString("topic") == topic } ?: return
    val payload = try { Protocol.payload(message.getString("message"), message.optString("encoding")) }
    catch (_: IllegalArgumentException) {
      // An invalid wire payload cannot block later valid ciphertext on other topics.
      state.put("cursor", id)
      save(context, state)
      return
    }
    val pending = state.getJSONArray("pending")
    val recent = state.getJSONArray("recent")
    val known = records(state, "pending").any { it.getString("topic") == topic && it.getString("id") == id } ||
      (0 until recent.length()).any { recent.getString(it) == "$topic:$id" }
    if (!known) {
      check(pending.length() < Protocol.PENDING) { "Inbox full: waiting for app acknowledgements" }
      pending.put(JSONObject().put("id", id).put("topic", topic).put("token", registration.getString("token"))
        .put("payload", java.util.Base64.getEncoder().encodeToString(payload)))
    }
    state.put("cursor", id)
    save(context, state)
  }
}
