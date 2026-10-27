package oro.runtime.app

import android.content.Context
import android.database.DatabaseErrorHandler
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteDatabaseCorruptException
import android.database.sqlite.SQLiteDatabaseLockedException
import android.database.sqlite.SQLiteFullException
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.security.KeyStore
import java.security.MessageDigest
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Fixed native error codes; values and SQL/Keystore exception messages are not exported. */
class NativeStateException (val code: String) : Exception(code) {
  init { require(Regex("[A-Z][A-Z0-9_]{0,63}").matches(code)) }
}

/** Opaque application-resolved identity, not proof of server authorization. */
data class NativeRecordIdentity (val authorityRef: String, val accountRef: String, val identityRef: String) {
  init {
    for (value in listOf(authorityRef, accountRef, identityRef)) {
      require(value.isNotEmpty() && value.toByteArray(Charsets.UTF_8).size <= 256 && value.none { it.code < 32 })
    }
  }
}

/** Caller-owned byte copies. Revision zero is an absent scope; revisions never reset. */
data class NativeRecordSnapshot (val revision: Long, val commitId: String?, val records: Map<String, ByteArray>)

data class NativeRecordStatus (val revision: Long, val commitId: String?, val keyPresent: Boolean,
  val requiresRecovery: Boolean, val readability: String = "not-probed")

/** This store has no JavaScript/IPC route and uses a separate Keystore key per identity. */
object NativeRecords {
  internal const val MAX_BYTES = 262144
  internal const val MAX_RECORDS = 128
  internal const val MAX_SCOPES = 256
  private const val PREFIX = "oro.native.records.v1."

  @JvmStatic
  fun open (context: Context, namespace: String, identity: NativeRecordIdentity): NativeRecordStore {
    require(Regex("[a-z][a-z0-9_.-]{0,63}").matches(namespace) && !namespace.startsWith("oro."))
    return NativeRecordStore(context.applicationContext, namespace, identity)
  }

  internal fun runtime (context: Context, namespace: String) = NativeRecordStore(context.applicationContext,
    "oro.$namespace", NativeRecordIdentity("runtime", context.packageName, "v1"))

  internal fun digest (bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes)
    .joinToString("") { "%02x".format(it.toInt() and 255) }

  internal fun binding (values: List<String>): ByteArray {
    val bytes = values.map { it.toByteArray(Charsets.UTF_8) }
    val result = ByteBuffer.allocate(bytes.sumOf { it.size + 4 })
    for (value in bytes) result.putInt(value.size).put(value)
    return result.array()
  }

  internal fun alias (scope: String, key: String) = PREFIX + scope + "." + key
  internal fun key (alias: String): SecretKey? = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    .getKey(alias, null) as? SecretKey

