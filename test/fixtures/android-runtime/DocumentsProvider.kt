package oro.runtime.qualification

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsProvider
import java.io.File
import java.io.FileNotFoundException

/** Qualification-only provider: fixed public fixture bytes, never packaged in production. */
class Documents : DocumentsProvider() {
  private val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID,
    DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
    DocumentsContract.Document.COLUMN_SIZE, DocumentsContract.Document.COLUMN_LAST_MODIFIED, DocumentsContract.Document.COLUMN_FLAGS)
  override fun onCreate (): Boolean = true
  override fun queryRoots (projection: Array<out String>?): Cursor {
    val fields = projection ?: arrayOf(DocumentsContract.Root.COLUMN_ROOT_ID, DocumentsContract.Root.COLUMN_DOCUMENT_ID,
      DocumentsContract.Root.COLUMN_TITLE, DocumentsContract.Root.COLUMN_FLAGS)
    return MatrixCursor(fields).apply {
      val values = mapOf<String, Any>(DocumentsContract.Root.COLUMN_ROOT_ID to "public",
        DocumentsContract.Root.COLUMN_DOCUMENT_ID to "root", DocumentsContract.Root.COLUMN_TITLE to "Public runtime fixtures",
        DocumentsContract.Root.COLUMN_FLAGS to (DocumentsContract.Root.FLAG_LOCAL_ONLY or DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD))
      addRow(fields.map { values[it] }.toTypedArray())
    }
  }
  private fun add (cursor: MatrixCursor, id: String) {
    if (id !in listOf("root", "full", "unknown", "locked", "unicode")) throw FileNotFoundException()
    val values = mapOf<String, Any?>(DocumentsContract.Document.COLUMN_DOCUMENT_ID to id,
      DocumentsContract.Document.COLUMN_DISPLAY_NAME to if (id == "root") "Public fixtures" else if (id == "unicode") "Public-🙂.txt" else "$id.txt",
      DocumentsContract.Document.COLUMN_MIME_TYPE to if (id == "root") DocumentsContract.Document.MIME_TYPE_DIR else "text/plain",
      DocumentsContract.Document.COLUMN_SIZE to if (id == "unknown" || id == "root") null else file(id).length(),
      DocumentsContract.Document.COLUMN_LAST_MODIFIED to 0L,
      DocumentsContract.Document.COLUMN_FLAGS to if (id == "locked" || id == "root") 0 else DocumentsContract.Document.FLAG_SUPPORTS_WRITE)
    cursor.addRow(cursor.columnNames.map { values[it] }.toTypedArray())
  }
  override fun queryDocument (documentId: String, projection: Array<out String>?): Cursor =
    MatrixCursor(projection ?: columns).apply { add(this, documentId) }
  override fun queryChildDocuments (parentDocumentId: String, projection: Array<out String>?, sortOrder: String?): Cursor {
    if (parentDocumentId != "root") throw FileNotFoundException()
    return MatrixCursor(projection ?: columns).apply { for (id in listOf("full", "unknown", "locked", "unicode")) add(this, id) }
  }
  override fun isChildDocument (parentDocumentId: String, documentId: String): Boolean =
    parentDocumentId == "root" && documentId in listOf("full", "unknown", "locked", "unicode")
  private fun file (id: String): File {
    if (id !in listOf("full", "unknown", "locked", "unicode")) throw FileNotFoundException()
    val file = File(requireNotNull(context).cacheDir, "qualification-$id.txt")
    if (!file.exists()) file.writeText("public fixture")
    return file
  }
  override fun openDocument (documentId: String, mode: String, signal: CancellationSignal?): ParcelFileDescriptor {
    if (documentId == "locked" && mode != "r") throw SecurityException("Read-only public fixture")
    return ParcelFileDescriptor.open(file(documentId), ParcelFileDescriptor.parseMode(mode))
  }
}

/** Read-only bounded asset section and unknown-length pipe for manual I/O qualification. */
class Slices : ContentProvider() {
  override fun onCreate (): Boolean = true
  override fun getType (uri: Uri): String = "text/plain"
  override fun query (uri: Uri, projection: Array<out String>?, selection: String?, arguments: Array<out String>?, sort: String?): Cursor =
    MatrixCursor(arrayOf("_display_name", "_size")).apply { addRow(arrayOf("public.txt", null)) }
  override fun query (uri: Uri, projection: Array<out String>?, selection: String?, arguments: Array<out String>?, sort: String?, signal: CancellationSignal?): Cursor {
    if (uri.lastPathSegment == "blocked-cancel") {
      // Fixed delays qualify timeout isolation using public metadata only.
      signal?.setOnCancelListener { Thread.sleep(7000) }
      Thread.sleep(8000)
    }
    return query(uri, projection, selection, arguments, sort)
  }
  override fun insert (uri: Uri, values: ContentValues?): Uri? = throw UnsupportedOperationException()
  override fun update (uri: Uri, values: ContentValues?, selection: String?, arguments: Array<out String>?): Int = throw UnsupportedOperationException()
  override fun delete (uri: Uri, selection: String?, arguments: Array<out String>?): Int = throw UnsupportedOperationException()
  override fun openAssetFile (uri: Uri, mode: String): android.content.res.AssetFileDescriptor {
    if (mode != "r") throw SecurityException("Read-only public fixture")
    if (uri.lastPathSegment == "pipe") {
      val pipe = ParcelFileDescriptor.createPipe()
      Thread {
        ParcelFileDescriptor.AutoCloseOutputStream(pipe[1]).use { it.write("stream".toByteArray()) }
      }.apply { isDaemon = true }.start()
      return android.content.res.AssetFileDescriptor(pipe[0], 0, android.content.res.AssetFileDescriptor.UNKNOWN_LENGTH)
    }
    if (uri.lastPathSegment != "section") throw FileNotFoundException()
    val file = File(requireNotNull(context).cacheDir, "qualification-section.txt").apply { writeText("HEADbodyTAIL") }
    return android.content.res.AssetFileDescriptor(ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY), 4, 4)
  }
}
