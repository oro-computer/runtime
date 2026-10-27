package oro.runtime.app

import android.content.Context
import android.os.CancellationSignal
import androidx.annotation.Keep
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.securestorage.SecureStorageManager
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** Process-owned C++ adapters. Android admission, bounded workers and cancellation remain in the shared controllers. */
@Keep
internal object RuntimeNativeCallbacks {
  private lateinit var context: Context
  private external fun nativeCall (command: String, handle: Long, type: Int, input: String, bytes: ByteArray): String
  private fun call (command: String, handle: Long = 0, type: Int = 0, input: String = "", bytes: ByteArray = byteArrayOf()): Any {
    val response = JSONObject(nativeCall(command, handle, type, input, bytes))
    response.optJSONObject("err")?.let { throw NativeStateException(it.getString("code")) }
    return response.get("data")
  }
  private fun task (id: String, kind: String, authority: String, account: String, target: String, operation: String = "") =
    JSONObject().put("id", id).put("kind", kind).put("authorityRef", authority).put("accountRef", account)
      .put("targetRef", target).put("operationRef", operation)

  private class Invocation (type: Int, task: JSONObject, private val signal: CancellationSignal) : AutoCloseable {
    private val closed = AtomicBoolean(false)
    val handle = (call("begin", type = type, input = task.toString()) as Number).toLong()
    init { signal.setOnCancelListener { nativeCall("cancel", handle, 0, "", byteArrayOf()) } }
    fun invoke (stage: String, input: String = "", bytes: ByteArray = byteArrayOf()): Any {
      signal.throwIfCanceled()
      val result = call(stage, handle, input = input, bytes = bytes)
      signal.throwIfCanceled()
      return result
    }
    override fun close () {
      if (!closed.compareAndSet(false, true)) return
      signal.setOnCancelListener(null)
      call("close", handle)
    }
  }

  fun initialize (app: App): List<NativeExtension> {
    if (app.getUserConfigValue("android_native_callbacks_enabled") != "true") return emptyList()
    check(JSONObject(app.getNativeBuildIdentity()).optInt("nativeCallbacksProtocol") == 1) {
      "Rebuild the Android native library and SDK together for compiled callbacks"
    }
    check(JSONObject(app.getNativeBuildIdentity()).optInt("nativeServicesProtocol") == 1) {
      "Rebuild the Android native library and SDK together for native services"
    }
    context = app.applicationContext
    val descriptors = call("initialize") as JSONArray
    val extensions = mutableListOf<NativeExtension>()
    for (index in 0 until descriptors.length()) {
      val descriptor = descriptors.getJSONObject(index)
      val type = descriptor.getInt("type")
      val kind = descriptor.getString("kind")
      when (type) {
        0, 1 -> (if (type == 0) ForegroundTasks else ContinuousTasks).registerExecutor(kind, ForegroundExecutor { _, value, signal ->
          val invocation = Invocation(type, task(value.id, kind, value.authorityRef, value.accountRef, value.targetRef, value.operationRef), signal)
          try {
            if (invocation.invoke("authorize") != true) { invocation.close(); null }
            else object : AuthorizedForegroundExecution {
              override fun execute (cancellation: CancellationSignal): ForegroundOutcome = when (invocation.invoke("run")) {
                "completed" -> ForegroundOutcome.COMPLETED
                "unauthorized" -> ForegroundOutcome.UNAUTHORIZED
                else -> ForegroundOutcome.UNKNOWN
              }
              override fun close () = invocation.close()
            }
          } catch (error: Exception) { invocation.close(); throw error }
        })
        2 -> DeferredSync.registerReadExecutor(kind, DeferredReadExecutor { _, value, signal ->
          val invocation = Invocation(type, task(value.id, kind, value.authorityRef, value.accountRef, value.targetRef, value.operationRef), signal)
          try {
            if (invocation.invoke("authorize") != true) { invocation.close(); null }
            else object : AuthorizedDeferredRead {
              override fun reconcile (cancellation: CancellationSignal): DeferredReadOutcome = when (invocation.invoke("run")) {
                "completed" -> DeferredReadOutcome.COMPLETED
                "retry" -> DeferredReadOutcome.RETRY
                "conflict" -> DeferredReadOutcome.CONFLICT
                "unauthorized" -> DeferredReadOutcome.UNAUTHORIZED
                else -> DeferredReadOutcome.UNKNOWN
              }
              override fun close () = invocation.close()
            }
          } catch (error: Exception) { invocation.close(); throw error }
        })
        3 -> {
          val values = descriptor.getJSONArray("operations")
          val operations = (0 until values.length()).map { values.getString(it) }.toSet()
          DeferredSync.registerMaintenanceExecutor(kind, operations, true, DeferredMaintenanceExecutor { _, value, signal ->
            val invocation = Invocation(type, task(value.id, kind, value.authorityRef, value.accountRef, value.targetRef, value.operationRef), signal)
            try {
              if (invocation.invoke("authorize") != true) { invocation.close(); null }
              else object : AuthorizedDeferredMaintenance {
                override fun perform (cancellation: CancellationSignal): DeferredMaintenanceOutcome = when (invocation.invoke("run")) {
                  "completed" -> DeferredMaintenanceOutcome.COMPLETED
                  "retry" -> DeferredMaintenanceOutcome.RETRY
                  "unauthorized" -> DeferredMaintenanceOutcome.UNAUTHORIZED
                  else -> DeferredMaintenanceOutcome.UNKNOWN
                }
                override fun close () = invocation.close()
              }
            } catch (error: Exception) { invocation.close(); throw error }
          })
        }
        4 -> NativePush.registerHandler(kind, object : NativePushHandler {
          private fun dispatch (value: NativePushRegistration, signal: CancellationSignal, stage: String,
            input: String = "", bytes: ByteArray = byteArrayOf()): Boolean =
            Invocation(type, task(value.id, kind, value.authorityRef, value.accountRef, value.targetRef), signal).use {
              it.invoke(stage, input, bytes) == true
            }
          override fun authorize (context: Context, registration: NativePushRegistration) = false
          override fun authorize (context: Context, registration: NativePushRegistration, cancellation: CancellationSignal) =
            dispatch(registration, cancellation, "push-authorize")
          override fun onMessage (context: Context, registration: NativePushRegistration, message: ByteArray, cancellation: CancellationSignal) =
            dispatch(registration, cancellation, "push-message", bytes = message)
          override fun onRegistrationChanged (context: Context, registration: NativePushRegistration, enrollment: JSONObject, cancellation: CancellationSignal) =
            dispatch(registration, cancellation, "push-enrollment", input = enrollment.toString())
        })
        5 -> extensions.add(object : NativeExtension {
          override val name = kind
          override fun request (call: NativeExtensionCall, action: String, options: JSONObject) {
            call.runOnWorker { signal ->
              Invocation(type, task("", kind, "", "", ""), signal).use {
                it.invoke("extension", JSONObject().put("action", action).put("payload", options).toString())
              }
            }
          }
        })
        else -> throw IllegalStateException("Unknown native callback type")
      }
    }
    return extensions
  }