  internal fun createKey (alias: String): SecretKey {
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    if (store.aliases().asSequence().count { it.startsWith(PREFIX) } >= 512) throw NativeStateException("ENOSPC")
    return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
      init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
        .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
    }.generateKey()
  }

  /** Removes only unreferenced retired keys; failed deletions remain journaled for retry. */
  @JvmStatic
  fun collectRetiredKeys (context: Context): Int = database(context).use { db ->
    var count = 0
    db.beginTransaction()
    try {
      val referenced = mutableSetOf<String>()
      db.rawQuery("SELECT scope, key_id FROM scopes", null).use { rows ->
        while (rows.moveToNext()) referenced.add(alias(rows.getString(0), rows.getString(1)))
      }
      val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
      val pending = mutableSetOf<String>()
      db.rawQuery("SELECT alias FROM retired_keys LIMIT 512", null).use { rows ->
        while (rows.moveToNext()) pending.add(rows.getString(0))
      }
      val retired = (pending + store.aliases().asSequence().filter { it.startsWith(PREFIX) && it !in referenced }.take(512).toList()).take(512)
      for (alias in retired) {
        require(alias.startsWith(PREFIX))
        if (alias !in referenced) store.deleteEntry(alias)
        db.execSQL("DELETE FROM retired_keys WHERE alias = ?", arrayOf(alias))
        count++
      }
      db.setTransactionSuccessful()
    } finally { db.endTransaction() }
    count
  }

  internal fun database (context: Context): SQLiteDatabase {
    val path = File(context.noBackupFilesDir, "oro-native-state-v1.db")
    if (!path.parentFile!!.isDirectory && !path.parentFile!!.mkdirs() && !path.parentFile!!.isDirectory) throw NativeStateException("EIO")
    val db = SQLiteDatabase.openDatabase(path.path, null,
      SQLiteDatabase.CREATE_IF_NECESSARY or SQLiteDatabase.NO_LOCALIZED_COLLATORS,
      DatabaseErrorHandler { throw NativeStateException("EBADMSG") })
    try {
      db.rawQuery("PRAGMA busy_timeout=5000", null).use { it.moveToFirst() }
      db.execSQL("PRAGMA synchronous=FULL")
      db.beginTransaction()
      try {
        val version = db.version
        if (version !in 0..1) throw NativeStateException("ENOTSUP")
        db.execSQL("CREATE TABLE IF NOT EXISTS scopes (scope TEXT PRIMARY KEY, revision INTEGER NOT NULL, " +
          "key_id TEXT NOT NULL, commit_id TEXT NOT NULL, envelope BLOB NOT NULL)")
        db.execSQL("CREATE TABLE IF NOT EXISTS retired_keys (alias TEXT PRIMARY KEY)")
        if (version == 0) db.version = 1
        db.setTransactionSuccessful()
      } finally { db.endTransaction() }
      return db
    } catch (error: Exception) { db.close(); throw error }
  }
}

/** Blocking native I/O. Use a worker; callbacks run inside one local SQLite transaction. */
class NativeRecordStore internal constructor (private val context: Context, namespace: String, identity: NativeRecordIdentity) {
  private val associated = NativeRecords.binding(listOf(context.packageName, namespace,
    identity.authorityRef, identity.accountRef, identity.identityRef))
  internal val scope = NativeRecords.digest(associated)
  private data class Row (val revision: Long, val keyId: String, val commitId: String, val envelope: ByteArray)

  private fun row (db: SQLiteDatabase): Row? = db.rawQuery(
    "SELECT revision, key_id, commit_id, envelope FROM scopes WHERE scope = ?", arrayOf(scope)).use {
    if (!it.moveToFirst()) null else Row(it.getLong(0), it.getString(1), it.getString(2), it.getBlob(3))
  }

