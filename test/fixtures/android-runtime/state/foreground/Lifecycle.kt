package androidx.lifecycle
class Lifecycle {
  enum class State { CREATED, STARTED, RESUMED; fun isAtLeast(other: State) = this >= other }
  var currentState = State.RESUMED
}