  /** JNI-only key access, in a namespace rejected by the renderer secure-storage API. */
  private fun recordKey (action: Int, scope: String, slot: Int, value: ByteArray): ByteArray? {
    check(App.getInstance().getUserConfigValue("secure_storage_enabled") != "false")
    require(Regex("[a-f0-9]{64}").matches(scope) && slot in listOf('a'.code, 'b'.code))
    val space = "oro.native.records.v2:cpp:${context.packageName}"
    val key = "record.$scope.${slot.toChar()}"
    return when (action) {
      0 -> SecureStorageManager.get(context, space, key)
      1 -> { require(value.size == 32); check(SecureStorageManager.set(context, space, key, value)); null }
      2 -> { check(SecureStorageManager.remove(context, space, key)); null }
      else -> throw IllegalArgumentException("Unknown native record operation")
    }
  }
  private fun recordDirectory (): String = File(context.noBackupFilesDir, "oro.native.cpp.v2").absolutePath
  private fun applicationContext (): Context = context
  private fun nativeServices(source: String, cancellation: CancellationSignal?): String = NativeServices.call(context, source, cancellation)

  /** Invoked by compiled policy only, after product authorization and recovery consent. */
  private fun recoverPush (source: String): String = try {
    val options = JSONObject(source)
    val result: Any = when (options.optString("action")) {
      "inspectJournal" -> NativePush.inspectJournalRecovery(context)
      "quarantineJournal" -> NativePush.quarantineUnreadableJournal(context, options)
      else -> {
        val authority = options.getString("authorityRef")
        val account = options.getString("accountRef")
        val kind = options.getString("kind")
        if (options.getString("id").isEmpty()) NativePush.discoverRegistrations(context, authority, account, kind)
        else NativePush.unregisterIfCurrent(context, authority, account, kind, options.getString("id"), options.getString("revision"))
      }
    }
    JSONObject().put("data", result).toString()
  } catch (error: PlatformFailure) { JSONObject().put("error", error.code).toString()
  } catch (error: NativeStateException) { JSONObject().put("error", error.code).toString()
  } catch (_: Exception) { JSONObject().put("error", "EIO").toString() }
}
