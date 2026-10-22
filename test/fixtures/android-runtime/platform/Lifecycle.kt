package androidx.lifecycle
class Lifecycle {
  enum class State { DESTROYED, RESUMED; fun isAtLeast(state: State) = ordinal >= state.ordinal }
  var currentState = State.RESUMED
}
