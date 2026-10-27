package android.os

object Looper {
  private val main = Any()
  private val thread = Thread.currentThread()
  fun getMainLooper() = main
  fun myLooper(): Any? = if (Thread.currentThread() === thread) main else null
}
object SystemClock {
  var offset = 0L
  fun elapsedRealtime() = System.nanoTime() / 1000000 + offset
}
class Handler(looper: Any) {
  companion object {
    private val queue = mutableListOf<Pair<Long, Runnable>>()
    fun pump() {
      val tasks = synchronized(queue) { queue.filter { it.first <= SystemClock.elapsedRealtime() }.also { queue.removeAll(it.toSet()) } }
      tasks.forEach { it.second.run() }
    }
  }
  fun post(task: Runnable) { synchronized(queue) { queue.add(0L to task) } }
  fun postDelayed(task: Runnable, delay: Long) { synchronized(queue) { queue.add(SystemClock.elapsedRealtime() + delay to task) } }
}
class CancellationSignal {
  @Volatile var isCanceled = false
  private var listener: (() -> Unit)? = null
  fun setOnCancelListener(callback: () -> Unit) { listener = callback; if (isCanceled) callback() }
  fun cancel() { isCanceled = true; listener?.invoke() }
  fun throwIfCanceled() { if (isCanceled) throw IllegalStateException("public-cancelled") }
}
object Build { object VERSION { var SDK_INT = 37 } }
object Process { fun myPid() = 1 }
interface IBinder
