package androidx.lifecycle

interface LifecycleOwner { val lifecycle: Lifecycle }
interface DefaultLifecycleObserver {
  fun onResume (owner: LifecycleOwner) {}
  fun onStop (owner: LifecycleOwner) {}
  fun onDestroy (owner: LifecycleOwner) {}
}
class Lifecycle (private val owner: LifecycleOwner) {
  enum class State { DESTROYED, INITIALIZED, CREATED, STARTED, RESUMED;
    fun isAtLeast (state: State) = ordinal >= state.ordinal
  }
  var currentState = State.RESUMED
  val observers = mutableListOf<DefaultLifecycleObserver>()
  fun addObserver (observer: DefaultLifecycleObserver) {
    observers.add(observer)
    if (currentState == State.RESUMED) observer.onResume(owner)
  }
  fun removeObserver (observer: DefaultLifecycleObserver) { observers.remove(observer) }
  fun move (state: State) {
    currentState = state
    for (observer in observers.toList()) when (state) {
      State.RESUMED -> observer.onResume(owner)
      State.CREATED -> observer.onStop(owner)
      State.DESTROYED -> observer.onDestroy(owner)
      else -> {}
    }
  }
}
