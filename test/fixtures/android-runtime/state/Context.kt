package android.content

import java.io.File

open class Context (val root: File) {
  val applicationContext get() = this
  val packageName = "oro.runtime.public.state"
  val noBackupFilesDir get() = File(root, "no-backup")
}
