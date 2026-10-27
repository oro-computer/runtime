package oro.runtime.app

import android.app.PendingIntent
import android.app.Service
import android.app.job.JobParameters
import android.app.job.JobService
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.CancellationSignal
import android.os.IBinder
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.Executors
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Stable classes are generated from bundled policy, not loaded from a page or broadcast. */
open class RetainedReceiver(private val profile: String) : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val sender = if (Build.VERSION.SDK_INT >= 34) sentFromUid else -1
    val pending = goAsync(); val done = AtomicBoolean(false)
    fun finish() { if (done.compareAndSet(false, true)) pending.finish() }
    val timeout = RetainedComponents.deadlines.schedule({ finish() }, 8, TimeUnit.SECONDS)
    try { RetainedComponents.workers.execute {
      try { RetainedComponents.receive(context.applicationContext, profile, intent, sender)
      } catch (_: Exception) { oro.runtime.debug.console.error("Retained input was not admitted")
      } finally { timeout.cancel(false); finish() }
    } } catch (_: java.util.concurrent.RejectedExecutionException) { timeout.cancel(false); finish() }
  }
}

/** A legacy importance binder exposes no methods or authority. */
open class RetainedImportanceService : Service() {
  override fun onBind(intent: Intent?): IBinder = Binder()
}

/** Job identity and OS schedule stay intact; only an explicitly active handover admits a read. */
open class RetainedJobService(private val profile: String) : JobService() {
  private data class Run(val parameters: JobParameters, val signal: CancellationSignal = CancellationSignal(),
    val active: AtomicBoolean = AtomicBoolean(true))
  private val runs = java.util.concurrent.ConcurrentHashMap<Int, Run>()
  override fun onStartJob(parameters: JobParameters): Boolean {
    val run = Run(parameters)
    if (runs.size >= 8 || runs.putIfAbsent(parameters.jobId, run) != null) return false
    val timeout = RetainedComponents.deadlines.schedule({
      if (runs.remove(parameters.jobId, run)) {
        run.active.set(false); run.signal.cancel(); jobFinished(parameters, false)
      }
    }, 25, TimeUnit.SECONDS)
    try { RetainedComponents.workers.execute {
      try { RetainedComponents.job(applicationContext, profile, parameters, run.signal)
      } catch (_: Exception) { oro.runtime.debug.console.error("Retained job did not complete")
      } finally {
        timeout.cancel(false)
        if (runs.remove(parameters.jobId, run)) { run.active.set(false); jobFinished(parameters, false) }
      }
    } } catch (_: java.util.concurrent.RejectedExecutionException) { timeout.cancel(false); runs.remove(parameters.jobId, run); return false }
    return true
  }
  override fun onStopJob(parameters: JobParameters): Boolean {
    val run = runs[parameters.jobId] ?: return false
    if (run.parameters !== parameters || !runs.remove(parameters.jobId, run)) return false
    run.active.set(false); run.signal.cancel()
    return false
  }
  override fun onDestroy() {
    runs.values.forEach { it.active.set(false); it.signal.cancel() }; runs.clear(); super.onDestroy()
  }
}

