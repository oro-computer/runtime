package android.os

object Build { object VERSION { const val SDK_INT = 37 } }
class PersistableBundle {
  private val values = mutableMapOf<String, String>()
  fun putString(key: String, value: String) { values[key] = value }
  fun getString(key: String): String? = values[key]
}
class OperationCanceledException : RuntimeException()
class CancellationSignal {
  @Volatile var isCanceled = false
  fun cancel() { isCanceled = true }
  fun throwIfCanceled() { if (isCanceled) throw OperationCanceledException() }
}
class Looper { companion object { fun getMainLooper() = Looper() } }
class Handler(looper: Looper) {
  companion object {
    @Volatile var paused = false
    val pending = java.util.concurrent.ConcurrentLinkedQueue<() -> Unit>()
    fun drain() {
      paused = false
      while (true) (pending.poll() ?: return).invoke()
    }
  }
  fun post(action: () -> Unit) { if (paused) pending.add(action) else action() }
}
