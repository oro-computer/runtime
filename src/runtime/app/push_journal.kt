package oro.runtime.app

import android.content.Context
import androidx.annotation.Keep
import org.json.JSONObject
import org.xmlpull.v1.XmlPullParser
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.io.File

/** Native installation recovery only. The selector/archives never contain decrypted transport values. */
@Keep
internal object NativePushJournal {
  private const val LIMIT = 4 * 1024 * 1024
  private lateinit var application: Context
  private var uncertain: String? = null
  private external fun control (options: String, ciphertext: ByteArray): String

  private fun bind (context: Context) {
    val value = context.applicationContext
    if (::application.isInitialized) check(application === value) else application = value
    if (JSONObject(App.getInstance().getNativeBuildIdentity()).optInt("nativePushJournalRecoveryProtocol") != 1)
      throw PlatformFailure("ENOTREADY")
  }
  private fun directory (): String = File(application.noBackupFilesDir, "oro.push.journal").absolutePath
  private fun applicationId (): String = application.packageName
  private fun call (options: JSONObject, ciphertext: ByteArray = byteArrayOf()): JSONObject {
    val result = try { JSONObject(control(options.toString(), ciphertext)) } finally { ciphertext.fill(0) }
    result.optString("error").takeIf { it.isNotEmpty() }?.let { throw PlatformFailure(it) }
    return result.getJSONObject("data")
  }
  fun epoch (context: Context): String {
    bind(context)
    return call(JSONObject().put("action", "epoch")).getString("epoch")
  }
  fun name (epoch: String): String = if (epoch.isEmpty()) "oro.push.v1" else "oro.push.v1.$epoch"
  fun alias (context: Context): String = alias(epoch(context))
  private fun alias (epoch: String): String = if (epoch.isEmpty()) "oro.push.storage.v1" else "oro.push.storage.v1.$epoch"

