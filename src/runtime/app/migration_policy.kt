package oro.runtime.app

import org.json.JSONObject
import org.json.JSONArray
import java.security.MessageDigest

/** Immutable migration selectors; a page cannot supply aliases, codecs, paths or component classes. */
internal object MigrationPolicy {
  const val MAX_SOURCE = 100000
  fun digest(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes)
    .joinToString("") { "%02x".format(it.toInt() and 255) }
  fun identifier(value: String): String = value.also { require(Regex("[A-Za-z0-9_-]{1,128}").matches(it)) }
  fun keys(value: JSONObject, allowed: Set<String>) { require(value.keys().asSequence().all { it in allowed }) }
  fun text(value: String, maximum: Int = 256): String = value.also {
    require(it.isNotEmpty() && it.toByteArray(Charsets.UTF_8).size <= maximum && it.none { ch -> ch.code < 32 })
  }
  fun expand(value: String, application: String, reference: String): String = value
    .replace("{package}", application).replace("{sourceRef}", identifier(reference))
  fun at(root: JSONObject, path: String): Any {
    require(path.startsWith('/') && path.length <= 256)
    var value: Any = root
    for (part in path.drop(1).split('/')) {
      require(part.isNotEmpty() && !part.contains('~'))
      value = (value as? JSONObject)?.get(part) ?: throw NativeStateException("EBADMSG")
    }
    return value
  }
  fun pointer(root: JSONObject, path: String): String {
    val value = at(root, path)
    require(value is String || value is Long || value is Int)
    return text(value.toString(), 4096)
  }
  fun generation(component: JSONObject, source: JSONObject, sourceDigest: String): String =
    if (component.has("generationPointer")) pointer(source, component.getString("generationPointer")) else sourceDigest

  /** Exact Android JSON serialization of sorted name/ciphertext pairs; never hashes plaintext. */
  fun legacyDigest(values: Map<String, *>, definition: JSONObject, application: String): String {
    val prefix = familyPrefix(definition, application)
    val names = values.keys.asSequence().filter { it.startsWith(prefix) }.take(33).toList()
    if (names.size > 32) throw NativeStateException("ENOSPC")
    var total = 0
    val rows = JSONArray()
    val ordered = names.sortedWith(Comparator { left, right ->
      val first = left.toByteArray(Charsets.UTF_8); val second = right.toByteArray(Charsets.UTF_8)
      var order = 0
      for (at in 0 until minOf(first.size, second.size)) {
        order = (first[at].toInt() and 255) - (second[at].toInt() and 255)
        if (order != 0) break
      }
      if (order != 0) order else first.size - second.size
    })
    for (name in ordered) {
      val value = values[name] as? String ?: throw NativeStateException("EBADMSG")
      total += name.toByteArray(Charsets.UTF_8).size + value.toByteArray(Charsets.UTF_8).size
      if (total > 262144 || value.toByteArray(Charsets.UTF_8).size > MAX_SOURCE) throw NativeStateException("ENOSPC")
      rows.put(JSONArray().put(name).put(value))
    }
    return digest(rows.toString().toByteArray(Charsets.UTF_8))
  }
  fun familyPrefix(definition: JSONObject, application: String): String {
    val key = definition.getString("key")
    require(key.endsWith("{sourceRef}") && key.indexOf("{sourceRef}") == key.lastIndexOf("{sourceRef}"))
    return expand(key.removeSuffix("{sourceRef}"), application, "public-reference").also { require(it.isNotEmpty() && !it.contains(Regex("[{}]"))) }
  }

