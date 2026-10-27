package oro.runtime.app

import android.content.Context
import android.os.CancellationSignal
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** Read-only historical inputs and encrypted, document-bound migration receipts. */
internal object NativeMigration {
  private fun store(context: Context) = NativeRecords.runtime(context, "migration.v1")
  private val gate = Any()
  internal fun <T> locked(operation: () -> T): T = synchronized(gate) { operation() }
  private var cached: MigrationPolicy.Definitions? = null
  @Synchronized internal fun definitions(): MigrationPolicy.Definitions {
    if (App.getInstance().getUserConfigValue("migration_enabled") != "true") throw NativeStateException("ENOSYS")
    cached?.let { return it }
    val bytes = App.getInstance().assets.open("oro-migration.json").use { it.readBytesBounded(65536) }
    return MigrationPolicy.parse(Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
      .decode(java.nio.ByteBuffer.wrap(bytes)).toString()).also { cached = it }
  }
  private fun java.io.InputStream.readBytesBounded(maximum: Int): ByteArray {
    val output = java.io.ByteArrayOutputStream(); val buffer = ByteArray(4096)
    while (true) { val count = read(buffer); if (count < 0) break; require(output.size() + count <= maximum); output.write(buffer, 0, count) }
    return output.toByteArray()
  }
  internal data class Source(val definition: JSONObject, val raw: String, val sha256: String, val epoch: String? = null)
  /** Selection is read-only. A present native scope, including an empty one, is authoritative. */
  internal fun selectedDefinition(context: Context, profile: String): JSONObject {
    val root = definitions().protected[profile] ?: throw NativeStateException("ENOENT")
    if (root.optString("format") != "prefer-native-records-v1") return root
    val primary = definitions().protected.getValue(root.getString("primary"))
    val selected = if (recordSource(context, primary, "public-reference").status().revision == 0L)
      definitions().protected.getValue(root.getString("fallback")) else primary
    return JSONObject(selected.toString()).apply {
      remove("exposure")
      if (root.has("exposure")) put("exposure", root.getString("exposure"))
    }
  }
  /** Static dependencies, independent of source availability, also bind already imported copies. */
  internal fun profileConfiguration(profile: String): JSONObject {
    val value = definitions().protected[profile] ?: throw NativeStateException("ENOENT")
    if (value.optString("format") == "prefer-native-records-v1") return JSONObject().put("definition", value)
      .put("primary", profileConfiguration(value.getString("primary"))).put("fallback", profileConfiguration(value.getString("fallback")))
    if (value.has("provenance")) return JSONObject().put("definition", value)
      .put("legacy", definitions().protected.getValue(value.getJSONObject("provenance").getString("legacy")))
    return value
  }
  private fun provenanceDigest(context: Context, definition: JSONObject): String? {
    if (!definition.has("provenance")) return null
    val legacy = definitions().protected.getValue(definition.getJSONObject("provenance").getString("legacy"))
    return MigrationPolicy.legacyDigest(context.getSharedPreferences(legacy.getString("preferences"), Context.MODE_PRIVATE).all, legacy, context.packageName)
  }
  private fun requireProvenance(context: Context, definition: JSONObject, snapshot: NativeRecordSnapshot, digest: String?) {
    if (digest == null) return
    val guard = definition.getJSONObject("provenance")
    val bytes = snapshot.records[guard.getString("markerKey")] ?: throw NativeStateException("EBADMSG")
    val marker = try { envelope(Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
      .decode(java.nio.ByteBuffer.wrap(bytes)).toString()) } catch (_: Exception) { throw NativeStateException("EBADMSG") }
    if (runCatching { MigrationPolicy.at(marker, guard.getString("formatPointer")) == guard.getInt("format") }.getOrDefault(false) != true)
      throw NativeStateException("EBADMSG")
    val recorded = runCatching { MigrationPolicy.at(marker, guard.getString("digestPointer")) as? String }.getOrNull()
    if (recorded == null || !Regex("[a-f0-9]{64}").matches(recorded)) throw NativeStateException("EBADMSG")
    if (recorded != digest) throw NativeStateException("EEXIST")
    if (provenanceDigest(context, definition) != digest) throw NativeStateException("ESTALE")
  }
  internal fun sourceNames(context: Context, profile: String): Pair<JSONObject, List<String>> {
    val definition = selectedDefinition(context, profile)
    val digest = provenanceDigest(context, definition)
    val names = if (definition.optString("format") == "native-records-v1") {
      val (_, snapshot) = recordSource(context, definition, "public-reference").migrationSource()
      try { requireProvenance(context, definition, snapshot, digest); snapshot.records.keys.toList() }
      finally { snapshot.records.values.forEach { it.fill(0) } }
    } else context.getSharedPreferences(definition.getString("preferences"), Context.MODE_PRIVATE).all.keys.toList()
    if (selectedDefinition(context, profile).getString("id") != definition.getString("id")) throw NativeStateException("ESTALE")
    return definition to names
  }
  internal fun recordSource(context: Context, definition: JSONObject, reference: String): NativeRecordStore {
    val records = definition.getJSONObject("records"); val identity = records.getJSONObject("identity")
    fun field(name: String): String {
      val selector = identity.get(name)
      return if (selector is String) MigrationPolicy.expand(selector, context.packageName, reference) else {
        val value = selector as JSONObject
        context.getSharedPreferences(value.getString("preferences"), Context.MODE_PRIVATE)
          .getString(value.getString("key"), value.getString("default")) ?: throw NativeStateException("ENOENT")
      }
    }
    return NativeRecords.open(context, records.getString("namespace"), NativeRecordIdentity(field("authorityRef"), field("accountRef"), field("identityRef")))
  }
  private fun recordDigest(context: Context, bytes: ByteArray, domain: String): String = locked {
    val metadata = read(context, "source-digest-key") ?: update(context, "source-digest-key") { it ?: JSONObject().put("key",
      Base64.encodeToString(ByteArray(32).also { java.security.SecureRandom().nextBytes(it) }, Base64.NO_WRAP)) }
    val key = Base64.decode(metadata.getString("key"), Base64.NO_WRAP)
    try {
      javax.crypto.Mac.getInstance("HmacSHA256").run {
        init(javax.crypto.spec.SecretKeySpec(key, "HmacSHA256")); update(domain.toByteArray(Charsets.UTF_8))
        doFinal(bytes).joinToString("") { "%02x".format(it.toInt() and 255) }
      }
    } finally { key.fill(0) }
  }
  internal fun source(context: Context, profile: String, reference: String): Source {
    val definition = selectedDefinition(context, profile)
    val key = MigrationPolicy.expand(definition.getString("key"), context.packageName, reference)
    if (definition.optString("format") == "native-records-v1") {
      val records = recordSource(context, definition, reference)
      val digest = provenanceDigest(context, definition)
      val (epoch, snapshot) = records.migrationSource()
      try {
        requireProvenance(context, definition, snapshot, digest)
        val bytes = snapshot.records[key] ?: throw NativeStateException("ENOENT")
        val raw = Base64.encodeToString(bytes, Base64.NO_WRAP)
        return Source(definition, raw, recordDigest(context, bytes, profile + ":" + records.scope + ":" + epoch + ":" + key), records.scope + ":" + epoch)
      } finally { snapshot.records.values.forEach { it.fill(0) } }
    }
    val raw = context.getSharedPreferences(definition.getString("preferences"), Context.MODE_PRIVATE).getString(key, null)
      ?: throw NativeStateException("ENOENT")
    if (selectedDefinition(context, profile).getString("id") != definition.getString("id")) throw NativeStateException("ESTALE")
    if (raw.toByteArray(Charsets.UTF_8).size > MigrationPolicy.MAX_SOURCE) throw NativeStateException("EBADMSG")
    if (definition.optString("format") == "preferences-json") {
      envelope(raw)
      return Source(definition, raw, recordDigest(context, raw.toByteArray(Charsets.UTF_8), profile + ":" + context.packageName + ":" + key))
    }
    return Source(definition, raw, MigrationPolicy.digest(raw.toByteArray(Charsets.UTF_8)))
  }
  internal fun sourcePlain(context: Context, component: JSONObject, reference: String): Triple<JSONObject, String, JSONObject> {
    val selected = source(context, component.getString("source"), reference); val (definition, raw) = selected
    val clear = if (definition.optString("format") == "native-records-v1") Base64.decode(raw, Base64.NO_WRAP)
      else if (definition.optString("format") == "preferences-json") raw.toByteArray(Charsets.UTF_8)
      else ProtectedAuthentication.legacyPlain(context, definition, reference, envelope(raw))
    try {
      val value = envelope(Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
        .decode(java.nio.ByteBuffer.wrap(clear)).toString())
      if (MigrationPolicy.pointer(value, component.getString("referencePointer")) != reference) throw NativeStateException("EACCES")
      return Triple(value, selected.sha256, definition)
    } finally { clear.fill(0) }
  }
  private fun envelope(raw: String): JSONObject = try { MigrationPolicy.json(raw, MigrationPolicy.MAX_SOURCE)
  } catch (_: Exception) { throw NativeStateException("EBADMSG") }
  internal fun read(context: Context, key: String): JSONObject? {
    val snapshot = store(context).read()
    try { return snapshot.records[key]?.let { JSONObject(it.toString(Charsets.UTF_8)) } }
    finally { snapshot.records.values.forEach { it.fill(0) } }
  }
  internal fun update(context: Context, key: String, transform: (JSONObject?) -> JSONObject): JSONObject {
    repeat(8) {
      val snapshot = store(context).read()
      try {
        val value = transform(snapshot.records[key]?.let { JSONObject(it.toString(Charsets.UTF_8)) })
        val bytes = value.toString().toByteArray(Charsets.UTF_8)
        try {
          try { store(context).apply(snapshot.revision, mapOf(key to bytes), UUID.randomUUID().toString()).records.values.forEach { it.fill(0) }
          } catch (error: NativeStateException) { if (error.code == "ESTALE") return@repeat else throw error }
        } finally { bytes.fill(0) }
        return value
      } finally { snapshot.records.values.forEach { it.fill(0) } }
    }
    throw NativeStateException("EBUSY")
  }
  internal fun entries(context: Context, prefix: String): List<Pair<String, JSONObject>> {
    val snapshot = store(context).read()
    try { return snapshot.records.filterKeys { it.startsWith(prefix) }.map { it.key to JSONObject(it.value.toString(Charsets.UTF_8)) } }
    finally { snapshot.records.values.forEach { it.fill(0) } }
  }
  internal fun remove(context: Context, key: String) {
    repeat(8) {
      val snapshot = store(context).read()
      try { store(context).apply(snapshot.revision, mapOf(key to null), UUID.randomUUID().toString()).records.values.forEach { it.fill(0) }; return
      } catch (error: NativeStateException) { if (error.code != "ESTALE") throw error
      } finally { snapshot.records.values.forEach { it.fill(0) } }
    }
    throw NativeStateException("EBUSY")
  }
  internal fun origin(call: PlatformCall): String {
    val uri = android.net.Uri.parse(call.foreground().fragment.webview.url ?: throw PlatformFailure("ENOTREADY"))
    return uri.scheme + "://" + uri.encodedAuthority
  }
  internal fun identity(options: JSONObject, origin: String) = JSONObject().put("documentOrigin", origin).apply {
    for (field in listOf("authorityRef", "accountRef", "targetRef")) put(field, MigrationPolicy.identifier(options.getString(field)))
  }
  internal fun owner(value: JSONObject, origin: String) {
    if (value.getString("documentOrigin") != origin) throw NativeStateException("EACCES")
  }
  private fun canonical(value: Any?): String = when(value) {
    null, JSONObject.NULL -> "null"
    is JSONObject -> value.keys().asSequence().toList().sorted().joinToString(",", "{", "}") { JSONObject.quote(it) + ":" + canonical(value.get(it)) }
    is JSONArray -> (0 until value.length()).joinToString(",", "[", "]") { canonical(value.get(it)) }
    is String -> JSONObject.quote(value)
    is Boolean, is Number -> value.toString()
    else -> throw NativeStateException("EINVAL")
  }
  internal fun revision(value: JSONObject) = MigrationPolicy.digest(canonical(value).toByteArray(Charsets.UTF_8))
  private fun saveImport(context: Context, id: String, value: JSONObject): JSONObject {
    val encrypted = Base64.decode(value.getJSONObject("sealed").getString("ciphertext"), Base64.NO_WRAP)
    val metadata = JSONObject(value.toString()); val sealed = metadata.getJSONObject("sealed")
    sealed.remove("ciphertext"); sealed.put("bodyParts", if (encrypted.size <= 32768) 1 else 2)
    val updates = linkedMapOf("import.$id" to metadata.toString().toByteArray(Charsets.UTF_8))
    updates["import-body.$id.0"] = encrypted.copyOfRange(0, minOf(32768, encrypted.size))
    if (encrypted.size > 32768) updates["import-body.$id.1"] = encrypted.copyOfRange(32768, encrypted.size)
    try {
      repeat(8) {
        val snapshot = store(context).read()
        try {
          snapshot.records["import.$id"]?.let { return JSONObject(it.toString(Charsets.UTF_8)) }
          if (snapshot.records.keys.count { it.startsWith("import.") } >= 16) throw NativeStateException("ENOSPC")
          store(context).apply(snapshot.revision, updates, UUID.randomUUID().toString()).records.values.forEach { it.fill(0) }
          return metadata
        } catch (error: NativeStateException) { if (error.code != "ESTALE") throw error
        } finally { snapshot.records.values.forEach { it.fill(0) } }
      }
      throw NativeStateException("EBUSY")
    } finally { encrypted.fill(0); updates.values.forEach { it.fill(0) } }
  }
  private fun sealedImport(context: Context, id: String, origin: String): Pair<JSONObject, JSONObject> {
    val snapshot = store(context).read()
    try {
      val record = JSONObject((snapshot.records["import.$id"] ?: throw NativeStateException("ENOENT")).toString(Charsets.UTF_8))
      owner(record, origin)
      val sealed = record.getJSONObject("sealed")
      val count = sealed.getInt("bodyParts"); require(count in 1..2)
      val first = snapshot.records["import-body.$id.0"] ?: throw NativeStateException("EBADMSG")
      val second = if (count == 2) snapshot.records["import-body.$id.1"] ?: throw NativeStateException("EBADMSG") else ByteArray(0)
      val body = first + second
      try { require(body.size in 16..65552); sealed.put("ciphertext", Base64.encodeToString(body, Base64.NO_WRAP)) }
      finally { body.fill(0) }
      return record to sealed
    } finally { snapshot.records.values.forEach { it.fill(0) } }
  }
  private fun imported(value: JSONObject, id: String) = JSONObject().put("handle", id).put("profile", value.getString("profile"))
    .put("sourceSha256", value.getString("sourceSha256")).put("valueExported", false).put("originalsPreserved", true)

