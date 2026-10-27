package oro.runtime.app

import org.json.JSONObject
import java.util.UUID

/** The exact store, generation and revision authorize one conditional transition. */
data class NativeCoordinationTicket (val scope: String, val slot: String, val generation: String, val revision: Long)

data class NativeCoordinationState (
  val ticket: NativeCoordinationTicket,
  val operationRef: String,
  val phase: String,
  val ownerProcessMatches: Boolean,
  val receipt: ByteArray?
)

/** Atomic local records/outcomes. External protocols and fresh authorization remain native app code. */
class NativeCoordinator (private val store: NativeRecordStore) {
  companion object {
    private val process = UUID.randomUUID().toString()
    private val terminal = setOf("committed", "aborted")
  }

  private fun identifier (value: String) {
    require(Regex("[a-zA-Z0-9_.:-]{1,128}").matches(value))
  }
  private fun journal (values: Map<String, ByteArray>): JSONObject = try {
    JSONObject(values["@coordination"]?.toString(Charsets.UTF_8) ?: "{}")
  } catch (_: Exception) { throw NativeStateException("EBADMSG") }
  private fun save (values: MutableMap<String, ByteArray>, journal: JSONObject) {
    values.put("@coordination", journal.toString().toByteArray(Charsets.UTF_8))?.fill(0)
  }
  private fun state (slot: String, revision: Long, record: JSONObject) = NativeCoordinationState(
    NativeCoordinationTicket(store.scope, slot, record.getString("generation"), revision),
    record.getString("operationRef"), record.getString("phase"), record.optString("owner") == process,
    record.optString("receipt").takeIf { it.isNotEmpty() }?.let { android.util.Base64.decode(it, android.util.Base64.NO_WRAP) })

  /** No transition or replay occurs on inspection, including after process loss. */
  fun get (slot: String): NativeCoordinationState? {
    identifier(slot)
    val (revision, values) = store.inspectInternal()
    try { return journal(values).optJSONObject(slot)?.let { state(slot, revision, it) }
    } finally { values.values.forEach { it.fill(0) } }
  }

  /** A retained outcome must be explicitly released before its slot can be reused. */
  fun prepare (slot: String, operationRef: String, expectedRevision: Long): NativeCoordinationState {
    identifier(slot); identifier(operationRef)
    val generation = UUID.randomUUID().toString()
    val record = JSONObject().put("generation", generation).put("operationRef", operationRef)
      .put("phase", "prepared").put("owner", process)
    val result = store.mutate(expectedRevision) { values ->
      val journal = journal(values)
      if (journal.has(slot)) throw NativeStateException("EEXIST")
      if (journal.length() >= 16) throw NativeStateException("ENOSPC")
      journal.put(slot, record); save(values, journal)
    }
    return state(slot, result.revision, record)
  }

  /** Persist before invoking an external operation. A lost reply is never permission to replay. */
  fun begin (ticket: NativeCoordinationTicket): NativeCoordinationState = transition(ticket, setOf("prepared"), "executing")

  /** Record uncertainty, including cancellation after begin; no remote rollback is asserted. */
  fun unknown (ticket: NativeCoordinationTicket): NativeCoordinationState = transition(ticket, setOf("executing", "unknown"), "unknown")

  /** Only a prepared intent can be aborted without external reconciliation. */
  fun abort (ticket: NativeCoordinationTicket): NativeCoordinationState = transition(ticket, setOf("prepared"), "aborted")

  /** Atomically install native records and retain the confirmed external receipt/outcome. */
  fun commit (ticket: NativeCoordinationTicket, updates: Map<String, ByteArray?>,
    receipt: ByteArray = ByteArray(0)): NativeCoordinationState =
    transition(ticket, setOf("executing"), "committed", updates, receipt)

  /** Explicit application reconciliation; false asserts the external change did not apply. */
  fun resolve (ticket: NativeCoordinationTicket, applied: Boolean,
    updates: Map<String, ByteArray?> = emptyMap(), receipt: ByteArray = ByteArray(0)): NativeCoordinationState {
    require(applied || updates.isEmpty())
    return transition(ticket, setOf("unknown"), if (applied) "committed" else "aborted", updates, receipt)
  }

  /** Fence a prior process/abandoned executor, then reconcile externally. No liveness inference or replay. */
  fun recover (ticket: NativeCoordinationTicket): NativeCoordinationState = unknown(ticket)

  fun release (ticket: NativeCoordinationTicket): NativeRecordSnapshot {
    validate(ticket)
    return store.mutate(ticket.revision) { values ->
      val journal = journal(values)
      val record = current(journal, ticket)
      if (record.getString("phase") !in terminal) throw NativeStateException("EBUSY")
      journal.remove(ticket.slot); save(values, journal)
    }
  }

  private fun validate (ticket: NativeCoordinationTicket) {
    if (ticket.scope != store.scope) throw NativeStateException("EACCES")
    identifier(ticket.slot)
    require(Regex("[a-f0-9-]{36}").matches(ticket.generation))
  }
  private fun current (journal: JSONObject, ticket: NativeCoordinationTicket): JSONObject {
    val record = journal.optJSONObject(ticket.slot) ?: throw NativeStateException("ENOENT")
    if (record.optString("generation") != ticket.generation) throw NativeStateException("ESTALE")
    return record
  }
  private fun transition (ticket: NativeCoordinationTicket, allowed: Set<String>, phase: String,
    updates: Map<String, ByteArray?> = emptyMap(), receipt: ByteArray = ByteArray(0)): NativeCoordinationState {
    validate(ticket)
    require(receipt.size <= 65536)
    var result: JSONObject? = null
    val changed = store.mutate(ticket.revision) { values ->
      val journal = journal(values)
      val record = current(journal, ticket)
      if (record.getString("phase") !in allowed) throw NativeStateException("EINVAL")
      store.changes(values, updates)
      record.put("phase", phase).put("owner", process)
      if (receipt.isNotEmpty()) record.put("receipt", android.util.Base64.encodeToString(receipt, android.util.Base64.NO_WRAP))
      save(values, journal); result = record
    }
    return state(ticket.slot, changed.revision, checkNotNull(result))
  }
}
