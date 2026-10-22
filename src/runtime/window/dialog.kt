package oro.runtime.window

import java.lang.Runtime

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import android.content.Intent
import android.net.Uri
import android.webkit.WebChromeClient

import androidx.activity.result.contract.ActivityResultContracts

import oro.runtime.debug.console
import oro.runtime.window.WindowManagerActivity

/**
 * XXX
 */
open class Dialog (val activity: WindowManagerActivity) {
  /**
 * XXX
   */
  open class FileSystemPickerOptions (
    val params: WebChromeClient.FileChooserParams? = null,
    val mimeTypes: MutableList<String> = mutableListOf<String>(),
    val directories: Boolean = false,
    val multiple: Boolean = false,
    val files: Boolean = true,
    val owner: Window? = null,
    val save: Boolean = false,
    val defaultName: String = ""
  ) {
    init {
      if (params != null && params.acceptTypes.size > 0) {
        this.mimeTypes += params.acceptTypes
      }
    }
  }

  var callback: ((results: Array<Uri>) -> Unit)? = null
  private var pickerActive = false
  private var pickerOwner: Window? = null
  var sharePointer: Long = 0
  private var shareActive = false
  private var shareReceiver: BroadcastReceiver? = null
  private var shareIntent: PendingIntent? = null

  private fun finishShare (success: Boolean, error: String? = null) {
    val pointer = sharePointer
    sharePointer = 0
    shareReceiver?.let { activity.unregisterReceiver(it) }
    shareReceiver = null
    shareIntent?.cancel()
    shareIntent = null
    if (pointer != 0L) onShareResult(pointer, success, error)
  }

  fun close () {
    finishShare(false, "Sharing interrupted by activity destruction")
    callback?.invoke(emptyArray())
    callback = null
    pickerOwner = null
  }

  val launcherForSingleItem = activity.registerForActivityResult(
    ActivityResultContracts.GetContent(),
    fun (uri: Uri?) { this.resolve(uri) }
  )

  val launcherForMulitpleItems = activity.registerForActivityResult(
    ActivityResultContracts.GetMultipleContents(),
    { uris -> this.resolve(uris) }
  )

  val launcherForSingleDocument = activity.registerForActivityResult(
    ActivityResultContracts.OpenDocument(),
    { uri -> this.resolve(uri) }
  )

  val launcherForMulitpleDocuments = activity.registerForActivityResult(
    ActivityResultContracts.OpenMultipleDocuments(),
    { uris -> this.resolve(uris) }
  )

  val launcherForDirectory = activity.registerForActivityResult(
    ActivityResultContracts.OpenDocumentTree(),
    { uri -> this.resolve(uri) }
  )

  val launcherForSaveDocument = activity.registerForActivityResult(
    ActivityResultContracts.StartActivityForResult(),
    { result -> this.resolve(if (result.resultCode == Activity.RESULT_OK) result.data?.data else null) }
  )

  // XXX(@jwerle): unused at the moment
  val launcherForSingleVisualMedia = activity.registerForActivityResult(
    ActivityResultContracts.PickVisualMedia(),
    { uri -> this.resolve(uri) }
  )

  // XXX(@jwerle): unused at the moment
  val launcherForMultipleVisualMedia = activity.registerForActivityResult(
    ActivityResultContracts.PickMultipleVisualMedia(),
    { uris -> this.resolve(uris) }
  )

  val launcherForShare = activity.registerForActivityResult(
    ActivityResultContracts.StartActivityForResult(),
    { _ ->
      shareActive = false
      // Target selection is reported separately; resultCode is not delivery evidence.
      val pointer = sharePointer
      activity.window.decorView.post { if (sharePointer == pointer) finishShare(false) }
    }
  )

  fun resolve (uri: Uri?) {
    if (uri != null) {
      return this.resolve(arrayOf(uri))
    }

    return this.resolve(arrayOf<Uri>())
  }

  fun resolve (uris: List<Uri>) {
    this.resolve(Array<Uri>(uris.size, { i -> uris[i] }))
  }

  fun resolve (uris: Array<Uri>) {
    val callback = this.callback
    this.pickerActive = false
    this.pickerOwner = null

    if (callback != null) {
      this.callback = null
      callback(uris)
    }
  }

  fun cancelFileSystemPicker (owner: Window) {
    if (this.pickerOwner !== owner) return
    val callback = this.callback
    this.callback = null
    this.pickerOwner = null
    // Keep the launcher reserved until its outstanding platform result arrives.
    callback?.invoke(emptyArray())
  }

