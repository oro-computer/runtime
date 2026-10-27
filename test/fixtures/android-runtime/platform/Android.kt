package android.os
class Looper { companion object {
  private val main = Looper()
  fun getMainLooper() = main
  fun myLooper() = main
} }
class Handler(looper: Looper) { fun post(action: () -> Unit) { action() } }
object SystemClock { fun elapsedRealtime() = System.nanoTime() / 1000000 }
