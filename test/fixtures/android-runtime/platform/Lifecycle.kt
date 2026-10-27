package androidx.lifecycle
class Lifecycle {
  enum class State { DESTROYED, STARTED, RESUMED; fun isAtLeast(state: State) = ordinal >= state.ordinal }
  var currentState = State.RESUMED
}