  data class Definitions(val protected: Map<String, JSONObject>, val components: Map<String, JSONObject>)
  fun json(text: String, maximum: Int = 65536): JSONObject {
    require(text.toByteArray(Charsets.UTF_8).size <= maximum)
    var depth = 0; var quoted = false; var escaped = false
    for (character in text) {
      if (quoted) {
        if (escaped) escaped = false else if (character == '\\') escaped = true else if (character == '"') quoted = false
      } else if (character == '"') quoted = true
      else if (character == '{' || character == '[') { depth++; require(depth <= 16) }
      else if (character == '}' || character == ']') { depth--; require(depth >= 0) }
    }
    require(depth == 0 && !quoted)
    return JSONObject(text)
  }
  fun parse(text: String): Definitions {
    val root = json(text)
    keys(root, setOf("schema", "protected", "components")); require(root.opt("schema") == 1)
    val sources = linkedMapOf<String, JSONObject>()
    val components = linkedMapOf<String, JSONObject>()
    val sourceRows = root.getJSONArray("protected"); require(sourceRows.length() <= 16)
    for (index in 0 until sourceRows.length()) {
      val value = sourceRows.getJSONObject(index)
      keys(value, setOf("id", "format", "records", "preferences", "key", "alias", "aad", "encoding", "ciphertextField", "authentication", "policy", "exposure", "provenance", "primary", "fallback"))
      val id = identifier(value.getString("id")); require(!sources.containsKey(id))
      require(value.getString("authentication") in setOf("none", "biometric", "biometric-or-device"))
      require(value.getString("policy") in setOf("biometric", "biometric-or-device"))
      if (value.has("exposure")) require(value.getString("exposure") == "private-document" && value.getString("authentication") == "none")
      if (value.optString("format") == "prefer-native-records-v1") {
        keys(value, setOf("id", "format", "primary", "fallback", "authentication", "policy", "exposure"))
        require(value.getString("authentication") == "none")
        identifier(value.getString("primary")); identifier(value.getString("fallback"))
        sources[id] = value; continue
      }
      if (value.optString("format") == "preferences-json") {
        keys(value, setOf("id", "format", "preferences", "key", "authentication", "policy", "exposure"))
        require(value.getString("authentication") == "none" && Regex("[A-Za-z0-9_.-]{1,128}").matches(value.getString("preferences")))
        text(value.getString("key")); sources[id] = value; continue
      }
      if (value.optString("format", "aes-gcm") == "native-records-v1") {
        keys(value, setOf("id", "format", "records", "key", "authentication", "policy", "exposure", "provenance"))
        require(value.getString("authentication") == "none")
        text(value.getString("key")); val records = value.getJSONObject("records")
        keys(records, setOf("namespace", "identity"))
        require(Regex("[a-z][a-z0-9_.-]{0,63}").matches(records.getString("namespace")) && !records.getString("namespace").startsWith("oro."))
        val identity = records.getJSONObject("identity")
        require(identity.keys().asSequence().toSet() == setOf("authorityRef", "accountRef", "identityRef"))
        for (field in identity.keys()) {
          val selector = identity.get(field)
          if (selector is String) { require(!selector.contains("{sourceRef}")); text(expand(selector, "public.application", "public-reference")) } else {
            require(selector is JSONObject); keys(selector, setOf("preferences", "key", "default"))
            require(Regex("[A-Za-z0-9_.-]{1,128}").matches(selector.getString("preferences")))
            text(selector.getString("key")); text(selector.getString("default"))
          }
        }
        sources[id] = value
        continue
      }
      keys(value, setOf("id", "format", "preferences", "key", "alias", "aad", "encoding", "ciphertextField", "authentication", "policy", "exposure"))
      require(value.optString("format", "aes-gcm") == "aes-gcm")
      require(Regex("[A-Za-z0-9_.-]{1,128}").matches(value.getString("preferences")))
      for (field in listOf("key", "alias", "aad")) {
        val template = text(value.getString(field), 1024)
        require(!expand(template, "public.application", "public-reference").contains(Regex("[{}]")))
      }
      require(!value.getString("alias").startsWith("oro."))
      require(value.getString("encoding") in setOf("base64", "base64url"))
      require(Regex("[A-Za-z][A-Za-z0-9_]{0,63}").matches(value.getString("ciphertextField")))
      require(value.getString("authentication") in setOf("none", "biometric", "biometric-or-device"))
      require(value.getString("policy") in setOf("biometric", "biometric-or-device"))
      sources[id] = value
    }
    for (value in sources.values) {
      if (value.has("provenance")) {
        require(value.optString("format") == "native-records-v1")
        val guard = value.getJSONObject("provenance")
        keys(guard, setOf("legacy", "markerKey", "format", "formatPointer", "digestPointer", "digestFormat"))
        val legacy = sources[identifier(guard.getString("legacy"))] ?: throw IllegalArgumentException("Unknown legacy source")
        require(legacy.optString("format", "aes-gcm") == "aes-gcm" && legacy.getString("authentication") == "none")
        val prefix = familyPrefix(legacy, "public.application")
        require(prefix.isNotEmpty())
        require(guard.getString("digestFormat") == "android-json-pairs-sha256-v1")
        require(guard.get("format") is Int && guard.getInt("format") > 0)
        require(Regex("[A-Za-z0-9_.:-]{1,128}").matches(guard.getString("markerKey")))
        require(!guard.getString("markerKey").startsWith(familyPrefix(value, "public.application")))
        for (field in listOf("formatPointer", "digestPointer")) require(Regex("/[^/~]+(?:/[^/~]+)*").matches(text(guard.getString(field))))
        require(guard.getString("formatPointer") != guard.getString("digestPointer"))
      }
      if (value.optString("format") == "prefer-native-records-v1") {
        val primary = sources[value.getString("primary")] ?: throw IllegalArgumentException("Unknown primary source")
        val fallback = sources[value.getString("fallback")] ?: throw IllegalArgumentException("Unknown fallback source")
        require(primary.optString("format") == "native-records-v1" && primary.has("provenance"))
        require(primary.getJSONObject("provenance").getString("legacy") == fallback.getString("id"))
        require(fallback.optString("format", "aes-gcm") == "aes-gcm")
        require(listOf(primary, fallback).all { it.getString("authentication") == "none" && it.getString("policy") == value.getString("policy") })
        familyPrefix(primary, "public.application"); familyPrefix(fallback, "public.application")
      }
    }
    val classes = mutableSetOf<String>()
    val rows = root.getJSONArray("components"); require(rows.length() <= 16)
    for (index in 0 until rows.length()) {
      val value = rows.getJSONObject(index)
      keys(value, setOf("id", "className", "type", "actions", "source", "referenceExtra", "referencePointer",
        "generationExtra", "generationPointer", "tokenExtra", "tokenPointer", "senderPointer", "senderPreferences", "senderKey",
        "identityFields", "checks", "minimumJobId", "maximumJobId", "targetActivity", "referenceExtras"))
      val id = identifier(value.getString("id")); require(!components.containsKey(id))
      val name = value.getString("className")
      require(Regex("[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+").matches(name) && classes.add(name) && !name.startsWith("oro.runtime."))
      val type = value.getString("type"); require(type in setOf("receiver", "job", "service", "activity"))
      if (type == "activity") {
        require(value.getString("targetActivity").isNotEmpty())
        require(value.keys().asSequence().all { it in setOf("id", "type", "className", "targetActivity", "actions", "referenceExtras") })
        if (value.has("actions")) {
          val actions = value.getJSONArray("actions"); require(actions.length() in 1..8)
          for (at in 0 until actions.length()) require(Regex("[A-Za-z][A-Za-z0-9_.]{1,159}").matches(actions.getString(at)))
          val extras = value.getJSONArray("referenceExtras"); require(extras.length() in 1..8)
          for (at in 0 until extras.length()) require(Regex("[A-Za-z][A-Za-z0-9_.]{0,63}").matches(extras.getString(at)))
        }
      } else if (type == "service") {
        require(value.keys().asSequence().all { it in setOf("id", "type", "className", "actions") })
      } else {
        require(sources[value.getString("source")]?.getString("authentication") == "none")
        text(value.getString("referenceExtra")); text(value.getString("referencePointer"))
        if (value.has("generationExtra")) text(value.getString("generationExtra"))
        if (value.has("generationPointer")) text(value.getString("generationPointer"))
        val fields = value.getJSONObject("identityFields")
        require(fields.keys().asSequence().toSet() == setOf("authorityRef", "accountRef", "targetRef"))
        for (field in fields.keys()) {
          val mapping = fields.getJSONObject(field); keys(mapping, setOf("pointer", "encoding"))
          text(mapping.getString("pointer")); require(mapping.getString("encoding") in setOf("text", "sha256"))
        }
        value.optJSONArray("checks")?.let { checks ->
          require(checks.length() <= 8)
          for (at in 0 until checks.length()) {
            val check = checks.getJSONObject(at); keys(check, setOf("pointer", "equals", "default"))
            text(check.getString("pointer")); val expected = check.get("equals")
            require(expected is String || expected is Boolean || expected is Int || expected is Long)
            if (check.has("default")) require(check.get("default") == expected)
          }
        }
        if (type == "job") {
          val first = value.get("minimumJobId"); val last = value.get("maximumJobId")
          require(first is Int && last is Int && first >= 0 && last >= first && last.toLong() - first < 32)
        } else {
          text(value.getString("tokenExtra")); text(value.getString("tokenPointer"))
          require(value.has("senderPointer") != value.has("senderPreferences"))
          if (value.has("senderPreferences")) {
            require(Regex("[A-Za-z0-9_.-]{1,128}").matches(value.getString("senderPreferences"))); text(value.getString("senderKey"))
          } else text(value.getString("senderPointer"))
        }
      }
      if (type == "receiver" || type == "service") {
        val actions = value.getJSONArray("actions"); require(actions.length() in 1..8)
        val seen = mutableSetOf<String>()
        for (at in 0 until actions.length()) require(Regex("[A-Za-z][A-Za-z0-9_.]{1,159}").matches(actions.getString(at)) && seen.add(actions.getString(at)))
      }
      components[id] = value
    }
    return Definitions(sources, components)
  }
}
