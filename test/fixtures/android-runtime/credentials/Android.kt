package android.os

class Looper {
  companion object {
    private val main = Looper()
    fun getMainLooper() = main
    fun myLooper() = main
  }
}

object SystemClock { var now = 0L; fun elapsedRealtime() = now }

class Handler (looper: Looper) {
  private data class Entry(val time: Long, val action: Runnable)
  private val entries = mutableListOf<Entry>()
  fun post (action: () -> Unit) = post(Runnable(action))
  fun post (action: Runnable): Boolean { entries.add(Entry(SystemClock.now, action)); return true }
  fun postDelayed (action: Runnable, delay: Long): Boolean { entries.add(Entry(SystemClock.now + delay, action)); return true }
  fun removeCallbacks (action: Runnable) { entries.removeAll { it.action === action } }
  fun flush () {
    var count = 0
    while (true) {
      val entry = entries.firstOrNull { it.time <= SystemClock.now } ?: break
      entries.remove(entry)
      check(++count < 1000)
      entry.action.run()
    }
  }
  fun advance (time: Long) { SystemClock.now += time; flush() }
  fun size () = entries.size
}

object Build { object VERSION { const val SDK_INT = 37 } }
open class Bundle {
  private val values = mutableMapOf<String, Any>()
  constructor()
  constructor(other: Bundle) { values.putAll(other.values) }
  fun putBoolean (key: String, value: Boolean) { values[key] = value }
  fun putString (key: String, value: String) { values[key] = value }
  fun putStringArrayList (key: String, value: ArrayList<String>) { values[key] = value }
  fun getString (key: String) = values[key] as? String
}
class CancellationSignal { var cancelled = false; fun cancel () { cancelled = true } }
interface OutcomeReceiver<R, E> { fun onResult (result: R); fun onError (error: E) }
