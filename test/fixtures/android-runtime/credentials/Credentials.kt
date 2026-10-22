package android.credentials

import android.os.*
import java.util.concurrent.Executor
import oro.runtime.window.WindowManagerActivity

class Credential (val type: String, val data: Bundle)
class GetCredentialResponse (val credential: Credential)
class GetCredentialException (val type: String) : Exception() {
  companion object {
    const val TYPE_USER_CANCELED = "cancelled"
    const val TYPE_NO_CREDENTIAL = "missing"
    const val TYPE_INTERRUPTED = "interrupted"
  }
}
class CredentialOption {
  class Builder (type: String, data: Bundle, query: Bundle) {
    fun setIsSystemProviderRequired (value: Boolean) = this
    fun build () = CredentialOption()
  }
}
class GetCredentialRequest {
  class Builder (data: Bundle) {
    fun addCredentialOption (value: CredentialOption) = this
    fun build () = GetCredentialRequest()
  }
}
class CredentialManager {
  lateinit var receiver: OutcomeReceiver<GetCredentialResponse, GetCredentialException>
  lateinit var signal: CancellationSignal
  var requests = 0
  fun getCredential (activity: WindowManagerActivity, request: GetCredentialRequest, cancellation: CancellationSignal,
      executor: Executor, callback: OutcomeReceiver<GetCredentialResponse, GetCredentialException>) {
    requests++
    signal = cancellation
    receiver = callback
  }
}
