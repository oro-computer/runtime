package oro.runtime.window

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileNotFoundException
import java.util.UUID
import org.json.JSONArray

/** Reject expired or writable grants even when Android has not pruned the cache. */
class ShareFileProvider : FileProvider() {
  override fun openFile (uri: Uri, mode: String): android.os.ParcelFileDescriptor {
    val segments = uri.pathSegments
    val root = File(requireNotNull(context).cacheDir, "oro-share").canonicalFile
    if (mode != "r" || segments.size != 3 || segments[0] != "shares" ||
        !Regex("[a-f0-9-]{36}").matches(segments[1])) throw FileNotFoundException("Share unavailable")
    val directory = File(root, segments[1]).canonicalFile
    val file = File(directory, segments[2]).canonicalFile
    if (directory.parentFile != root || file.parentFile != directory ||
        !ShareFiles.isFresh(directory)) {
      throw FileNotFoundException("Share expired")
    }
    return super.openFile(uri, mode) ?: throw FileNotFoundException("Share unavailable")
  }
}

/** Only caller-supplied bytes enter this dedicated, bounded share directory. */
internal object ShareFiles {
  private const val MAX_BYTES = 8 * 1024 * 1024
  private const val CACHE_BYTES = 64 * 1024 * 1024L
  private const val CACHE_SHARES = 128
  private const val CACHE_FILES = 512
  private const val TTL = 24 * 60 * 60 * 1000L
  private const val CREATED = ".oro-share-created"

  fun isFresh (directory: File): Boolean {
    val marker = File(directory, CREATED)
    return marker.isFile && marker.length() == 0L && System.currentTimeMillis() - marker.lastModified() in 0..TTL
  }

  /** The immutable marker prevents deletion from renewing a directory's expiry. */
  @Synchronized
  fun prune (context: Context, cancelled: () -> Boolean = { false }): org.json.JSONObject {
    val root = File(context.cacheDir, "oro-share").canonicalFile
    var retained = 0L
    var shares = 0
    var files = 0
    var removed = 0
    for (directory in root.listFiles().orEmpty()) {
      if (cancelled()) throw java.io.InterruptedIOException()
      if (!directory.isDirectory || !Regex("[a-f0-9-]{36}").matches(directory.name)) continue
      require(directory.canonicalFile.parentFile == root)
      val entries = directory.listFiles().orEmpty()
      require(entries.size <= 17 && entries.all { it.isFile && it.canonicalFile.parentFile == directory.canonicalFile })
      if (!isFresh(directory)) {
        for (entry in entries.filter { it.name != CREATED }) {
          if (cancelled()) throw java.io.InterruptedIOException()
          val uri = FileProvider.getUriForFile(context, context.packageName + ".oro.share", entry)
          context.revokeUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
          if (!entry.delete()) throw java.io.IOException("Expired share removal failed")
          removed++
        }
        val marker = File(directory, CREATED)
        if (marker.exists() && !marker.delete()) throw java.io.IOException("Share marker removal failed")
        if (!directory.delete()) throw java.io.IOException("Expired share directory removal failed")
      } else {
        retained += entries.sumOf { it.length() }
        shares++
        files += entries.count { it.name != CREATED }
      }
    }
    return org.json.JSONObject().put("retainedBytes", retained).put("retainedShares", shares)
      .put("retainedFiles", files).put("removedFiles", removed)
  }

  @Synchronized
  fun attach (context: Context, intent: Intent, metadata: String, bytes: ByteArray) {
    require(metadata.length <= 32768 && bytes.size <= MAX_BYTES)
    val files = JSONArray(metadata)
    require(files.length() in 1..16)
    var offset = 0
    val names = mutableSetOf<String>()
    val types = mutableSetOf<String>()
    for (index in 0 until files.length()) {
      val file = files.getJSONObject(index)
      val name = file.getString("name")
      val type = file.getString("type")
      val size = file.getLong("size")
      require(name.length in 1..255 && name != "." && name != ".." && name != CREATED &&
        name.none { it == '/' || it == '\\' || it.code < 32 || it.code == 127 })
      val encodedName = name.toByteArray(Charsets.UTF_8)
      require(encodedName.size <= 255 && String(encodedName, Charsets.UTF_8) == name)
      require(names.add(name)) { "Share file names must be distinct" }
      require(type.length <= 255 && Regex("[a-zA-Z0-9!#$&^_.+-]+/[a-zA-Z0-9!#$&^_.+-]+").matches(type))
      require(file.getLong("offset") == offset.toLong() && size >= 0 && size <= bytes.size - offset)
      offset += size.toInt()
      types.add(type)
    }
    require(offset == bytes.size)

    val root = File(context.cacheDir, "oro-share")
    require(root.isDirectory || root.mkdirs()) { "Share cache is unavailable" }
    val retained = prune(context)
    require(retained.getLong("retainedBytes") + bytes.size <= CACHE_BYTES) { "Share cache is full; retry after older shares expire" }
    require(retained.getInt("retainedShares") < CACHE_SHARES && retained.getInt("retainedFiles") + files.length() <= CACHE_FILES) {
      "Share cache entry limit reached; retry after older shares expire"
    }
    require(root.usableSpace >= bytes.size + 1024 * 1024L) { "Insufficient space for sharing" }
    val directory = File(root, UUID.randomUUID().toString())
    require(directory.mkdir())
    require(File(directory, CREATED).createNewFile())
    val uris = arrayListOf<Uri>()
    try {
      for (index in 0 until files.length()) {
        val descriptor = files.getJSONObject(index)
        val file = File(directory, descriptor.getString("name"))
        require(file.canonicalFile.parentFile == directory.canonicalFile)
        file.outputStream().use { stream ->
          stream.write(bytes, descriptor.getInt("offset"), descriptor.getInt("size"))
        }
        uris.add(FileProvider.getUriForFile(context, context.packageName + ".oro.share", file))
      }
    } catch (error: Exception) {
      directory.listFiles().orEmpty().forEach { it.delete() }
      directory.delete()
      throw error
    }
    intent.type = types.singleOrNull() ?: "*/*"
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    intent.clipData = ClipData.newRawUri("Shared files", uris.first()).apply {
      uris.drop(1).forEach { addItem(ClipData.Item(it)) }
    }
    if (uris.size == 1) {
      intent.action = Intent.ACTION_SEND
      intent.putExtra(Intent.EXTRA_STREAM, uris.first())
    } else {
      intent.action = Intent.ACTION_SEND_MULTIPLE
      intent.putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
    }
  }
}
