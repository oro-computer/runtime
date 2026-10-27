package android.os
object Build { object VERSION { var SDK_INT = 37 } }
object Looper { fun myLooper() = 1; fun getMainLooper() = 1 }
class Handler { fun post(action: () -> Unit) { action() } }