  private fun bytes (file: File): ByteArray {
    if (!file.exists()) return byteArrayOf()
    if (file.length() > LIMIT) throw PlatformFailure("ENOSPC")
    return file.inputStream().use { input ->
      val output = ByteArrayOutputStream()
      val buffer = ByteArray(8192)
      while (true) {
        val count = input.read(buffer)
        if (count < 0) break
        if (output.size() + count > LIMIT) throw PlatformFailure("ENOSPC")
        output.write(buffer, 0, count)
      }
      output.toByteArray()
    }
  }
  private fun files (context: Context, epoch: String): List<File> {
    val file = File(File(context.applicationInfo.dataDir, "shared_prefs"), name(epoch) + ".xml")
    return listOf(file, File(file.path + ".bak"))
  }
  /** Parse independently of SharedPreferences so inspecting malformed XML cannot restore/overwrite its originals. */
  private fun values (bytes: ByteArray): Map<String, String> {
    if (bytes.isEmpty()) return emptyMap()
    val parser = android.util.Xml.newPullParser()
    try { parser.setFeature(XmlPullParser.FEATURE_PROCESS_DOCDECL, false) }
    catch (_: org.xmlpull.v1.XmlPullParserException) { require(!parser.getFeature(XmlPullParser.FEATURE_PROCESS_DOCDECL)) }
    parser.setInput(ByteArrayInputStream(bytes), "UTF-8")
    val strings = mutableMapOf<String, String>()
    var root = false
    var closed = false
    var entries = 0
    while (true) {
      val event = parser.nextToken()
      if (event == XmlPullParser.DOCDECL) throw IllegalArgumentException()
      if (event == XmlPullParser.END_DOCUMENT) break
      if (event == XmlPullParser.START_TAG) {
        if (parser.depth == 1) {
          require(!root && parser.name == "map")
          root = true
        } else if (parser.depth == 2) {
          require(++entries <= 1024)
          val key = parser.getAttributeValue(null, "name") ?: throw IllegalArgumentException()
          require(key.length <= 256)
          if (parser.name == "string") {
            require(!strings.containsKey(key))
            strings[key] = parser.nextText()
          } else {
            require(!key.startsWith("registration.") && !key.startsWith("event."))
            require(parser.name in setOf("long", "int", "float", "boolean", "set"))
          }
        }
      }
      if (event == XmlPullParser.END_TAG && parser.depth == 1) closed = true
    }
    require(root && closed)
    return strings
  }
  fun preferences (context: Context): android.content.SharedPreferences {
    val epoch = epoch(context)
    if (uncertain == epoch) throw PlatformFailure("EUNKNOWN")
    for (file in files(context, epoch)) if (file.exists()) {
      try { require(file.length() > 0); values(bytes(file)) }
      catch (error: PlatformFailure) { throw error }
      catch (_: Exception) { throw PlatformFailure("EBADMSG") }
    }
    return context.getSharedPreferences(name(epoch), Context.MODE_PRIVATE)
  }
  fun commit (context: Context, editor: android.content.SharedPreferences.Editor) {
    val epoch = epoch(context)
    preferences(context)
    if (!editor.commit()) {
      uncertain = epoch
      throw PlatformFailure("EUNKNOWN")
    }
  }
  private data class Snapshot (val epoch: String, val reason: String, val ciphertext: ByteArray)
  private fun snapshot (context: Context): Snapshot {
    val epoch = epoch(context)
    val output = ByteArrayOutputStream()
    val data = DataOutputStream(output)
    data.writeInt(1)
    var reason = if (uncertain == epoch) "uncertain-write" else ""
    val encrypted = mutableMapOf<String, String>()
    for (file in files(context, epoch)) {
      val raw = bytes(file)
      data.writeBoolean(file.exists())
      data.writeInt(raw.size)
      data.write(raw)
      if (output.size() > LIMIT) throw PlatformFailure("ENOSPC")
      if (file.exists()) try {
        require(raw.isNotEmpty())
        val parsed = values(raw)
        encrypted.putAll(parsed.filterKeys { it.startsWith("registration.") || it.startsWith("event.") })
      } catch (_: Exception) { reason = "journal-format" }
    }
    if (uncertain == epoch) {
      // A failed commit may already have changed the cached map. Preserve that ciphertext separately too.
      val memory = context.getSharedPreferences(name(epoch), Context.MODE_PRIVATE).all.toSortedMap()
      val serialized = JSONObject(memory).toString().toByteArray(Charsets.UTF_8)
      data.writeInt(serialized.size)
      data.write(serialized)
    } else data.writeInt(0)
    if (output.size() > LIMIT) throw PlatformFailure("ENOSPC")
    if (reason.isEmpty() && encrypted.isNotEmpty()) {
      try {
        if (!PushCryptography.hasStorageKey(alias(epoch))) reason = "missing-key"
        else for ((key, value) in encrypted) PushCryptography.open(value, key, alias(epoch))
      } catch (_: android.security.keystore.KeyPermanentlyInvalidatedException) { reason = "invalidated-key"
      } catch (_: javax.crypto.AEADBadTagException) { reason = "ciphertext-corrupt"
      } catch (_: javax.crypto.BadPaddingException) { reason = "ciphertext-corrupt"
      } catch (_: org.json.JSONException) { reason = "ciphertext-corrupt"
      } catch (_: IllegalArgumentException) { reason = "ciphertext-corrupt"
      } catch (error: PlatformFailure) { reason = if (error.code == "EKEYREVOKED") "missing-key" else "provider-unavailable"
      } catch (_: Exception) { reason = "provider-unavailable" }
    }
    return Snapshot(epoch, reason, output.toByteArray())
  }
  fun inspect (context: Context): JSONObject {
    val snapshot = snapshot(context)
    return call(JSONObject().put("action", "inspect").put("epoch", snapshot.epoch).put("reason", snapshot.reason), snapshot.ciphertext)
  }
  fun quarantine (context: Context, options: JSONObject, idle: Boolean): JSONObject {
    if (!options.optBoolean("installationScopeConfirmed") || !options.optBoolean("archiveAndStopLocalDeliveryConfirmed"))
      throw PlatformFailure("EACCES")
    val snapshot = snapshot(context)
    val result = call(JSONObject(options.toString()).put("action", "quarantine").put("epoch", snapshot.epoch)
      .put("reason", snapshot.reason).put("idle", idle), snapshot.ciphertext)
    if (result.getBoolean("changed")) uncertain = null
    return result
  }
}
