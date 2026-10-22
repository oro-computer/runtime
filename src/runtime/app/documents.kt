package oro.runtime.app

import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.CancellationSignal
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Two provider workers, no queued queries, a five-second result deadline and cooperative cancellation. */
internal object ScopedDocuments {
  private val workers = ThreadPoolExecutor(2, 2, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
    { task -> Thread(task, "oro-document-query").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  private val deadlines = ScheduledThreadPoolExecutor(1,
    { task -> Thread(task, "oro-document-deadline").apply { isDaemon = true } }).apply { removeOnCancelPolicy = true }
  private val cancellations = ThreadPoolExecutor(0, 2, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
    { task -> Thread(task, "oro-document-cancel").apply { isDaemon = true } })

  private fun failure (code: String, message: String): String = JSONObject().put("err",
    JSONObject().put("code", code).put("message", message)).toString()

  fun start (app: App, token: Long, action: String, source: String, options: String) {
    val signal = CancellationSignal()
    val completed = AtomicBoolean(false)
    fun finish (value: String) {
      if (completed.compareAndSet(false, true)) app.onDocumentQueryResult(token, value)
    }
    val deadline = deadlines.schedule({
      finish(failure("ETIMEDOUT", "Document provider query timed out"))
      // Provider cancellation may block. It cannot occupy the deadline thread.
      // Two calls can run with no backlog; timed-out results remain final.
      try {
        cancellations.execute { try { signal.cancel() } catch (_: Exception) {} }
      } catch (_: java.util.concurrent.RejectedExecutionException) {}
    }, 5, TimeUnit.SECONDS)
    try {
      workers.execute {
        try {
          val result = query(app, action, source, options, signal)
          val encoded = JSONObject().put("data", result).toString()
          finish(if (encoded.toByteArray(Charsets.UTF_8).size <= 1_048_576) encoded
            else failure("E2BIG", "Document metadata exceeds the result limit"))
        } catch (_: SecurityException) {
          finish(failure("EACCES", "Document access is unavailable or revoked"))
        } catch (_: java.io.FileNotFoundException) {
          finish(failure("ENOENT", "Document is unavailable"))
        } catch (_: android.os.OperationCanceledException) {
          finish(failure("ETIMEDOUT", "Document provider query was cancelled"))
        } catch (_: IllegalArgumentException) {
          finish(failure("EINVAL", "Invalid document query or unsupported provider metadata"))
        } catch (_: Exception) {
          finish(failure("EIO", "Document provider query failed"))
        } finally {
          deadline.cancel(false)
        }
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) {
      deadline.cancel(false)
      finish(failure("EBUSY", "Two document provider queries are already running"))
    }
  }

  private fun text (cursor: Cursor, column: String): String? {
    val index = cursor.getColumnIndex(column)
    if (index < 0 || cursor.isNull(index)) return null
    val value = cursor.getString(index) ?: return null
    require(value.length <= 4096)
    return value
  }

  private fun number (cursor: Cursor, column: String): Any {
    val index = cursor.getColumnIndex(column)
    if (index < 0 || cursor.isNull(index)) return JSONObject.NULL
    val value = cursor.getLong(index)
    return if (value < 0) JSONObject.NULL else value
  }

  private fun row (cursor: Cursor, uri: Uri, mime: String?): JSONObject = JSONObject()
    .put("uri", uri.toString()).put("name", text(cursor, OpenableColumns.DISPLAY_NAME) ?: JSONObject.NULL)
    .put("mimeType", text(cursor, DocumentsContract.Document.COLUMN_MIME_TYPE) ?: mime ?: JSONObject.NULL)
    .put("size", number(cursor, OpenableColumns.SIZE))
    .put("lastModified", number(cursor, DocumentsContract.Document.COLUMN_LAST_MODIFIED))
    .put("flags", number(cursor, DocumentsContract.Document.COLUMN_FLAGS))

  private fun query (context: Context, action: String, source: String, options: String, signal: CancellationSignal): Any {
    signal.throwIfCanceled()
    require(source.length in 1..4096 && source.none { it.code < 32 || it.code == 127 } && options.length <= 4096)
    val uri = Uri.parse(source)
    require(uri.scheme == "content" && !uri.authority.isNullOrBlank())
    val resolver = context.contentResolver
    val tree = DocumentsContract.isTreeUri(uri)
    val document = DocumentsContract.isDocumentUri(context, uri)
    val id = if (tree) {
      if (document) DocumentsContract.getDocumentId(uri) else DocumentsContract.getTreeDocumentId(uri)
    } else null
    val target = if (id != null) DocumentsContract.buildDocumentUriUsingTree(uri, id) else uri
    val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, OpenableColumns.DISPLAY_NAME,
      DocumentsContract.Document.COLUMN_MIME_TYPE, OpenableColumns.SIZE,
      DocumentsContract.Document.COLUMN_LAST_MODIFIED, DocumentsContract.Document.COLUMN_FLAGS)
    if (action == "stat") {
      val projection = if (tree || document) columns else arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
      resolver.query(target, projection, null, null, null, signal)?.use { cursor ->
        if (!cursor.moveToFirst()) throw java.io.FileNotFoundException()
        return row(cursor, target, if (tree || document) null else resolver.getType(target))
      }
      throw java.io.FileNotFoundException()
    }
    require(action == "children" && tree && id != null)
    val parameters = JSONObject(options)
    val offset = parameters.optInt("offset", 0)
    val limit = parameters.optInt("limit", 128)
    require(offset in 0..10_000 && limit in 1..128)
    val children = DocumentsContract.buildChildDocumentsUriUsingTree(uri, id)
    val entries = JSONArray()
    resolver.query(children, columns, null, null, null, signal)?.use { cursor ->
      var more = cursor.moveToPosition(offset)
      while (more && entries.length() < limit) {
        signal.throwIfCanceled()
        val childId = text(cursor, DocumentsContract.Document.COLUMN_DOCUMENT_ID)
          ?: throw IllegalArgumentException()
        val child = DocumentsContract.buildDocumentUriUsingTree(uri, childId)
        require(child.toString().length <= 4096)
        entries.put(row(cursor, child, null))
        more = cursor.moveToNext()
      }
      return JSONObject().put("entries", entries).put("truncated", more)
        .put("nextOffset", if (more && offset + entries.length() <= 10_000) offset + entries.length() else JSONObject.NULL)
    }
    throw java.io.FileNotFoundException()
  }
}
