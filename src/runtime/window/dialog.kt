package oro.runtime.window

import java.lang.Runtime

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.Manifest
import android.os.Build
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
    val files: Boolean = true
  ) {
    init {
      if (params != null && params.acceptTypes.size > 0) {
        this.mimeTypes += params.acceptTypes
      }
    }
  }

  var callback: ((results: Array<Uri>) -> Unit)? = null
  var sharePointer: Long = 0

  val launcherForSingleItem = activity.registerForActivityResult(
    ActivityResultContracts.GetContent(),
    fun (uri: Uri?) { this.resolve(uri) }
  )

  val launcherForMulitpleItems = activity.registerForActivityResult(
    ActivityResultContracts.GetMultipleContents(),
    { uris -> this.resolve(uris) }
  )

  // XXX(@jwerle): unused at the moment
  val launcherForSingleDocument = activity.registerForActivityResult(
    ActivityResultContracts.OpenDocument(),
    { uri -> this.resolve(uri) }
  )

  // XXX(@jwerle): unused at the moment
  val launcherForMulitpleDocuments = activity.registerForActivityResult(
    ActivityResultContracts.OpenMultipleDocuments(),
    { uris -> this.resolve(uris) }
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
    { result ->
      val pointer = sharePointer
      sharePointer = 0

      if (pointer != 0L) {
        val success = result.resultCode == Activity.RESULT_OK
        onShareResult(pointer, success, null)
      }
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

    /*
    for (uri in uris) {
      this.activity.applicationContext.contentResolver.takePersistableUriPermission(
        uri,
        (
          Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
          Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
      )
    }
    */

    if (callback != null) {
      this.callback = null
      callback(uris)
    }
  }

  fun showFileSystemPicker (
    options: FileSystemPickerOptions,
    callback: ((Array<Uri>) -> Unit)? = null
  ) {
    val activity = this.activity as oro.runtime.app.AppActivity
    val mimeType =
      if (options.mimeTypes.size > 0 && options.mimeTypes[0].length > 0) {
        options.mimeTypes[0]
      } else { "*/*" }

    this.callback = callback

    val permissions = mutableListOf(
      Manifest.permission.CAMERA
    )

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      permissions += listOf(
        Manifest.permission.READ_MEDIA_IMAGES,
        Manifest.permission.READ_MEDIA_VIDEO,
        Manifest.permission.READ_MEDIA_AUDIO
      )
    } else {
      permissions += Manifest.permission.READ_EXTERNAL_STORAGE
    }

    activity.requestPermissions(permissions.toTypedArray(), { _ ->
      activity.runOnUiThread {
        // TODO(@jwerle): support the other launcher types above
        // through the `showFileSystemPicker()` method some how
        if (options.multiple) {
          launcherForMulitpleItems.launch(mimeType)
        } else {
          //launcherForSingleDocument.launch(arrayOf(mimeType))
          launcherForSingleItem.launch(mimeType)
        }
      }
    })
  }

  fun showFileSystemPicker (
    mimeTypes: String,
    directories: Boolean = false,
    multiple: Boolean = false,
    files: Boolean = true,
    pointer: Long = 0
  )  {
    return this.showFileSystemPicker(FileSystemPickerOptions(
      null,
      mimeTypes.split("|").toMutableList(),
      directories,
      multiple,
      files
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
    pointer: Long
  ) {
    val activity = this.activity as oro.runtime.app.AppActivity
    val parts = mutableListOf<String>()

    if (!text.isNullOrBlank()) {
      parts += text.trim()
    }

    if (!url.isNullOrBlank()) {
      parts += url.trim()
    }

    if (parts.isEmpty() && !title.isNullOrBlank()) {
      parts += title.trim()
    }

    if (parts.isEmpty()) {
      if (pointer != 0L) {
        onShareResult(pointer, false, null)
      }
      return
    }

    val intent = Intent(Intent.ACTION_SEND)
    intent.type = "text/plain"
    intent.putExtra(Intent.EXTRA_TEXT, parts.joinToString(separator = "\n\n"))

    if (!title.isNullOrBlank()) {
      intent.putExtra(Intent.EXTRA_SUBJECT, title)
    }

    val chooser = Intent.createChooser(intent, title ?: "Share")

    sharePointer = pointer

    activity.runOnUiThread {
      try {
        launcherForShare.launch(chooser)
      } catch (err: Exception) {
        sharePointer = 0
        onShareResult(pointer, false, err.message)
      }
    }
  }

  @Throws(Exception::class)
  external fun onResults (pointer: Long, results: Array<Uri>): Unit
  external fun onShareResult (pointer: Long, success: Boolean, error: String?): Unit
}
