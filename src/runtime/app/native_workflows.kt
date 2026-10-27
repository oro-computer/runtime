package oro.runtime.app

import android.content.Context
import android.net.Uri
import android.os.CancellationSignal
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.securestorage.SecureStorageManager
import java.util.UUID

/** Packaged GET/checkpoint/notification executors; no product native code or background JavaScript. */
internal object NativeWorkflows {
  private val policies = linkedMapOf<String, WorkflowPolicy>()
  private val active = mutableMapOf<String, CancellationSignal>()
  private class MonitorWake { val monitor = java.lang.Object(); var version = 0L }
  private val monitorWakes = mutableMapOf<String, MonitorWake>()
  private val revoking = mutableSetOf<String>()
  private class Gate { val monitor = Any(); var references = 0 }
  private val presentationGates = mutableMapOf<String, Gate>()
  private fun <T> withGate(id: String, operation: () -> T): T {
    val gate = synchronized(this) { presentationGates.getOrPut(id) { Gate() }.also { it.references++ } }
    try { return synchronized(gate.monitor) { operation() } }
    finally { synchronized(this) { if (--gate.references == 0) presentationGates.remove(id) } }
  }
  private var enabled = false
  private var error = ""
  private fun identifier(value: String): String = value.also { require(Regex("[A-Za-z0-9_-]{1,128}").matches(it)) }
  private fun policy(id: String) = policies[id] ?: throw NativeStateException("ENOENT")
  private fun store(context: Context) = NativeRecords.runtime(context, "workflows")
  private fun read(context: Context, kind: String, id: String): JSONObject {
    val snapshot = store(context).read()
    try { return JSONObject((snapshot.records[kind + "." + identifier(id)] ?: throw NativeStateException("ENOENT")).toString(Charsets.UTF_8)) }
    finally { snapshot.records.values.forEach { it.fill(0) } }
  }
  private fun count(context: Context, kind: String): Int {
    val snapshot = store(context).read()
    try { return snapshot.records.keys.count { it.startsWith("$kind.") } }
    finally { snapshot.records.values.forEach { it.fill(0) } }
  }
  /** Only local transforms may be retried. No network or OS submission is inside this loop. */
  private fun change(context: Context, kind: String, id: String, transform: (JSONObject) -> Unit): JSONObject {
    val key = kind + "." + identifier(id)
    repeat(8) {
      val snapshot = store(context).read()
      try {
        if (!snapshot.records.containsKey(key)) {
          val maximum = if (kind == "binding") 8 else 16
          if (snapshot.records.keys.count { it.startsWith("$kind.") } >= maximum) throw NativeStateException("ENOSPC")
        }
        val value = JSONObject(snapshot.records[key]?.toString(Charsets.UTF_8) ?: "{}")
        transform(value)
        if (kind == "binding" && !snapshot.records.containsKey(key) && value.has("credentialHandle")) {
          val credentialBytes = snapshot.records["credential." + identifier(value.getString("credentialHandle"))]
            ?: throw NativeStateException("EACCES")
          val credential = JSONObject(credentialBytes.toString(Charsets.UTF_8))
          scope(credential, value)
          if (!credential.getBoolean("enabled") ||
              (credential.optLong("expiresAt") > 0 && credential.getLong("expiresAt") <= System.currentTimeMillis()))
            throw NativeStateException("EACCES")
        }
        if (kind in setOf("binding", "credential") && !snapshot.records.containsKey(key) && value.optString("authorityHandle").isNotEmpty()) {
          val encodedAuthority = snapshot.records["authority." + identifier(value.getString("authorityHandle"))]
            ?: throw NativeStateException("EACCES")
          val authority = JSONObject(encodedAuthority.toString(Charsets.UTF_8))
          for (field in listOf("workflow", "policyRevision", "documentOrigin", "authorityRef", "accountRef", "targetRef"))
            if (authority.getString(field) != value.getString(field)) throw NativeStateException("EACCES")
          if (!authority.getBoolean("enabled")) throw NativeStateException("EACCES")
        }
        val encoded = value.toString().toByteArray(Charsets.UTF_8)
        require(encoded.size <= 65536)
        try {
          val result = store(context).apply(snapshot.revision, mapOf(key to encoded))
          result.records.values.forEach { it.fill(0) }
          return value
        } catch (failure: NativeStateException) {
          if (failure.code != "ESTALE") throw failure
        } finally { encoded.fill(0) }
      } finally { snapshot.records.values.forEach { it.fill(0) } }
    }
    throw NativeStateException("EBUSY")
  }
  private fun remove(context: Context, kind: String, id: String, validate: (JSONObject) -> Unit) {
    val key = kind + "." + identifier(id)
    repeat(8) {
      val snapshot = store(context).read()
      try {
        val bytes = snapshot.records[key] ?: throw NativeStateException("ENOENT")
        validate(JSONObject(bytes.toString(Charsets.UTF_8)))
        try {
          val result = store(context).apply(snapshot.revision, mapOf(key to null))
          result.records.values.forEach { it.fill(0) }
          return
        } catch (failure: NativeStateException) { if (failure.code != "ESTALE") throw failure }
      } finally { snapshot.records.values.forEach { it.fill(0) } }
    }
    throw NativeStateException("EBUSY")
  }
  private fun scope(value: JSONObject, expected: JSONObject) {
    for (field in listOf("workflow", "policyRevision", "documentOrigin", "authorityHandle", "authorityRef", "accountRef", "targetRef"))
      if (value.getString(field) != expected.getString(field)) throw NativeStateException("EACCES")
  }
  private fun binding(context: Context, id: String, generation: String? = null): JSONObject {
    val value = read(context, "binding", id)
    if (!value.optBoolean("enabled") || (generation != null && value.getString("generation") != generation))
      throw NativeStateException("EACCES")
    if (value.optString("policyRevision") != policy(value.getString("workflow")).revision) throw NativeStateException("EACCES")
    if (policy(value.getString("workflow")).approvedPath != null) {
      val authority = read(context, "authority", value.getString("authorityHandle"))
      for (field in listOf("workflow", "policyRevision", "documentOrigin", "authorityRef", "accountRef", "targetRef"))
        if (authority.getString(field) != value.getString(field)) throw NativeStateException("EACCES")
      if (!authority.getBoolean("enabled")) throw NativeStateException("EACCES")
    }
    if (policy(value.getString("workflow")).credentialKey != null) {
      val handle = value.getString("credentialHandle")
      synchronized(this) { if (handle in revoking) throw NativeStateException("EACCES") }
      val credential = read(context, "credential", handle)
      scope(credential, value)
      if (!credential.getBoolean("enabled") ||
          (credential.optLong("expiresAt") > 0 && credential.getLong("expiresAt") <= System.currentTimeMillis()))
        throw NativeStateException("EACCES")
    }
    return value
  }
  private fun authorized(context: Context, id: String, kind: String, authority: String, account: String): Boolean = try {
    val value = binding(context, id)
    value.getString("authorityRef") == authority && value.getString("accountRef") == account &&
      policy(value.getString("workflow")).kind == kind
  } catch (_: Exception) { false }

