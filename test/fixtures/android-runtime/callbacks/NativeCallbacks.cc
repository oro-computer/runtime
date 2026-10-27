#include "runtime/native/android_platform.hh"
#include <jni.h>
#include <thread>
#include <mutex>
#include <atomic>

using namespace oro::runtime;
using namespace oro::runtime::native;
namespace {
  std::shared_ptr<AndroidPlatform> platform;
  std::mutex recordsMutex;
  std::atomic<bool> cleanupFailed = false;
  std::optional<PushRecoveryRegistration> recovery;
  RecordStore records () {
    return platform->openRecords ("product.public-callbacks", {"public-authority", "public-account", "public-device"});
  }
  int value (const RecordSnapshot& snapshot, const std::string& key) {
    const auto found = snapshot.records.find (key);
    return found == snapshot.records.end () ? 0
                                            : std::stoi (std::string (found->second.begin (), found->second.end ()));
  }
  void increment (const std::string& key) {
    std::lock_guard lock (recordsMutex);
    auto store = records ();
    const auto snapshot = store.read ();
    const auto bytes = std::to_string (value (snapshot, key) + 1);
    store.apply (snapshot.revision, {{key, RecordBytes (bytes.begin (), bytes.end ())}});
  }
  std::string state () {
    const auto snapshot = records ().read ();
    return "{\"initialized\":true,\"starts\":" + std::to_string (value (snapshot, "starts")) +
           ",\"ticks\":" + std::to_string (value (snapshot, "ticks")) +
           ",\"closed\":" + std::to_string (value (snapshot, "closed")) +
           ",\"deferred\":" + std::to_string (value (snapshot, "deferred")) +
           ",\"pushes\":" + std::to_string (value (snapshot, "pushes")) +
           ",\"authorizing\":" + std::to_string (value (snapshot, "authorizing")) +
           ",\"authorizationExited\":" + std::to_string (value (snapshot, "authorizationExited")) +
           ",\"cleanupFailed\":" + (cleanupFailed ? "true" : "false") + "}";
  }
  struct Execution : CallbackExecution {
    std::string mode;
    explicit Execution (std::string mode) : mode (std::move (mode)) {
    }
    ~Execution () override {
      try {
        increment ("closed");
      } catch (...) {
        cleanupFailed = true;
      }
    }
    CallbackOutcome run (const ExecutionCancellation& signal) override {
      increment ("starts");
      if (mode == "public.monitor") {
        while (!signal.cancelled ()) {
          increment ("ticks");
          std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        return CallbackOutcome::Unknown;
      }
      if (mode == "public.read" || mode == "public.cleanup")
        increment ("deferred");
      return CallbackOutcome::Completed;
    }
  };
}  // namespace
extern "C" void initialize_public_callbacks (Runtime* runtime) {
  platform = AndroidPlatform::fromRuntime (*runtime);
  auto authorize = [] (const ExecutionTask& task,
                       const ExecutionCancellation& signal) -> std::unique_ptr<CallbackExecution> {
    if (task.authorityRef != "authority" || task.accountRef != "account" || signal.cancelled ())
      return nullptr;
    return std::make_unique<Execution> (task.kind);
  };
  platform->registerExecution (CallbackType::Foreground, "public.complete", authorize);
  platform->registerExecution (CallbackType::Continuous, "public.monitor", authorize);
  platform->registerExecution (CallbackType::DeferredRead, "public.read", authorize);
  platform->registerExecution (CallbackType::Maintenance, "public.cleanup", authorize, {"cleanup"}, true);
  platform->registerExtension ("public.callbacks", [] (const auto& action, const auto&, const auto&) {
    if (action != "read")
      throw StateError ("EINVAL");
    platform->withApplication ([] (JNIEnv* env, jobject context) {
      const auto type = env->GetObjectClass (context);
      const auto method = env->GetMethodID (type, "getPackageName", "()Ljava/lang/String;");
      const auto name = static_cast<jstring> (env->CallObjectMethod (context, method));
      const auto bytes = name ? env->GetStringUTFChars (name, nullptr) : nullptr;
      const bool valid = bytes && std::string (bytes) == "oro.runtime.qualification.callbacks";
      if (bytes)
        env->ReleaseStringUTFChars (name, bytes);
      if (name)
        env->DeleteLocalRef (name);
      env->DeleteLocalRef (type);
      if (!valid)
        throw StateError ("EIO");
    });
    return state ();
  });
  platform->registerPush (
      "public.alert",
      {[] (const auto& task, const auto& signal) { return task.accountRef == "account" && !signal.cancelled (); },
       [] (const auto&, auto bytes, const auto& signal) {
         if (signal.cancelled () || std::string (bytes.begin (), bytes.end ()) != "public-message")
           return false;
         increment ("pushes");
         return true;
       },
       {}});
  platform->registerPush ("public.waiting-alert", {[] (const auto&, const auto& signal) {
                                                     increment ("authorizing");
                                                     while (!signal.cancelled ())
                                                       std::this_thread::sleep_for (std::chrono::milliseconds (5));
                                                     increment ("authorizationExited");
                                                     return false;
                                                   },
                                                   [] (const auto&, auto, const auto&) { return false; },
                                                   {}});
}

extern "C" JNIEXPORT jstring JNICALL
Java_oro_runtime_qualification_callbacks_PublicCallbackFixture_runPublicNativeCase (JNIEnv* env, jobject,
                                                                                    jstring operation) {
  const auto chars = env->GetStringUTFChars (operation, nullptr);
  const std::string mode = chars ? chars : "";
  if (chars)
    env->ReleaseStringUTFChars (operation, chars);
  std::string result;
  try {
    if (mode == "read")
      result = state ();
    else if (mode == "interop-outside") {
      try {
        platform->withApplication ([] (JNIEnv*, jobject) {});
        throw StateError ("EIO");
      } catch (const StateError& error) {
        if (std::string (error.what ()) != "EACCES")
          throw;
        result = "{\"outsideInteropDenied\":true}";
      }
    } else if (mode == "push-drain-stop") {
      const auto rows = platform->discoverPushRegistrations ({"authority", "account", "public.waiting-alert"});
      if (rows.size () != 1)
        throw StateError ("ENOENT");
      result = platform->unregisterPushIfCurrent ({"authority", "account", "public.waiting-alert"}, rows[0].id,
                                                  rows[0].revision)
                   ? "{\"authorizationStopSubmitted\":true}"
                   : "{\"authorizationStopSubmitted\":false}";
    } else if (mode == "recovery-discover") {
      const auto rows = platform->discoverPushRegistrations ({"authority", "account", "public.alert"});
      if (rows.size () != 1)
        throw StateError ("ENOENT");
      recovery = rows.front ();
      if (!platform->discoverPushRegistrations ({"authority", "different", "public.alert"}).empty ())
        throw StateError ("EACCES");
      result = "{\"redactedRecoveryPassed\":true}";
    } else if (mode == "recovery-stale") {
      if (!recovery)
        throw StateError ("ENOENT");
      bool refused = false;
      try {
        platform->unregisterPushIfCurrent ({"authority", "account", "public.alert"}, recovery->id, recovery->revision);
      } catch (const StateError& error) {
        refused = error.code == "EAGAIN";
      }
      result = refused ? "{\"staleRecoveryPassed\":true}" : "{\"staleRecoveryPassed\":false}";
    } else if (mode == "recovery-remove") {
      const auto rows = platform->discoverPushRegistrations ({"authority", "account", "public.alert"});
      if (rows.size () != 1)
        throw StateError ("ENOENT");
      if (!platform->unregisterPushIfCurrent ({"authority", "account", "public.alert"}, rows[0].id, rows[0].revision))
        throw StateError ("EIO");
      if (!platform->discoverPushRegistrations ({"authority", "account", "public.alert"}).empty ())
        throw StateError ("EIO");
      result = "{\"recoveryCleanupPassed\":true}";
    } else if (mode == "key-lifetime") {
      auto store = records ();
      platform->shutdown ();
      platform->drain ();
      bool refused = false;
      try {
        store.read ();
      } catch (const StateError& error) {
        refused = error.code == "EACCES";
      }
      result = refused ? "{\"keyLifetimePassed\":true}" : "{\"keyLifetimePassed\":false}";
    } else
      throw StateError ("EINVAL");
  } catch (const StateError& error) {
    result = "{\"error\":\"" + error.code + "\"}";
  } catch (...) {
    result = "{\"error\":\"EIO\"}";
  }
  return env->NewStringUTF (result.c_str ());
}