  private fun decode (value: Row?): MutableMap<String, ByteArray> {
    if (value == null) return linkedMapOf()
    val key = NativeRecords.key(NativeRecords.alias(scope, value.keyId)) ?: throw NativeStateException("EKEYREVOKED")
    if (value.envelope.size !in 28..(NativeRecords.MAX_BYTES * 2)) throw NativeStateException("EBADMSG")
    try {
      val cipher = Cipher.getInstance("AES/GCM/NoPadding")
      cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, value.envelope.copyOfRange(0, 12)))
      cipher.updateAAD(NativeRecords.binding(listOf(scope, value.keyId, value.revision.toString(), value.commitId)))
      val plain = cipher.doFinal(value.envelope, 12, value.envelope.size - 12)
      try {
        val json = JSONObject(String(plain, Charsets.UTF_8))
        val result = linkedMapOf<String, ByteArray>()
        for (name in json.keys()) result[name] = android.util.Base64.decode(json.getString(name), android.util.Base64.NO_WRAP)
        validate(result)
        return result
      } finally { plain.fill(0) }
    } catch (error: NativeStateException) { throw error
    } catch (_: Exception) { throw NativeStateException("EBADMSG") }
  }

  private fun encode (values: Map<String, ByteArray>, key: SecretKey, keyId: String, revision: Long, commit: String): ByteArray {
    validate(values)
    val json = JSONObject()
    for ((name, value) in values.toSortedMap()) json.put(name, android.util.Base64.encodeToString(value, android.util.Base64.NO_WRAP))
    val plain = json.toString().toByteArray(Charsets.UTF_8)
    try {
      val cipher = Cipher.getInstance("AES/GCM/NoPadding")
      cipher.init(Cipher.ENCRYPT_MODE, key)
      cipher.updateAAD(NativeRecords.binding(listOf(scope, keyId, revision.toString(), commit)))
      return cipher.iv + cipher.doFinal(plain)
    } finally { plain.fill(0) }
  }

  private fun validate (values: Map<String, ByteArray>) {
    if (values.size > NativeRecords.MAX_RECORDS + 2 || values.values.any { it.size > 131072 } ||
        values.entries.sumOf { it.key.toByteArray(Charsets.UTF_8).size + it.value.size } > NativeRecords.MAX_BYTES) {
      throw NativeStateException("ENOSPC")
    }
    require(values.keys.all { it in listOf("@coordination", "@migrations") || validKey(it) })
    if (!values.filterKeys { !it.startsWith("@") }.let {
      it.size <= NativeRecords.MAX_RECORDS && it.values.all { bytes -> bytes.size <= 65536 }
    }) throw NativeStateException("ENOSPC")
  }

  private fun validKey (key: String) = Regex("[a-zA-Z0-9_.:-]{1,128}").matches(key)
  internal fun changes (values: MutableMap<String, ByteArray>, updates: Map<String, ByteArray?>) {
    require(updates.keys.all { validKey(it) })
    for ((key, bytes) in updates) {
      if (bytes == null) values.remove(key)?.fill(0)
      else values.put(key, bytes.copyOf())?.fill(0)
    }
    validate(values)
  }

  private fun snapshot (row: Row?, values: Map<String, ByteArray>) = NativeRecordSnapshot(row?.revision ?: 0,
    row?.commitId, values.filterKeys { !it.startsWith("@") }.mapValues { it.value.copyOf() })

  /** An absent record never creates a key. Locked/corrupt data is preserved and reported. */
  fun read (): NativeRecordSnapshot = access { db ->
    val row = row(db)
    val values = decode(row)
    try { snapshot(row, values) } finally { values.values.forEach { it.fill(0) } }
  }

  /** Migration binds a read to its original wrapping-key epoch without exporting key material. */
  internal fun migrationSource (): Pair<String, NativeRecordSnapshot> = access { db ->
    val row = row(db) ?: throw NativeStateException("ENOENT")
    val values = decode(row)
    try { row.keyId to snapshot(row, values) } finally { values.values.forEach { it.fill(0) } }
  }
  internal fun migrationEpoch (): String = access { db -> row(db)?.keyId ?: throw NativeStateException("ENOENT") }

  /** Reads recovery metadata without decrypting, generating a key or erasing ciphertext. */
  fun status (): NativeRecordStatus = access { db ->
    val row = row(db)
    val present = row != null && NativeRecords.key(NativeRecords.alias(scope, row.keyId)) != null
    NativeRecordStatus(row?.revision ?: 0, row?.commitId, present, row != null && !present)
  }

  /** CAS update; an empty map still advances the revision, including an empty scope. */
  fun apply (expectedRevision: Long, updates: Map<String, ByteArray?>, commitId: String = UUID.randomUUID().toString()): NativeRecordSnapshot =
    mutate(expectedRevision, commitId) { values -> changes(values, updates) }

  /** Atomic import and digest marker. Never deletes or rewrites the legacy source. */
  fun migrate (migrationId: String, sourceSha256: String, expectedRevision: Long,
    updates: Map<String, ByteArray>): NativeRecordSnapshot {
    require(validKey(migrationId) && Regex("[a-f0-9]{64}").matches(sourceSha256))
    return mutate(expectedRevision, UUID.randomUUID().toString()) { values ->
      val markers = JSONObject(values["@migrations"]?.toString(Charsets.UTF_8) ?: "{}")
      if (markers.has(migrationId)) {
        if (markers.getString(migrationId) != sourceSha256) throw NativeStateException("EEXIST")
      } else {
        if (markers.length() >= 32) throw NativeStateException("ENOSPC")
        for ((key, bytes) in updates) {
          if (values[key]?.contentEquals(bytes) == false) throw NativeStateException("EEXIST")
        }
        changes(values, updates)
        markers.put(migrationId, sourceSha256)
        values["@migrations"] = markers.toString().toByteArray(Charsets.UTF_8)
      }
    }
  }

  /** Explicit destructive recovery for this identity only; never automatic on read failure. */
  fun reset (expectedRevision: Long): NativeRecordSnapshot = mutate(expectedRevision, UUID.randomUUID().toString(), true) { it.clear() }

  internal fun inspectInternal (): Pair<Long, Map<String, ByteArray>> = access { db ->
    val row = row(db)
    (row?.revision ?: 0) to decode(row)
  }

  internal fun mutate (expectedRevision: Long, commitId: String = UUID.randomUUID().toString(), reset: Boolean = false,
    change: (MutableMap<String, ByteArray>) -> Unit): NativeRecordSnapshot = access { db ->
    require(expectedRevision >= 0 && Regex("[a-zA-Z0-9_-]{1,128}").matches(commitId))
    val previous = row(db)
    if ((previous?.revision ?: 0) != expectedRevision) throw NativeStateException("ESTALE")
    if (expectedRevision == Long.MAX_VALUE) throw NativeStateException("EOVERFLOW")
    if (previous == null) db.rawQuery("SELECT count(*) FROM scopes", null).use {
      it.moveToFirst(); if (it.getInt(0) >= NativeRecords.MAX_SCOPES) throw NativeStateException("ENOSPC")
    }
    val values = if (reset) linkedMapOf() else decode(previous)
    try {
      change(values)
      validate(values)
      val keyId = if (previous == null || reset) UUID.randomUUID().toString() else previous.keyId
      val alias = NativeRecords.alias(scope, keyId)
      val newKey = previous == null || reset
      if (previous != null && reset) db.rawQuery("SELECT count(*) FROM retired_keys", null).use {
        it.moveToFirst(); if (it.getInt(0) >= 512) throw NativeStateException("ENOSPC")
      }
      // Failed/uncertain commits can leave an unreferenced key. Explicit GC holds
      // the SQLite write lock while comparing all live references before deletion.
      val key = if (newKey) NativeRecords.createKey(alias) else NativeRecords.key(alias) ?: throw NativeStateException("EKEYREVOKED")
      val next = Row(expectedRevision + 1, keyId, commitId, encode(values, key, keyId, expectedRevision + 1, commitId))
      if (previous != null && reset) db.execSQL("INSERT OR IGNORE INTO retired_keys(alias) VALUES (?)",
        arrayOf(NativeRecords.alias(scope, previous.keyId)))
      db.execSQL("INSERT OR REPLACE INTO scopes(scope, revision, key_id, commit_id, envelope) VALUES (?, ?, ?, ?, ?)",
        arrayOf(scope, next.revision, next.keyId, next.commitId, next.envelope))
      snapshot(next, values)
    } finally { values.values.forEach { it.fill(0) } }
  }

  private fun <T> access (action: (SQLiteDatabase) -> T): T {
    try {
      return NativeRecords.database(context).use { db ->
        db.beginTransaction()
        var success = false
        try {
          val value = action(db)
          db.setTransactionSuccessful()
          success = true
          value
        } finally {
          try { db.endTransaction() } catch (_: Exception) { throw NativeStateException(if (success) "EUNKNOWN" else "EIO") }
        }
      }
    } catch (error: NativeStateException) { throw error
    } catch (_: SQLiteDatabaseLockedException) { throw NativeStateException("EBUSY")
    } catch (_: SQLiteDatabaseCorruptException) { throw NativeStateException("EBADMSG")
    } catch (_: SQLiteFullException) { throw NativeStateException("ENOSPC")
    } catch (_: IllegalArgumentException) { throw NativeStateException("EINVAL")
    } catch (_: Exception) { throw NativeStateException("EIO") }
  }
}