  fun initialize(app: App) {
    enabled = app.getUserConfigValue("workflows_enabled") == "true"
    if (!enabled) return
    if (JSONObject(app.getNativeBuildIdentity()).optInt("nativeWorkflowsProtocol") != 1) {
      error = "ENOTREADY"
      return
    }
    try {
      val path = app.getUserConfigValue("workflows_definitions").ifBlank { "workflows.json" }
      require(Regex("[A-Za-z0-9_./-]{1,256}").matches(path) &&
        path.split('/').none { it.isEmpty() || it == "." || it == ".." })
      val bytes = app.assets.open(path).use { input ->
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(4096)
        while (true) {
          val count = input.read(buffer)
          if (count < 0) break
          require(output.size() + count <= 65536)
          output.write(buffer, 0, count)
        }
        output.toByteArray()
      }
      require(bytes.size <= 65536)
      for (definition in WorkflowPolicy.parse(Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
        .onUnmappableCharacter(java.nio.charset.CodingErrorAction.REPORT).decode(java.nio.ByteBuffer.wrap(bytes)).toString())) policies[definition.id] = definition
      for (definition in policies.values) {
        ForegroundTasks.registerExecutor(definition.kind, ForegroundExecutor { context, task, _ ->
          if (!authorized(context, task.targetRef, task.kind, task.authorityRef, task.accountRef) || task.operationRef != "read") null
          else object : AuthorizedForegroundExecution {
            override fun execute(signal: CancellationSignal): ForegroundOutcome = try {
              perform(context, task.targetRef, signal); ForegroundOutcome.COMPLETED
            } catch (_: WorkflowReadFailure) { ForegroundOutcome.FAILED
            } catch (failure: NativeStateException) {
              if (failure.code == "EACCES") ForegroundOutcome.UNAUTHORIZED else ForegroundOutcome.UNKNOWN
            } catch (_: Exception) { ForegroundOutcome.UNKNOWN }
            override fun close() {}
          }
        })
        val pollInterval = definition.pollIntervalMs
        if (pollInterval != null) ContinuousTasks.registerExecutor(definition.kind, ForegroundExecutor { context, task, _ ->
          if (!authorized(context, task.targetRef, task.kind, task.authorityRef, task.accountRef) || task.operationRef != "read") null
          else object : AuthorizedForegroundExecution {
            override fun execute(signal: CancellationSignal): ForegroundOutcome {
              val wake = MonitorWake()
              claim(task.targetRef, signal, wake)
              try {
                while (true) {
                  signal.throwIfCanceled()
                  var cooldown = false
                  var delay = pollInterval.toLong()
                  try { perform(context, task.targetRef, signal, true)
                  } catch (failure: WorkflowReadFailure) {
                    if (!failure.retryable) throw failure
                    binding(context, task.targetRef)
                    cooldown = true
                    delay = maxOf(delay, WorkflowReadRetry.COOLDOWN_MS)
                    val admitted = binding(context, task.targetRef)
                    change(context, "binding", task.targetRef) {
                      if (!it.getBoolean("enabled") || it.getString("generation") != admitted.getString("generation")) throw NativeStateException("EACCES")
                      it.put("lastOutcome", "retry-cooldown").put("retryAt", System.currentTimeMillis() + delay)
                    }
                  }
                  val handled = read(context, "binding", task.targetRef).optLong("handledWakeVersion")
                  val monitor = wake.monitor
                  val until = android.os.SystemClock.elapsedRealtime() + delay
                  signal.setOnCancelListener { synchronized(monitor) { monitor.notifyAll() } }
                  try {
                    synchronized(monitor) {
                      while (!signal.isCanceled && (cooldown || wake.version <= handled)) {
                        val remaining = until - android.os.SystemClock.elapsedRealtime()
                        if (remaining <= 0) break
                        monitor.wait(remaining)
                      }
                    }
                  } finally { signal.setOnCancelListener(null) }
                }
              } catch (_: WorkflowReadFailure) { return ForegroundOutcome.FAILED
              } catch (failure: NativeStateException) {
                return if (failure.code == "EACCES") ForegroundOutcome.UNAUTHORIZED else ForegroundOutcome.UNKNOWN
              } catch (_: Exception) { return ForegroundOutcome.UNKNOWN
              } finally {
                withGate(task.targetRef) {
                  synchronized(NativeWorkflows) { active.remove(task.targetRef); monitorWakes.remove(task.targetRef) }
                }
                NativePush.start(context)
              }
            }
            override fun close() {}
          }
        })
        DeferredSync.registerReadExecutor(definition.kind, DeferredReadExecutor { context, task, _ ->
          if (!authorized(context, task.targetRef, task.kind, task.authorityRef, task.accountRef) || task.operationRef != "read") null
          else object : AuthorizedDeferredRead {
            override fun reconcile(signal: CancellationSignal): DeferredReadOutcome = try {
              perform(context, task.targetRef, signal); DeferredReadOutcome.COMPLETED
            } catch (failure: WorkflowReadFailure) { if (failure.retryable) DeferredReadOutcome.RETRY else DeferredReadOutcome.FAILED
            } catch (failure: NativeStateException) {
              if (failure.code == "EACCES") DeferredReadOutcome.UNAUTHORIZED else DeferredReadOutcome.UNKNOWN
            } catch (_: Exception) { DeferredReadOutcome.UNKNOWN }
            override fun close() {}
          }
        })
        NativePush.registerHandler(definition.kind, object : NativePushHandler {
          override fun authorize(context: Context, value: NativePushRegistration): Boolean = value.id == value.targetRef &&
            authorized(context, value.targetRef, value.kind, value.authorityRef, value.accountRef)
          override fun onMessage(context: Context, value: NativePushRegistration, message: ByteArray, signal: CancellationSignal): Boolean = try {
            if (!queueHint(context, value.targetRef)) perform(context, value.targetRef, signal)
            true
          } catch (_: Exception) { false }
          // Foreground JavaScript authenticates server enrollment and acknowledges its endpoint generation.
        })
      }
    } catch (_: Exception) { policies.clear(); error = "EINVAL" }
  }
  private fun orphan(value: JSONObject) {
    if (value.optString("state") == "running") value.put("interruptedOutcome", "unknown")
    val events = value.optJSONObject("events") ?: JSONObject()
    for (id in events.keys()) if (events.optString(id) == "attempting") events.put(id, "unknown")
    value.put("events", events)
  }
  private fun queueHint(context: Context, id: String): Boolean = withGate(id) {
    val wake = synchronized(this) { monitorWakes[id]?.takeIf { active[id]?.isCanceled == false } }
    if (wake == null) false else {
      binding(context, id)
      val updated = change(context, "binding", id) {
        val version = it.optLong("wakeVersion")
        if (version == Long.MAX_VALUE) throw NativeStateException("EOVERFLOW")
        it.put("wakeVersion", version + 1)
      }
      synchronized(wake.monitor) { wake.version = maxOf(wake.version, updated.getLong("wakeVersion")); wake.monitor.notifyAll() }
      true
    }
  }
  /** Retained adapters can only select the exact already-approved runtime binding. */
  internal fun validateRetained(context: Context, id: String, generation: String, identity: JSONObject) = withGate(id) {
    val value = binding(context, identifier(id))
    if (value.getString("generation") != generation || listOf("documentOrigin", "authorityRef", "accountRef", "targetRef")
        .any { value.getString(it) != identity.getString(it) }) throw NativeStateException("ESTALE")
    Unit
  }
  internal fun hintRetained(context: Context, id: String, generation: String, identity: JSONObject) {
    withGate(id) {
      validateRetained(context, id, generation, identity)
      if (!queueHint(context, id)) change(context, "binding", id) {
        val version = it.optLong("wakeVersion")
        if (version == Long.MAX_VALUE) throw NativeStateException("EOVERFLOW")
        it.put("wakeVersion", version + 1)
      }
    }
  }
  internal fun readRetained(context: Context, id: String, generation: String, identity: JSONObject, signal: CancellationSignal) {
    validateRetained(context, id, generation, identity)
    signal.throwIfCanceled()
    perform(context, id, signal)
  }
  private fun claim(id: String, signal: CancellationSignal, wake: MonitorWake? = null) {
    withGate(id) {
      synchronized(this) {
        if (active.containsKey(id) || active.size >= 8) throw NativeStateException("EBUSY")
        active[id] = signal
        if (wake != null) monitorWakes[id] = wake
      }
    }
  }
  private fun perform(context: Context, id: String, signal: CancellationSignal, owned: Boolean = false) {
    if (!owned) claim(id, signal)
    var generation: String? = null
    var reading = true
    try {
      val current = binding(context, id)
      generation = current.getString("generation")
      val definition = policy(current.getString("workflow"))
      signal.throwIfCanceled()
      change(context, "binding", id) {
        if (!it.getBoolean("enabled") || it.getString("generation") != generation) throw NativeStateException("EACCES")
        orphan(it); it.put("state", "running")
      }
      val received = WorkflowReadRetry.execute(definition.timeoutMs, { android.os.SystemClock.elapsedRealtime() },
        { delay ->
          val monitor = java.lang.Object()
          val until = android.os.SystemClock.elapsedRealtime() + delay
          signal.setOnCancelListener { synchronized(monitor) { monitor.notifyAll() } }
          try {
            synchronized(monitor) {
              while (!signal.isCanceled) {
                val remaining = until - android.os.SystemClock.elapsedRealtime()
                if (remaining <= 0) break
                monitor.wait(remaining)
              }
            }
            signal.throwIfCanceled()
          } finally { signal.setOnCancelListener(null) }
        }, { attempt, delay ->
          change(context, "binding", id) {
            if (!it.getBoolean("enabled") || it.getString("generation") != generation) throw NativeStateException("EACCES")
            it.put("readAttempts", attempt).put("retryAt", if (delay > 0) System.currentTimeMillis() + delay else 0)
              .put("lastOutcome", if (delay > 0) "retrying" else "reading")
          }
        }) { timeout ->
          signal.throwIfCanceled()
          val fresh = binding(context, id, generation)
          val headers = mutableMapOf<String, String>()
          if (definition.credentialKey != null)
            headers["Authorization"] = "Bearer " + read(context, "credential", fresh.getString("credentialHandle")).getString("value")
          val cursor = fresh.optString("cursor")
          val endpoint = if (definition.approvedPath == null) definition.url else {
            val destination = read(context, "authority", fresh.getString("authorityHandle"))
            destination.getString("origin") + destination.optString("basePath") + definition.approvedPath
          }
          val url = if (definition.cursorParameter != null && cursor.isNotEmpty())
            Uri.parse(endpoint).buildUpon().appendQueryParameter(definition.cursorParameter, cursor).build().toString() else endpoint
          val trust = try { NativeTlsPolicy.value(context) }
            catch (failure: NativeHttpsFailure) { throw WorkflowReadFailure(failure.code) }
          val response = try {
            NativeHttps.requestOwned(context, url, "GET", headers, null, signal, timeout, definition.maximumResponseBytes)
          } catch (failure: NativeHttpsFailure) {
            throw WorkflowReadFailure(failure.code, failure.code in setOf("EIO", "ETIMEDOUT"))
          } finally { headers.clear() }
          try {
            if (response.status in listOf(401, 403)) throw NativeStateException("EACCES")
            if (response.status !in 200..299)
              throw WorkflowReadFailure("EHTTP", response.status in listOf(408, 429, 500, 502, 503, 504))
            val plan = try { definition.plan(response.body, fresh) }
              catch (failure: NativeStateException) { throw failure }
              catch (_: Exception) { throw WorkflowReadFailure("EINVAL") }
            Triple(plan, trust, fresh)
          } finally { response.body.fill(0) }
        }
      reading = false
      val plan = received.first
      val trust = received.second
      NativeNotifications.configureChannels(context, JSONArray(definition.channels))
      for (event in plan.events) {
        signal.throwIfCanceled()
        binding(context, id, generation)
        var submit = false
        change(context, "binding", id) {
          if (!it.getBoolean("enabled") || it.getString("generation") != generation) throw NativeStateException("EACCES")
          val events = it.getJSONObject("events")
          submit = !events.has(event.key)
          if (submit) {
            if (events.length() >= 128) throw NativeStateException("ENOSPC")
            events.put(event.key, "attempting")
          }
        }
        if (!submit) continue
        // A durable attempt precedes OS submission; losing the reply is never permission to repeat it.
        withGate(id) {
          signal.throwIfCanceled()
          binding(context, id, generation)
          if (!NativeTlsPolicy.current(context, trust)) throw NativeStateException("ETRUST")
          event.presentation.put("id", "workflow-$id-" + event.key)
          NativeNotifications.submitNative(context, event.presentation, "$id:" + event.key)
        }
        change(context, "binding", id) { it.getJSONObject("events").put(event.key, "submitted") }
      }
      signal.throwIfCanceled()
      binding(context, id, generation)
      change(context, "binding", id) {
        if (!it.getBoolean("enabled") || it.getString("generation") != generation) throw NativeStateException("EACCES")
        if (plan.cursor != null) it.put("cursor", plan.cursor)
        it.put("handledWakeVersion", maxOf(it.optLong("handledWakeVersion"), received.third.optLong("wakeVersion")))
        it.put("state", "idle").put("lastOutcome", "completed").put("retryAt", 0)
      }
    } catch (failure: Exception) {
      val readFailure = if (reading && failure !is WorkflowReadFailure &&
          !(failure is NativeStateException && failure.code == "EACCES"))
        WorkflowReadFailure(if (signal.isCanceled) "ECANCELLED" else if (failure is NativeStateException) failure.code else "EIO")
        else failure
      try { if (generation != null) change(context, "binding", id) {
        if (it.optString("generation") == generation) {
          orphan(it)
          it.put("retryAt", 0).put("state", if (reading) "idle" else "unknown")
            .put("lastOutcome", if (reading) (if (signal.isCanceled) "cancelled-read" else if (failure is NativeStateException && failure.code == "EACCES") "unauthorized" else "failed-read") else "unknown")
        }
      } } catch (_: Exception) {}
      throw readFailure
    } finally { if (!owned) synchronized(this) { active.remove(id) } }
  }
  private fun summary(context: Context, id: String): JSONObject = withGate(id) {
    var value = read(context, "binding", id)
    val running = synchronized(this) { active.containsKey(id) }
    if (!running && value.optString("state") == "running") value = change(context, "binding", id) {
      orphan(it); it.put("state", "unknown").put("lastOutcome", "unknown")
    }
    val events = value.optJSONObject("events") ?: JSONObject()
    JSONObject().put("id", id).put("workflow", value.getString("workflow")).put("generation", value.getString("generation"))
      .put("enabled", value.getBoolean("enabled")).put("state", if (running) "running" else value.optString("state", "idle"))
      .put("lastOutcome", value.optString("lastOutcome", "not-started")).put("running", running).put("drained", !running)
      .put("interruptedOutcome", value.optString("interruptedOutcome"))
      .put("readAttempts", value.optInt("readAttempts")).put("retryAt", if (running) value.optLong("retryAt") else 0)
      .put("pendingRead", value.optLong("wakeVersion") > value.optLong("handledWakeVersion"))
      .put("retainedEvents", events.length())
      .put("uncertainEvents", events.keys().asSequence().count { events.optString(it) in setOf("attempting", "unknown") })
  }
  private fun cancel(id: String) {
    val signal = synchronized(this) { active[id] } ?: return
    try { NativeExtensionWorkers.cancellations.execute { signal.cancel() } }
    catch (_: java.util.concurrent.RejectedExecutionException) {
      // Disabled/generation fences deny new presentation; I/O keeps its slot until its cleanup returns.
    }
  }
  private fun identity(input: JSONObject, origin: String): JSONObject {
    val value = JSONObject().put("workflow", policy(input.getString("workflow")).id)
      .put("policyRevision", policy(input.getString("workflow")).revision).put("documentOrigin", origin)
    for (field in listOf("authorityRef", "accountRef", "targetRef")) value.put(field, identifier(input.getString(field)))
    val handle = input.optString("authorityHandle")
    val definition = policy(value.getString("workflow"))
    if (definition.approvedPath == null) require(handle.isEmpty()) else identifier(handle)
    return value.put("authorityHandle", handle)
  }
  private fun owned(context: Context, id: String, origin: String, requireEnabled: Boolean = true): JSONObject {
    val value = if (requireEnabled) binding(context, id) else read(context, "binding", id)
    if (value.getString("documentOrigin") != origin) throw NativeStateException("EACCES")
    return value
  }
  private fun task(id: String, value: JSONObject, taskId: String = id) = JSONObject().put("id", taskId)
    .put("kind", policy(value.getString("workflow")).kind).put("authorityRef", value.getString("authorityRef"))
    .put("accountRef", value.getString("accountRef")).put("targetRef", id).put("operationRef", "read")