  fun showFileSystemPicker (
    options: FileSystemPickerOptions,
    callback: ((Array<Uri>) -> Unit)? = null
  ) {
    this.activity.runOnUiThread {
      val owner = options.owner
      if (activity.isFinishing || activity.isDestroyed || (owner != null &&
          (!owner.userMessageHandler.active || owner.fragment.window !== owner || !owner.fragment.hasWebView))) {
        callback?.invoke(emptyArray())
        return@runOnUiThread
      }
      // A result belongs to exactly one picker. Do not replace an active request.
      if (this.pickerActive) {
        callback?.invoke(emptyArray())
        return@runOnUiThread
      }

      this.callback = callback
      this.pickerActive = true
      this.pickerOwner = options.owner
      val mimeTypes = options.mimeTypes.filter { it.isNotBlank() }
        .ifEmpty { listOf("*/*") }.toTypedArray()

      try {
        if (options.save) {
          val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType(mimeTypes.singleOrNull() ?: "*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes)
          if (options.defaultName.isNotBlank()) intent.putExtra(Intent.EXTRA_TITLE, options.defaultName)
          launcherForSaveDocument.launch(intent)
        } else if (options.directories && !options.files) {
          launcherForDirectory.launch(null)
        } else if (options.multiple) {
          launcherForMulitpleDocuments.launch(mimeTypes)
        } else {
          launcherForSingleDocument.launch(mimeTypes)
        }
      } catch (_: RuntimeException) {
        this.resolve(emptyArray<Uri>())
      }
    }
  }

  fun showFileSystemPicker (
    mimeTypes: String,
    directories: Boolean = false,
    multiple: Boolean = false,
    files: Boolean = true,
    owner: Window?,
    save: Boolean,
    defaultName: String,
    pointer: Long = 0
  )  {
    return this.showFileSystemPicker(FileSystemPickerOptions(
      null,
      mimeTypes.split("|").toMutableList(),
      directories,
      multiple,
      files,
      owner,
      save,
      defaultName
    ), fun (uris: Array<Uri>) {
      if (pointer != 0L) {
        this.onResults(pointer, uris)
      }
    })
  }

  fun share (
    title: String?,
    text: String?,
    url: String?,
    files: String,
    bytes: ByteArray,
    pointer: Long
  ) {
    activity.runOnUiThread {
      if (shareActive || activity.isFinishing || activity.isDestroyed) {
        onShareResult(pointer, false, "Sharing is unavailable while another chooser is active")
        return@runOnUiThread
      }
      shareActive = true
      sharePointer = pointer
      // File copying is bounded and runs off the Android UI thread.
      kotlin.concurrent.thread(name = "oro-share") {
        try {
          val parts = listOfNotNull(text, url).filter { it.isNotBlank() }
          val intent = Intent(Intent.ACTION_SEND).setType("text/plain")
          if (parts.isNotEmpty()) intent.putExtra(Intent.EXTRA_TEXT, parts.joinToString("\n\n"))
          if (!title.isNullOrBlank()) intent.putExtra(Intent.EXTRA_SUBJECT, title)
          if (files.isNotEmpty()) {
            ShareFiles.attach(activity.applicationContext, intent, files, bytes)
          } else if (parts.isEmpty()) {
            require(!title.isNullOrBlank()) { "Share data is empty" }
            intent.putExtra(Intent.EXTRA_TEXT, title)
          }
          activity.runOnUiThread {
            if (sharePointer != pointer || activity.isDestroyed) return@runOnUiThread
            try {
              val action = activity.packageName + ".oro.share." + java.util.UUID.randomUUID()
              val receiver = object : BroadcastReceiver() {
                override fun onReceive (context: Context, selected: Intent) {
                  if (selected.action == action) finishShare(true)
                }
              }
              ContextCompat.registerReceiver(activity, receiver, IntentFilter(action), ContextCompat.RECEIVER_NOT_EXPORTED)
              shareReceiver = receiver
              val callback = PendingIntent.getBroadcast(activity, 0,
                Intent(action).setPackage(activity.packageName),
                PendingIntent.FLAG_ONE_SHOT or PendingIntent.FLAG_IMMUTABLE)
              shareIntent = callback
              launcherForShare.launch(Intent.createChooser(intent, title ?: "Share", callback.intentSender))
            } catch (_: Exception) {
              shareActive = false
              finishShare(false, "No share target is available")
            }
          }
        } catch (_: Exception) {
          activity.runOnUiThread {
            if (sharePointer == pointer) {
              shareActive = false
              finishShare(false, "Unable to prepare share; check file names, size, type, and free space")
            }
          }
        }
      }
    }
  }

  @Throws(Exception::class)
  external fun onResults (pointer: Long, results: Array<Uri>): Unit
  external fun onShareResult (pointer: Long, success: Boolean, error: String?): Unit
}