  fun request(call: PlatformCall, action: String, options: JSONObject) {
    val origin = origin(call)
    if (action == "support") {
      require(options.length() == 0)
      try {
        val policies = definitions()
        call.success(JSONObject().put("available", true).put("protectedImport", "protected-import-v1")
          .put("retainedComponents", "retained-components-v1").put("nativeIntegrationRequired", false)
          .put("sourceSelection", "prefer-native-records-v1").put("legacyProvenance", "android-json-pairs-sha256-v1")
          .put("generatedAdapters", App.getInstance().getUserConfigValue("migration_generate_adapters") != "false")
          .put("maximumImports", 16).put("maximumReceipts", 48).put("maximumEntries", 15).put("maximumStoredBytes", 262144)
          .put("profiles", JSONArray(policies.protected.keys.toList())).put("components", JSONArray(policies.components.keys.toList())))
      } catch (error: Exception) {
        call.success(JSONObject().put("available", false).put("error", if (error is NativeStateException) error.code else "EINVAL")
          .put("nativeIntegrationRequired", false).put("profiles", JSONArray()).put("components", JSONArray()))
      }
      return
    }
    definitions()
    NativeExtensionCall(call).runOnWorker { signal ->
      try {
        when(action) {
          "inspectProtected" -> {
            MigrationPolicy.keys(options, setOf("profile", "sourceRef"))
            val profile = MigrationPolicy.identifier(options.getString("profile")); val reference = MigrationPolicy.identifier(options.getString("sourceRef"))
            val selected = source(call.app, profile, reference)
            JSONObject().put("profile", profile).put("sourceSha256", selected.sha256)
              .put("originalsPreserved", true)
          }
          "importProtected" -> {
            MigrationPolicy.keys(options, setOf("profile", "sourceRef", "sourceSha256", "authorityRef", "accountRef", "targetRef", "userInitiated", "recoveryRef"))
            require(options.opt("userInitiated") == true)
            val profile = MigrationPolicy.identifier(options.getString("profile")); val reference = MigrationPolicy.identifier(options.getString("sourceRef"))
            val selected = source(call.app, profile, reference); val (definition, raw) = selected
            val digest = selected.sha256
            if (digest != options.getString("sourceSha256")) throw NativeStateException("ESTALE")
            val value = identity(options, origin).put("profile", profile).put("sourceRef", reference)
              .put("sourceSha256", digest).put("policyRevision", revision(profileConfiguration(profile)))
            if (options.has("recoveryRef")) value.put("recoveryRef", MigrationPolicy.identifier(options.getString("recoveryRef")))
            val id = "migration-" + revision(value)
            val existing = read(call.app, "import.$id")
            if (existing != null) imported(existing, id) else {
              if (entries(call.app, "import.").size >= 16) throw NativeStateException("ENOSPC")
              val native = if (definition.optString("format") == "native-records-v1") Base64.decode(raw, Base64.NO_WRAP)
                else if (definition.optString("format") == "preferences-json") raw.toByteArray(Charsets.UTF_8) else null
              val sealed = try { ProtectedAuthentication.rewrapLegacy(call, signal, definition, reference, if (native == null) envelope(raw) else JSONObject(), id, native)
              } finally { native?.fill(0) }
              signal.throwIfCanceled()
              if (source(call.app, profile, reference).sha256 != digest)
                throw NativeStateException("ESTALE")
              value.put("sealed", sealed)
              val saved = locked { saveImport(call.app, id, value) }
              imported(saved, id)
            }
          }
          "openProtected" -> {
            MigrationPolicy.keys(options, setOf("handle", "userInitiated")); require(options.opt("userInitiated") == true)
            val id = MigrationPolicy.identifier(options.getString("handle"))
            val (record, sealed) = sealedImport(call.app, id, origin)
            if (!definitions().protected.containsKey(record.getString("profile"))) throw NativeStateException("ESTALE")
            if (record.getString("policyRevision") != revision(profileConfiguration(record.getString("profile")))) throw NativeStateException("ESTALE")
            val clear = ProtectedAuthentication.openMigrated(call, signal, sealed)
            try { JSONObject().put("value", Base64.encodeToString(clear, Base64.NO_WRAP)) } finally { clear.fill(0) }
          }
          "discoverProtected" -> {
            MigrationPolicy.keys(options, setOf("profile", "sourceRef", "authorityRef", "accountRef", "targetRef"))
            val scope = identity(options, origin)
            JSONArray(entries(call.app, "import.").filter { (_, value) ->
              value.getString("documentOrigin") == origin && value.getString("profile") == options.getString("profile") &&
                value.getString("sourceRef") == options.getString("sourceRef") &&
                listOf("authorityRef", "accountRef", "targetRef").all { value.getString(it) == scope.getString(it) }
            }.map { (key, value) -> imported(value, key.removePrefix("import.")) })
          }
          "readUnprotected" -> {
            MigrationPolicy.keys(options, setOf("profile", "sourceRef", "userInitiated")); require(options.opt("userInitiated") == true)
            val selected = source(call.app, MigrationPolicy.identifier(options.getString("profile")), MigrationPolicy.identifier(options.getString("sourceRef")))
            val definition = selected.definition
            if (definition.getString("authentication") != "none" || definition.optString("exposure") != "private-document") throw NativeStateException("EACCES")
            if (!NativeConsent.approve(call, signal, "Read declared historical data", "Allow this private application document to read the selected historical record? This does not sign in or authorize a server."))
              throw NativeStateException("ABORT_ERR")
            val current = source(call.app, options.getString("profile"), options.getString("sourceRef"))
            if (current.sha256 != selected.sha256) throw NativeStateException("ESTALE")
            val bytes = when(definition.optString("format")) {
              "native-records-v1" -> Base64.decode(current.raw, Base64.NO_WRAP)
              "preferences-json" -> current.raw.toByteArray(Charsets.UTF_8)
              else -> ProtectedAuthentication.legacyPlain(call.app, definition, options.getString("sourceRef"), envelope(current.raw))
            }
            try { JSONObject().put("value", Base64.encodeToString(bytes, Base64.NO_WRAP)).put("originalsPreserved", true) }
            finally { bytes.fill(0) }
          }
          else -> RetainedComponents.request(call, signal, origin, action, options)
        }
      } catch (_: android.security.keystore.KeyPermanentlyInvalidatedException) { throw NativeStateException("EKEYREVOKED")
      } catch (_: java.security.UnrecoverableKeyException) { throw NativeStateException("EKEYREVOKED")
      } catch (_: javax.crypto.AEADBadTagException) { throw NativeStateException("EBADMSG") }
    }
  }
}