  internal fun request(call: PlatformCall, action: String, options: JSONObject) {
    val owner = call.foreground()
    val uri = Uri.parse(owner.fragment.webview.url ?: throw PlatformFailure("ENOTREADY"))
    val origin = uri.scheme + "://" + uri.encodedAuthority
    if (action == "support") {
      require(options.length() == 0)
      call.success(JSONObject().put("available", enabled && error.isEmpty()).put("error", error)
        .put("workflows", JSONArray(policies.keys.toList())).put("nativeIntegrationRequired", false).put("backgroundJavaScript", false)
        .put("executorComposition", "workflow-namespace-v1").put("maximumDefinitions", 16).put("maximumApplicationHandlersPerType", 16)
        .put("correlationVersion", "workflow-events-v1").put("approvedBasePaths", true)
        .put("readRetryPolicy", WorkflowReadRetry.VERSION).put("maximumReadAttempts", WorkflowReadRetry.MAX_ATTEMPTS)
        .put("maximumReadBudgetMs", WorkflowReadRetry.BUDGET_MS).put("minimumReadCycleCooldownMs", WorkflowReadRetry.COOLDOWN_MS)
        .put("maximumBindings", 8).put("maximumRetainedEventsPerBinding", 128))
      return
    }
    if (!enabled) throw PlatformFailure("ENOSYS")
    if (error.isNotEmpty()) throw PlatformFailure(error)
    if (action in setOf("run", "monitor")) {
      require(options.keys().asSequence().all { it in
        if (action == "run") setOf("id", "userInitiated", "durationMs") else setOf("id", "userInitiated") } && options.opt("userInitiated") == true)
      val id = identifier(options.getString("id"))
      val duration = options.opt("durationMs") ?: 60000L
      require(duration is Number && duration.toDouble() == duration.toLong().toDouble() && duration.toLong() in 1000L..1800000L)
      NativeExtensionCall(call).runOnWorker { signal ->
        val value = owned(call.app, id, origin)
        if (action == "monitor" && policy(value.getString("workflow")).pollIntervalMs == null) throw NativeStateException("ENOSYS")
        val input = task(id, value, UUID.randomUUID().toString()).put("userInitiated", true)
        if (action == "run") input.put("durationMs", duration.toLong())
        PlatformOperations.main.post {
          try {
            signal.throwIfCanceled()
            if (call.done.get()) throw NativeStateException("ABORT_ERR")
            (if (action == "monitor") ContinuousTasks else ForegroundTasks).request(call, "start", input)
          } catch (failure: NativeStateException) { call.failure(failure.code)
          } catch (failure: PlatformFailure) { call.failure(failure.code)
          } catch (_: Exception) { call.failure("ABORT_ERR") }
        }
        NativeDeferredReply
      }
      return
    }
    NativeExtensionCall(call).runOnWorker { signal ->
      signal.throwIfCanceled()
      if (call.done.get()) throw NativeStateException("ABORT_ERR")
      when(action) {
        "approveAuthority" -> {
          require(options.keys().asSequence().all { it in setOf("workflow", "authorityRef", "accountRef", "targetRef", "origin", "basePath", "userInitiated") } &&
            options.opt("userInitiated") == true)
          val definition = policy(options.getString("workflow"))
          require(definition.approvedPath != null)
          val basePath = WorkflowPolicy.basePath(options.optString("basePath"))
          require(basePath.isEmpty() || definition.allowApprovedBasePath)
          val address = options.getString("origin")
          require(address.length in 1..512)
          val parsed = java.net.URI(address)
          require(parsed.scheme == "https" && parsed.host != null && parsed.rawUserInfo == null && parsed.rawQuery == null &&
            parsed.rawFragment == null && parsed.rawPath in listOf("", "/") && (parsed.port == -1 || parsed.port in 1..65535))
          val approved = java.net.URI("https", null, parsed.host.lowercase(java.util.Locale.ROOT),
            if (parsed.port == 443) -1 else parsed.port, null, null, null).toASCIIString()
          val approvedOptions = JSONObject(options.toString()).put("authorityHandle", UUID.randomUUID().toString())
          val value = identity(approvedOptions, origin).put("origin", approved).put("basePath", basePath).put("enabled", true)
          value.remove("authorityHandle")
          if (!NativeConsent.approve(call, signal, "Approve workflow server",
              "Allow workflow '" + definition.id + "' to read from:\n" + approved + basePath + definition.approvedPath + "\nOnly this fixed workflow can use the destination.")) throw NativeStateException("ABORT_ERR")
          signal.throwIfCanceled()
          if (call.done.get()) throw NativeStateException("ABORT_ERR")
          val id = UUID.randomUUID().toString()
          change(call.app, "authority", id) { target -> value.keys().forEach { target.put(it, value.get(it)) } }
          JSONObject().put("authorityHandle", id).put("origin", approved).put("basePath", basePath)
        }
        "importCredential" -> {
          require(options.keys().asSequence().all { it in setOf("workflow", "authorityRef", "accountRef", "targetRef", "authorityHandle", "expiresAt", "userInitiated") } &&
            options.opt("userInitiated") == true)
          val value = identity(options, origin); val definition = policy(value.getString("workflow"))
          val sourceKey = definition.credentialKey ?: throw NativeStateException("EINVAL")
          if (count(call.app, "credential") >= 16) throw NativeStateException("ENOSPC")
          val bytes = SecureStorageManager.get(call.app, origin, sourceKey)
            ?: throw NativeStateException("ENOENT")
          try {
            require(bytes.size in 1..4096 && bytes.all { it.toInt() in 33..126 })
            value.put("value", bytes.toString(Charsets.US_ASCII)).put("enabled", true).put("expiresAt", options.optLong("expiresAt", 0))
            require(value.getLong("expiresAt") == 0L || value.getLong("expiresAt") > System.currentTimeMillis())
            val id = UUID.randomUUID().toString()
            change(call.app, "credential", id) { target -> value.keys().forEach { target.put(it, value.get(it)) } }
            JSONObject().put("credentialHandle", id).put("valueExported", false)
          } finally { bytes.fill(0) }
        }
        "bind" -> {
          require(options.keys().asSequence().all { it in setOf("workflow", "authorityRef", "accountRef", "targetRef", "authorityHandle", "credentialHandle", "userInitiated") } &&
            options.opt("userInitiated") == true)
          val value = identity(options, origin)
          if (count(call.app, "binding") >= 8) throw NativeStateException("ENOSPC")
          if (policy(value.getString("workflow")).credentialKey != null) {
            val handle = identifier(options.getString("credentialHandle")); val credential = read(call.app, "credential", handle)
            scope(credential, value)
            if (!credential.getBoolean("enabled")) throw NativeStateException("EACCES")
            value.put("credentialHandle", handle)
          }
          val id = UUID.randomUUID().toString()
          value.put("enabled", true).put("generation", UUID.randomUUID().toString()).put("state", "idle").put("events", JSONObject())
          change(call.app, "binding", id) { target -> value.keys().forEach { target.put(it, value.get(it)) } }
          summary(call.app, id)
        }
        "discover" -> {
          require(options.keys().asSequence().all { it in setOf("workflow", "authorityRef", "accountRef", "targetRef") })
          val definition = policy(options.getString("workflow"))
          for (field in listOf("authorityRef", "accountRef", "targetRef")) identifier(options.getString(field))
          val result = JSONObject().put("bindings", JSONArray()).put("credentials", JSONArray()).put("authorities", JSONArray())
          val snapshot = store(call.app).read()
          try {
            for ((name, bytes) in snapshot.records) {
              val value = JSONObject(bytes.toString(Charsets.UTF_8))
              if (value.optString("documentOrigin") != origin || value.optString("workflow") != definition.id ||
                  listOf("authorityRef", "accountRef", "targetRef").any { value.optString(it) != options.getString(it) }) continue
              val id = name.substringAfter('.')
              when(name.substringBefore('.')) {
                "binding" -> result.getJSONArray("bindings").put(summary(call.app, id))
                "credential" -> result.getJSONArray("credentials").put(JSONObject().put("credentialHandle", id)
                  .put("enabled", value.optBoolean("enabled")).put("valueExported", false).put("authorityHandle", value.optString("authorityHandle")))
                "authority" -> result.getJSONArray("authorities").put(JSONObject().put("authorityHandle", id)
                  .put("enabled", value.optBoolean("enabled")).put("origin", value.getString("origin")).put("basePath", value.optString("basePath")))
              }
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          result
        }
        "status" -> {
          require(options.length() == 1); val id = identifier(options.getString("id"))
          owned(call.app, id, origin, false); summary(call.app, id)
        }
        "pushOptions" -> {
          require(options.length() == 1); val id = identifier(options.getString("id"))
          task(id, owned(call.app, id, origin)).apply { remove("operationRef") }
        }
        "schedule" -> {
          require(options.keys().asSequence().all { it in setOf("id", "intervalMs", "network", "requiresCharging", "requiresBatteryNotLow") })
          val id = identifier(options.getString("id")); val input = task(id, owned(call.app, id, origin))
          for (field in listOf("intervalMs", "network", "requiresCharging", "requiresBatteryNotLow"))
            if (options.has(field)) input.put(field, options.get(field))
          DeferredSync.request(call.app, "enqueue", input)
        }
        "stop" -> {
          require(options.length() == 1); val id = identifier(options.getString("id"))
          owned(call.app, id, origin, false)
          withGate(id) {
            change(call.app, "binding", id) { require(it.has("workflow")); it.put("enabled", false).put("generation", UUID.randomUUID().toString()) }
          }
          cancel(id)
          try { DeferredSync.request(call.app, "cancel", JSONObject().put("id", id)) }
          catch (failure: PlatformFailure) { if (failure.code !in setOf("ENOENT", "ENOSYS")) throw failure }
          summary(call.app, id)
        }
        "revokeCredential" -> {
          require(options.length() == 1); val id = identifier(options.getString("credentialHandle"))
          if (read(call.app, "credential", id).getString("documentOrigin") != origin) throw NativeStateException("EACCES")
          synchronized(this) { revoking.add(id) }
          change(call.app, "credential", id) { require(it.has("value")); it.put("enabled", false) }
          val snapshot = store(call.app).read()
          try {
            for ((name, bytes) in snapshot.records) if (name.startsWith("binding.") &&
              JSONObject(bytes.toString(Charsets.UTF_8)).optString("credentialHandle") == id) {
              val owner = name.removePrefix("binding.")
              withGate(owner) { /* Wait for a presentation already admitted before revocation. */ }
              cancel(owner)
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          synchronized(this) { revoking.remove(id) }
          JSONObject().put("revokedLocally", true).put("serverRevocationConfirmed", false)
        }
        "revokeAuthority" -> {
          require(options.length() == 1)
          val id = identifier(options.getString("authorityHandle"))
          if (read(call.app, "authority", id).getString("documentOrigin") != origin) throw NativeStateException("EACCES")
          change(call.app, "authority", id) { it.put("enabled", false) }
          val snapshot = store(call.app).read()
          try {
            for ((name, bytes) in snapshot.records) if (name.startsWith("binding.") &&
              JSONObject(bytes.toString(Charsets.UTF_8)).optString("authorityHandle") == id) {
              val owner = name.removePrefix("binding.")
              withGate(owner) { }
              cancel(owner)
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          JSONObject().put("revokedLocally", true).put("serverRevocationConfirmed", false)
        }
        "events" -> {
          require(options.length() == 1)
          val id = identifier(options.getString("id")); val value = owned(call.app, id, origin, false)
          val events = value.optJSONObject("events") ?: JSONObject()
          JSONArray(events.keys().asSequence().map { event ->
            JSONObject().put("eventHandle", event).put("notificationId", "workflow-$id-$event").put("outcome", events.getString(event))
          }.toList())
        }
        "releaseEvents" -> {
          require(options.keys().asSequence().all { it in setOf("id", "generation", "eventHandles") })
          val id = identifier(options.getString("id")); owned(call.app, id, origin, false)
          val handles = options.getJSONArray("eventHandles")
          require(handles.length() in 1..128)
          withGate(id) {
            synchronized(this) { if (active.containsKey(id)) throw NativeStateException("EBUSY") }
            change(call.app, "binding", id) { value ->
              if (value.getString("generation") != options.getString("generation")) throw NativeStateException("ESTALE")
              val events = value.getJSONObject("events")
              for (index in 0 until handles.length()) {
                val event = handles.getString(index); require(Regex("[a-f0-9]{64}").matches(event))
                if (events.optString(event) == "attempting") throw NativeStateException("EBUSY")
                events.remove(event)
              }
            }
          }
          summary(call.app, id)
        }
        "release" -> {
          require(options.keys().asSequence().all { it in setOf("id", "generation") })
          val id = identifier(options.getString("id"))
          withGate(id) {
            synchronized(this) { if (active.containsKey(id)) throw NativeStateException("EBUSY") }
            remove(call.app, "binding", id) { value ->
              if (value.getString("documentOrigin") != origin) throw NativeStateException("EACCES")
              if (value.getString("generation") != options.getString("generation")) throw NativeStateException("ESTALE")
              if (value.getBoolean("enabled") || value.getJSONObject("events").length() > 0) throw NativeStateException("EBUSY")
            }
          }
          JSONObject().put("released", true).put("serverRevocationConfirmed", false)
        }
        "releaseAuthority" -> {
          require(options.length() == 1)
          val id = identifier(options.getString("authorityHandle"))
          val snapshot = store(call.app).read()
          try {
            for ((name, bytes) in snapshot.records) if (name.startsWith("binding.") || name.startsWith("credential.")) {
              val owner = JSONObject(bytes.toString(Charsets.UTF_8))
              if (owner.optString("authorityHandle") == id) throw NativeStateException("EBUSY")
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          remove(call.app, "authority", id) { value ->
            if (value.getString("documentOrigin") != origin) throw NativeStateException("EACCES")
            if (value.getBoolean("enabled")) throw NativeStateException("EBUSY")
          }
          JSONObject().put("released", true).put("serverRevocationConfirmed", false)
        }
        "releaseCredential" -> {
          require(options.length() == 1)
          val id = identifier(options.getString("credentialHandle"))
          val snapshot = store(call.app).read()
          try {
            for ((name, bytes) in snapshot.records) if (name.startsWith("binding.")) {
              val owner = JSONObject(bytes.toString(Charsets.UTF_8))
              if (owner.optString("credentialHandle") == id &&
                  (owner.getBoolean("enabled") || synchronized(this) { active.containsKey(name.removePrefix("binding.")) }))
                throw NativeStateException("EBUSY")
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          remove(call.app, "credential", id) { value ->
            if (value.getString("documentOrigin") != origin) throw NativeStateException("EACCES")
            if (value.getBoolean("enabled")) throw NativeStateException("EBUSY")
          }
          JSONObject().put("released", true).put("serverRevocationConfirmed", false)
        }
        "responses" -> {
          require(options.length() == 1); val id = identifier(options.getString("id")); owned(call.app, id, origin)
          val values = JSONArray(NotificationReceipts.listNative(call.app))
          JSONArray((0 until values.length()).map { values.getJSONObject(it) }.filter { it.optString("targetRef").startsWith("$id:") }
            .map { JSONObject().put("receiptId", it.getString("receiptId")).put("eventHandle", it.getString("targetRef").substringAfter(':'))
              .put("notificationId", "workflow-$id-" + it.getString("targetRef").substringAfter(':'))
              .put("action", it.getString("action")).put("receivedAt", it.getLong("receivedAt")) })
        }
        else -> throw NativeStateException("ENOSYS")
      }
    }
  }
}

/** Native foreground destination approval; web content cannot click or suppress this dialog. */
internal object NativeConsent {
  private val active = java.util.concurrent.atomic.AtomicReference<Any?>(null)
  fun approve(call: PlatformCall, cancellation: android.os.CancellationSignal, title: String, message: String): Boolean {
    val result = java.util.concurrent.CompletableFuture<Boolean>()
    val dialog = java.util.concurrent.atomic.AtomicReference<androidx.appcompat.app.AlertDialog?>(null)
    val owner = Any()
    PlatformOperations.main.post {
      try {
        if (call.done.get() || cancellation.isCanceled) throw PlatformFailure("ABORT_ERR")
        val activity = call.foreground().activity as? AppActivity ?: throw PlatformFailure("ENOTREADY")
        if (!active.compareAndSet(null, owner)) throw PlatformFailure("EBUSY")
        val popup = androidx.appcompat.app.AlertDialog.Builder(activity).setTitle(title).setMessage(message)
          .setPositiveButton("Allow") { _, _ ->
            try {
              call.foreground()
              if (call.done.get() || cancellation.isCanceled) result.complete(false) else result.complete(true)
            } catch (_: Exception) { result.complete(false) }
          }.setNegativeButton("Cancel") { _, _ -> result.complete(false) }.create()
        dialog.set(popup)
        popup.setOnDismissListener { active.compareAndSet(owner, null); result.complete(false) }
        cancellation.setOnCancelListener {
          result.complete(false)
          PlatformOperations.main.post { popup.dismiss() }
        }
        if (call.done.get() || cancellation.isCanceled) {
          result.complete(false); active.compareAndSet(owner, null); popup.dismiss()
        } else popup.show()
      } catch (failure: PlatformFailure) {
        active.compareAndSet(owner, null)
        result.completeExceptionally(failure)
      } catch (_: Exception) {
        active.compareAndSet(owner, null)
        result.completeExceptionally(PlatformFailure("EIO"))
      }
    }
    try {
      return result.get()
    } catch (failure: java.util.concurrent.ExecutionException) {
      throw (failure.cause as? PlatformFailure ?: PlatformFailure("EIO"))
    } finally {
      cancellation.setOnCancelListener(null)
      PlatformOperations.main.post { dialog.get()?.dismiss(); active.compareAndSet(owner, null) }
    }
  }
}