internal object RetainedComponents {
  private val active = mutableMapOf<String, MutableSet<CancellationSignal>>()
  private val captured = java.util.WeakHashMap<Intent, Boolean>()
  val workers = ThreadPoolExecutor(2, 2, 30, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(8),
    { work -> Thread(work, "oro-retained-input").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  val deadlines = Executors.newSingleThreadScheduledExecutor { work -> Thread(work, "oro-retained-deadlines").apply { isDaemon = true } }
  private fun component(id: String): JSONObject = NativeMigration.definitions().components[id] ?: throw NativeStateException("ENOENT")
  private fun identity(component: String, reference: String) = MigrationPolicy.digest((component + ":" + reference).toByteArray(Charsets.UTF_8))
  private fun policyRevision(context: Context, policy: JSONObject, reference: String): String {
    val profile = policy.getString("source")
    val selected = NativeMigration.source(context, profile, reference)
    val definition = selected.definition
    val epoch = when(definition.optString("format", "aes-gcm")) {
      "native-records-v1" -> selected.epoch ?: throw NativeStateException("ESTALE")
      "preferences-json" -> context.packageName + ":" + definition.getString("preferences")
      else -> {
        val alias = MigrationPolicy.expand(definition.getString("alias"), context.packageName, reference)
        val keys = java.security.KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        alias + ":" + (keys.getCreationDate(alias)?.time ?: throw NativeStateException("EKEYREVOKED"))
      }
    }
    val value = JSONObject().put("component", policy).put("source", NativeMigration.profileConfiguration(profile)).put("epoch", epoch)
    if (NativeMigration.definitions().protected.getValue(profile).optString("format") == "prefer-native-records-v1")
      value.put("selectedSource", definition.getString("id"))
    return NativeMigration.revision(value)
  }
  private fun publicState(value: JSONObject) = JSONObject().put("id", value.getString("id"))
    .put("generation", value.getString("generation")).put("phase", value.getString("phase"))
    .put("sourceGeneration", value.getString("sourceGeneration")).put("sourceSha256", value.getString("sourceSha256"))
    .put("originalsPreserved", true).put("remoteRevocationConfirmed", false)
    .put("drained", synchronized(active) { active[value.getString("id")].isNullOrEmpty() })
  private fun requireSource(context: Context, policy: JSONObject, reference: String, expected: JSONObject, strictDigest: Boolean = true): JSONObject {
    val (plain, digest, _) = NativeMigration.sourcePlain(context, policy, reference)
    if ((strictDigest && digest != expected.getString("sourceSha256")) || MigrationPolicy.generation(policy, plain, digest) != expected.getString("sourceGeneration"))
      throw NativeStateException("ESTALE")
    return plain
  }
  private fun requireIdentity(policy: JSONObject, plain: JSONObject, value: JSONObject) {
    val mappings = policy.getJSONObject("identityFields")
    for (field in listOf("authorityRef", "accountRef", "targetRef")) {
      val mapping = mappings.getJSONObject(field)
      val raw = MigrationPolicy.pointer(plain, mapping.getString("pointer"))
      val expected = if (mapping.getString("encoding") == "sha256") MigrationPolicy.digest(raw.toByteArray(Charsets.UTF_8)) else raw
      if (value.getString(field) != expected) throw NativeStateException("EACCES")
    }
    policy.optJSONArray("checks")?.let { checks ->
      for (index in 0 until checks.length()) {
        val check = checks.getJSONObject(index)
        val actual = try { MigrationPolicy.at(plain, check.getString("pointer"))
        } catch (_: Exception) { if (check.has("default")) check.get("default") else throw NativeStateException("EACCES") }
        if (actual != check.get("equals")) throw NativeStateException("EACCES")
      }
    }
  }
  private fun replacement(context: Context, value: JSONObject) = NativeWorkflows.validateRetained(context,
    value.getString("bindingId"), value.getString("bindingGeneration"), value)

  fun request(call: PlatformCall, signal: CancellationSignal, origin: String, action: String, options: JSONObject): Any {
    return when(action) {
      "entries", "acknowledgeEntry" -> {
        require(call.index == 0)
        MigrationPolicy.keys(options, if (action == "entries") setOf("component") else setOf("component", "entryId"))
        val profile = MigrationPolicy.identifier(options.getString("component"))
        require(component(profile).getString("type") == "activity")
        if (action == "entries") JSONArray(NativeMigration.entries(call.app, "entry.").filter { it.second.getString("component") == profile }
          .map { (key, value) -> JSONObject(value.toString()).put("entryId", key.removePrefix("entry.")).put("untrusted", true) })
        else NativeMigration.locked {
          val key = "entry." + MigrationPolicy.identifier(options.getString("entryId"))
          val value = NativeMigration.read(call.app, key) ?: throw NativeStateException("ENOENT")
          if (value.getString("component") != profile) throw NativeStateException("EACCES")
          NativeMigration.remove(call.app, key); JSONObject().put("acknowledged", true)
        }
      }
      "inspectHandover" -> {
        MigrationPolicy.keys(options, setOf("component", "sourceRef"))
        val policy = component(options.getString("component")); require(policy.getString("type") in setOf("job", "receiver"))
        val reference = MigrationPolicy.identifier(options.getString("sourceRef"))
        val (plain, digest, _) = NativeMigration.sourcePlain(call.app, policy, reference)
        JSONObject().put("sourceSha256", digest).put("sourceGeneration", MigrationPolicy.generation(policy, plain, digest))
      }
      "prepareHandover" -> {
        MigrationPolicy.keys(options, setOf("component", "sourceRef", "sourceSha256", "sourceGeneration", "expectedGeneration",
          "bindingId", "bindingGeneration", "authorityRef", "accountRef", "targetRef", "userInitiated"))
        require(options.opt("userInitiated") == true)
        val profile = MigrationPolicy.identifier(options.getString("component")); val policy = component(profile)
        require(policy.getString("type") in setOf("job", "receiver"))
        val reference = MigrationPolicy.identifier(options.getString("sourceRef"))
        val value = NativeMigration.identity(options, origin)
        val plain = requireSource(call.app, policy, reference, options)
        requireIdentity(policy, plain, value)
        val id = identity(profile, reference)
        val fingerprint = NativeMigration.revision(options)
        value.put("id", id).put("component", profile).put("sourceRef", reference).put("policyRevision", policyRevision(call.app, policy, reference))
          .put("sourceSha256", options.getString("sourceSha256")).put("sourceGeneration", options.getString("sourceGeneration"))
          .put("bindingId", MigrationPolicy.identifier(options.getString("bindingId")))
          .put("bindingGeneration", MigrationPolicy.identifier(options.getString("bindingGeneration")))
          .put("requestSha256", fingerprint)
        replacement(call.app, value)
        if (!NativeConsent.approve(call, signal, "Approve historical delivery handover",
            "Allow retained '" + profile + "' inputs for this account to request the selected fixed workflow? Original keys, records and OS job identities will be preserved."))
          throw NativeStateException("ABORT_ERR")
        signal.throwIfCanceled()
        NativeMigration.locked {
          requireSource(call.app, policy, reference, options)
          publicState(NativeMigration.update(call.app, "handover.$id") { previous ->
            if (previous?.getString("requestSha256") == fingerprint) { NativeMigration.owner(previous, origin); previous }
            else {
              if (previous != null) {
                NativeMigration.owner(previous, origin)
                if (listOf("authorityRef", "accountRef", "targetRef").any { previous.getString(it) != value.getString(it) })
                  throw NativeStateException("EACCES")
              }
              if ((previous?.getString("generation") ?: "") != options.getString("expectedGeneration")) throw NativeStateException("ESTALE")
              if (previous == null && NativeMigration.entries(call.app, "handover.").size >= 16) throw NativeStateException("ENOSPC")
              value.put("generation", UUID.randomUUID().toString()).put("phase", "prepared")
            }
          })
        }
      }
      "discoverHandovers" -> {
        MigrationPolicy.keys(options, setOf("authorityRef", "accountRef", "targetRef"))
        val scope = NativeMigration.identity(options, origin)
        JSONArray(NativeMigration.entries(call.app, "handover.").filter { (_, value) ->
          value.getString("documentOrigin") == origin && listOf("authorityRef", "accountRef", "targetRef").all { value.getString(it) == scope.getString(it) }
        }.map { (_, value) -> publicState(value).put("component", value.getString("component")).put("sourceRef", value.getString("sourceRef")) })
      }
      "activateHandover", "statusHandover", "stopHandover", "receipts", "resolveReceipt", "releaseReceipt" -> NativeMigration.locked {
        val allowed = if (action == "resolveReceipt") setOf("id", "generation", "receiptId", "outcome", "receiptSha256", "userInitiated")
          else if (action == "releaseReceipt") setOf("id", "generation", "receiptId") else setOf("id", "generation")
        MigrationPolicy.keys(options, allowed)
        val id = MigrationPolicy.identifier(options.getString("id"))
        val value = NativeMigration.read(call.app, "handover.$id") ?: throw NativeStateException("ENOENT")
        NativeMigration.owner(value, origin)
        if (value.getString("generation") != options.getString("generation")) throw NativeStateException("ESTALE")
        when(action) {
          "activateHandover" -> {
            val policy = component(value.getString("component"))
            if (policyRevision(call.app, policy, value.getString("sourceRef")) != value.getString("policyRevision")) throw NativeStateException("ESTALE")
            if (value.getString("phase") !in setOf("prepared", "active")) throw NativeStateException("ESTALE")
            val plain = requireSource(call.app, policy, value.getString("sourceRef"), value, value.getString("phase") == "prepared")
            requireIdentity(policy, plain, value); replacement(call.app, value)
            signal.throwIfCanceled()
            if (value.getString("phase") == "active") publicState(value)
            else publicState(NativeMigration.update(call.app, "handover.$id") { it!!.put("phase", "active") })
          }
          "stopHandover" -> {
            val saved = NativeMigration.update(call.app, "handover.$id") {
              it!!.put("phase", "stopped").put("generation", UUID.randomUUID().toString())
            }
            val signals = synchronized(active) { active[id]?.toList() ?: emptyList() }
            signals.forEach { owned -> NativeExtensionWorkers.cancellations.execute { owned.cancel() } }
            publicState(saved)
          }
          "statusHandover" -> publicState(value)
          "receipts" -> JSONArray(NativeMigration.entries(call.app, "receipt.").filter { it.second.optString("handover") == id }
            .map { (key, receipt) -> JSONObject().put("receiptId", key.removePrefix("receipt."))
              .put("outcome", receipt.getString("outcome")).put("receivedAt", receipt.getLong("receivedAt"))
              .put("sourceGeneration", receipt.getString("sourceGeneration")).put("kind", receipt.getString("kind")) })
          else -> {
            val receiptId = MigrationPolicy.identifier(options.getString("receiptId"))
            val receipt = NativeMigration.read(call.app, "receipt.$receiptId") ?: throw NativeStateException("ENOENT")
            if (receipt.getString("handover") != id)
              throw NativeStateException("ESTALE")
            if (action == "resolveReceipt") {
              require(options.opt("userInitiated") == true && options.getString("outcome") in setOf("applied", "not-applied") &&
                Regex("[a-f0-9]{64}").matches(options.getString("receiptSha256")))
              NativeMigration.update(call.app, "receipt.$receiptId") { old ->
                if (old!!.has("applicationResolution") && (old.getString("applicationResolution") != options.getString("outcome") ||
                    old.getString("receiptSha256") != options.getString("receiptSha256"))) throw NativeStateException("EEXIST")
                old.put("applicationResolution", options.getString("outcome")).put("receiptSha256", options.getString("receiptSha256"))
              }
              JSONObject().put("resolvedLocally", true).put("remoteOutcomeVerifiedByRuntime", false)
            } else {
              if (!receipt.has("applicationResolution")) throw NativeStateException("EBUSY")
              NativeMigration.remove(call.app, "receipt.$receiptId")
              JSONObject().put("released", true)
            }
          }
        }
      }
      else -> throw NativeStateException("ENOSYS")
    }
  }

  /** Historical entries are untrusted routing references, never sign-in or mutation authority. */
  fun activity(context: Context, intent: Intent) {
    if (App.getInstance().getUserConfigValue("migration_enabled") != "true") return
    val policy = NativeMigration.definitions().components.values.firstOrNull {
      it.getString("type") == "activity" && it.getString("className") == intent.component?.className && it.has("actions")
    } ?: return
    val actions = policy.getJSONArray("actions")
    if ((0 until actions.length()).none { actions.getString(it) == intent.action }) return
    synchronized(captured) {
      if (captured.containsKey(intent)) return
      if (captured.size >= 64) throw NativeStateException("ENOSPC")
      captured[intent] = true
    }
    val references = JSONObject(); val extras = policy.getJSONArray("referenceExtras")
    for (index in 0 until extras.length()) {
      val key = extras.getString(index)
      intent.getStringExtra(key)?.let { references.put(key, MigrationPolicy.identifier(it)) }
    }
    val value = JSONObject().put("component", policy.getString("id")).put("action", intent.action)
      .put("references", references).put("receivedAt", System.currentTimeMillis())
    workers.execute {
      try { NativeMigration.locked {
        if (NativeMigration.entries(context, "entry.").size >= 15) throw NativeStateException("ENOSPC")
        NativeMigration.update(context, "entry." + UUID.randomUUID().toString()) { value }
      } } catch (_: Exception) { oro.runtime.debug.console.error("Historical routing entry was not retained") }
    }
  }

  private fun capture(context: Context, profile: String, reference: String, sourceDigest: String, sourceGeneration: String,
    kind: String, event: String, payload: JSONObject): String = NativeMigration.locked {
    val id = MigrationPolicy.digest((profile + ":" + reference + ":" + sourceGeneration + ":" + event).toByteArray(Charsets.UTF_8))
    val key = "receipt.$id"
    val previous = NativeMigration.read(context, key)
    if (previous == null && NativeMigration.entries(context, "receipt.").size >= 48) throw NativeStateException("ENOSPC")
    NativeMigration.update(context, key) { it ?: JSONObject().put("handover", identity(profile, reference)).put("sourceSha256", sourceDigest)
      .put("sourceGeneration", sourceGeneration).put("kind", kind).put("receivedAt", System.currentTimeMillis())
      .put("payload", payload).put("outcome", "unknown") }
    id
  }
  private fun admitted(context: Context, profile: String, reference: String, policy: JSONObject, digest: String, generation: String): JSONObject? {
    val value = NativeMigration.read(context, "handover." + identity(profile, reference)) ?: return null
    if (value.optString("phase") != "active" || (!policy.has("generationPointer") && value.optString("sourceSha256") != digest) ||
        value.optString("sourceGeneration") != generation || value.optString("policyRevision") != policyRevision(context, policy, reference)) return null
    requireIdentity(policy, requireSource(context, policy, reference, value, false), value)
    replacement(context, value)
    return value
  }
  private fun references(context: Context, policy: JSONObject): List<String> {
    val (source, names) = NativeMigration.sourceNames(context, policy.getString("source"))
    val template = MigrationPolicy.expand(source.getString("key"), context.packageName, "MIGRATION_REFERENCE")
    if (!source.getString("key").contains("{sourceRef}")) return listOf(source.getString("id"))
    val pattern = Regex("^" + Regex.escape(template).replace("MIGRATION_REFERENCE", "\\E([A-Za-z0-9_-]{1,128})\\Q") + "$")
    val keys = names.mapNotNull { pattern.matchEntire(it)?.groupValues?.get(1) }
    require(keys.size <= 32)
    return keys
  }
  fun receive(context: Context, profile: String, intent: Intent, senderUid: Int) {
    val policy = component(profile); require(policy.getString("type") == "receiver")
    val actions = policy.getJSONArray("actions")
    if ((0 until actions.length()).none { actions.getString(it) == intent.action }) return
    val token = intent.getStringExtra(policy.getString("tokenExtra")) ?: return
    if (token.toByteArray(Charsets.UTF_8).size !in 1..4096) return
    var selected: Pair<String, Triple<JSONObject, String, JSONObject>>? = null
    for (reference in references(context, policy)) {
      val source = NativeMigration.sourcePlain(context, policy, reference)
      val expectedToken = runCatching { MigrationPolicy.pointer(source.first, policy.getString("tokenPointer")) }.getOrNull() ?: continue
      if (!java.security.MessageDigest.isEqual(token.toByteArray(Charsets.UTF_8), expectedToken.toByteArray(Charsets.UTF_8))) continue
      if (selected != null) throw NativeStateException("EACCES")
      selected = reference to source
    }
    val (reference, source) = selected ?: return
    val (plain, digest, _) = source
    val sender = if (policy.has("senderPointer")) MigrationPolicy.pointer(plain, policy.getString("senderPointer")) else
      context.getSharedPreferences(policy.getString("senderPreferences"), Context.MODE_PRIVATE).getString(policy.getString("senderKey"), null) ?: return
    val expectedUid = context.packageManager.getApplicationInfo(sender, 0).uid
    val proof = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra("pi", PendingIntent::class.java)
      else @Suppress("DEPRECATION") (intent.getParcelableExtra("pi") as? PendingIntent)
    if (senderUid >= 0 && senderUid != expectedUid) return
    if (proof != null && (proof.creatorPackage != sender || proof.creatorUid != expectedUid)) return
    if (senderUid < 0 && proof == null) return
    val generation = MigrationPolicy.generation(policy, plain, digest)
    val payload = JSONObject().put("action", intent.action)
    for (name in listOf("id", "endpoint", "message")) intent.getStringExtra(name)?.let { require(it.length <= 4096); payload.put(name, it) }
    intent.getByteArrayExtra("bytesMessage")?.let { require(it.size <= 4096); payload.put("bytesMessage", android.util.Base64.encodeToString(it, android.util.Base64.NO_WRAP)) }
    val event = intent.action + ":" + (intent.getStringExtra("id") ?: NativeMigration.revision(payload))
    capture(context, profile, reference, digest, generation, "receiver", event, payload)
    NativeMigration.locked {
      val value = admitted(context, profile, reference, policy, digest, generation)
      if (value != null) NativeWorkflows.hintRetained(context, value.getString("bindingId"), value.getString("bindingGeneration"), value)
    }
  }
  fun job(context: Context, profile: String, parameters: JobParameters, signal: CancellationSignal) {
    val policy = component(profile); require(policy.getString("type") == "job")
    require(parameters.jobId in policy.getInt("minimumJobId")..policy.getInt("maximumJobId"))
    val reference = MigrationPolicy.identifier(parameters.extras.getString(policy.getString("referenceExtra")) ?: return)
    val (plain, digest, _) = NativeMigration.sourcePlain(context, policy, reference)
    val generation = MigrationPolicy.generation(policy, plain, digest)
    if (policy.has("generationExtra") && parameters.extras.getString(policy.getString("generationExtra")) != generation) return
    val id = identity(profile, reference)
    val admitted = NativeMigration.locked {
      if (NativeMigration.entries(context, "receipt.").any { (_, receipt) -> receipt.optString("handover") == id &&
          receipt.optString("sourceGeneration") == generation && receipt.optString("kind") == "job" &&
          receipt.optString("outcome") == "unknown" && !receipt.has("applicationResolution") }) return@locked null
      val receipt = capture(context, profile, reference, digest, generation, "job", UUID.randomUUID().toString(), JSONObject().put("jobId", parameters.jobId))
      val value = admitted(context, profile, reference, policy, digest, generation) ?: return@locked null
      synchronized(active) { active.getOrPut(id) { mutableSetOf() }.add(signal) }
      receipt to value
    } ?: return
    try {
      signal.throwIfCanceled()
      NativeWorkflows.readRetained(context, admitted.second.getString("bindingId"), admitted.second.getString("bindingGeneration"), admitted.second, signal)
      signal.throwIfCanceled()
      NativeMigration.locked { NativeMigration.update(context, "receipt.${admitted.first}") { it!!.put("outcome", "read-completed") } }
    } finally { synchronized(active) { active[id]?.let { it.remove(signal); if (it.isEmpty()) active.remove(id) } } }
  }
}
