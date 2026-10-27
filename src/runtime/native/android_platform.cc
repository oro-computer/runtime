#include "android_platform.hh"
#if ORO_RUNTIME_PLATFORM_ANDROID
#include "../app.hh"
#include "../platform/android.hh"
#include "unicode.hh"
#include "service_values.hh"
#include <sodium.h>

namespace oro::runtime::native {
  struct AndroidPlatform::Binding : RecordKeyStore {
    JavaVM* jvm = nullptr;
    jobject object = nullptr;
    jmethodID key = nullptr;
    jmethodID recovery = nullptr;
    jmethodID application = nullptr;
    jmethodID services = nullptr;
    jclass signalClass = nullptr;
    jmethodID signalConstructor = nullptr;
    jmethodID signalCancel = nullptr;
    std::string directory;
    std::mutex mutex;
    bool active = true;
    ~Binding () {
      if (jvm && object) {
        android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
        if (attachment.env) {
          attachment.env->DeleteGlobalRef (object);
          if (signalClass)
            attachment.env->DeleteGlobalRef (signalClass);
        }
      }
    }
    RecordBytes access (int action, const std::string& scope, char slot, const RecordKey* value = nullptr) {
      std::lock_guard lock (mutex);
      if (!active)
        throw StateError ("EACCES");
      android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
      auto env = attachment.env;
      if (!env)
        throw StateError ("ENOTREADY");
      auto input = env->NewByteArray (value ? value->size () : 0);
      if (!input || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        if (input)
          env->DeleteLocalRef (input);
        throw StateError ("ENOMEM");
      }
      if (value)
        env->SetByteArrayRegion (input, 0, value->size (), reinterpret_cast<const jbyte*> (value->data ()));
      auto name = env->NewStringUTF (scope.c_str ());
      if (!name || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        env->DeleteLocalRef (input);
        if (name)
          env->DeleteLocalRef (name);
        throw StateError ("ENOMEM");
      }
      auto output =
          static_cast<jbyteArray> (env->CallObjectMethod (object, key, action, name, static_cast<jint> (slot), input));
      const bool failed = env->ExceptionCheck ();
      if (failed)
        env->ExceptionClear ();
      env->DeleteLocalRef (name);
      if (value) {
        RecordKey zeros{};
        env->SetByteArrayRegion (input, 0, zeros.size (), reinterpret_cast<const jbyte*> (zeros.data ()));
      }
      env->DeleteLocalRef (input);
      if (failed || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        if (output)
          env->DeleteLocalRef (output);
        throw StateError ("EACCES");
      }
      RecordBytes bytes;
      if (output) {
        const auto size = env->GetArrayLength (output);
        if (size != 0 && size != 32) {
          env->DeleteLocalRef (output);
          throw StateError ("EKEYREVOKED");
        }
        bytes.resize (size);
        if (size)
          env->GetByteArrayRegion (output, 0, size, reinterpret_cast<jbyte*> (bytes.data ()));
        RecordKey zeros{};
        if (size)
          env->SetByteArrayRegion (output, 0, size, reinterpret_cast<const jbyte*> (zeros.data ()));
        env->DeleteLocalRef (output);
      }
      return bytes;
    }
    std::string service (const std::string& feature, const std::string& action, const std::string& options,
                         const ExecutionCancellation* cancellation) {
      {
        std::lock_guard lock (mutex);
        if (!active)
          throw StateError ("EACCES");
      }
      if (cancellation && cancellation->cancelled ())
        throw StateError ("ABORT_ERR");
      android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
      const auto env = attachment.env;
      if (!env)
        throw StateError ("ENOTREADY");
      if (env->PushLocalFrame (16) != JNI_OK) {
        env->ExceptionClear ();
        throw StateError ("ENOMEM");
      }
      struct Frame {
        JNIEnv* env;
        ~Frame () { env->PopLocalFrame (nullptr); }
      } frame{env};
      JSON::Object input = JSON::Object::Entries{{"service", feature}, {"action", action},
                                                 {"options", JSON::parse (options).as<JSON::Object> ()}};
      const auto text = nativeUtf16 (input.str ());
      const auto source = env->NewString (reinterpret_cast<const jchar*> (text.data ()), text.size ());
      if (!source || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        throw StateError ("ENOMEM");
      }
      struct Signal {
        JavaVM* jvm = nullptr;
        jobject object = nullptr;
        jmethodID cancel = nullptr;
        ~Signal () {
          android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
          if (attachment.env && object)
            attachment.env->DeleteGlobalRef (object);
        }
        void stop () {
          android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
          if (attachment.env) {
            attachment.env->CallVoidMethod (object, cancel);
            if (attachment.env->ExceptionCheck ())
              attachment.env->ExceptionClear ();
          }
        }
      };
      std::shared_ptr<Signal> signal;
      CancellationSubscription subscription;
      if (cancellation) {
        const auto local = env->NewObject (signalClass, signalConstructor);
        if (!local || env->ExceptionCheck ()) {
          env->ExceptionClear ();
          throw StateError ("ENOMEM");
        }
        signal = std::make_shared<Signal> ();
        signal->jvm = jvm;
        signal->cancel = signalCancel;
        signal->object = env->NewGlobalRef (local);
        if (!signal->object || env->ExceptionCheck ()) {
          env->ExceptionClear ();
          throw StateError ("ENOMEM");
        }
        subscription = cancellation->onCancel ([signal] { signal->stop (); });
      }
      auto output = static_cast<jstring> (env->CallObjectMethod (object, services, source, signal ? signal->object : nullptr));
      if (env->ExceptionCheck () || !output) {
        env->ExceptionClear ();
        throw StateError ("EIO");
      }
      const auto size = env->GetStringLength (output);
      if (size > 2097152)
        throw StateError ("E2BIG");
      const auto chars = env->GetStringChars (output, nullptr);
      if (!chars || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        throw StateError ("ENOMEM");
      }
      std::string serialized;
      try {
        serialized = nativeUtf8 ({reinterpret_cast<const char16_t*> (chars), static_cast<size_t> (size)});
      } catch (...) {
        env->ReleaseStringChars (output, chars);
        throw;
      }
      env->ReleaseStringChars (output, chars);
      const auto result = JSON::parse (serialized).as<JSON::Object> ();
      if (result.has ("error"))
        throw StateError (result.get ("error").as<JSON::String> ().value ());
      return result.get ("data").str ();
    }
    std::optional<RecordKey> get (const std::string& scope, char slot) override {
      auto bytes = access (0, scope, slot);
      if (bytes.empty ())
        return std::nullopt;
      RecordKey value;
      std::copy (bytes.begin (), bytes.end (), value.begin ());
      sodium_memzero (bytes.data (), bytes.size ());
      return value;
    }
    void put (const std::string& scope, char slot, const RecordKey& value) override {
      access (1, scope, slot, &value);
    }
    void remove (const std::string& scope, char slot) override {
      access (2, scope, slot);
    }
    JSON::Any recover (const PushRecoveryScope& scope, const std::string& id = "", const std::string& revision = "") {
      validatePushRecoveryScope (scope);
      if (id.size () > 128 || revision.size () > 36)
        throw StateError ("EINVAL");
      return request (JSON::Object::Entries{{"authorityRef", scope.authorityRef},
                                            {"accountRef", scope.accountRef},
                                            {"kind", scope.kind},
                                            {"id", id},
                                            {"revision", revision}});
    }
    JSON::Any request (const JSON::Object& options) {
      std::lock_guard lock (mutex);
      if (!active)
        throw StateError ("EACCES");
      android::JNIEnvironmentAttachment attachment (jvm, JNI_VERSION_1_6);
      auto env = attachment.env;
      if (!env)
        throw StateError ("ENOTREADY");
      const auto input = options.str ();
      jstring inputReference = nullptr;
      jstring output = nullptr;
      {
        auto text = android::StringWrap (env, input);
        inputReference = text.j_str ();
        if (!inputReference)
          throw StateError ("ENOMEM");
        output = static_cast<jstring> (env->CallObjectMethod (object, recovery, inputReference));
      }
      env->DeleteLocalRef (inputReference);
      if (env->ExceptionCheck () || !output) {
        env->ExceptionClear ();
        if (output)
          env->DeleteLocalRef (output);
        throw StateError ("EIO");
      }
      const auto result = android::StringWrap (env, output).str ();
      env->DeleteLocalRef (output);
      const auto value = JSON::parse (result).as<JSON::Object> ();
      if (value.has ("error"))
        throw StateError (value.get ("error").as<JSON::String> ().value ());
      return value.get ("data");
    }
  };
  AndroidPlatform::AndroidPlatform (Runtime& runtime) : runtime (runtime) {
  }
  AndroidPlatform::~AndroidPlatform () {
    shutdown ();
  }
  std::shared_ptr<AndroidPlatform> AndroidPlatform::fromRuntime (Runtime& runtime) {
    const auto app = app::App::sharedApplication ();
    if (!app || &app->runtime != &runtime)
      throw StateError ("EACCES");
    return runtime.services.platform.nativeAndroid ();
  }
  void AndroidPlatform::bind (JNIEnv* env, jobject object) {
    std::lock_guard lock (bindingMutex);
    if (binding)
      throw StateError ("EEXIST");
    auto next = std::make_shared<Binding> ();
    if (env->GetJavaVM (&next->jvm) != JNI_OK)
      throw StateError ("ENOTREADY");
    next->object = env->NewGlobalRef (object);
    if (!next->object || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOMEM");
    }
    auto type = env->GetObjectClass (object);
    if (!type || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOTREADY");
    }
    struct ClassReference {
      JNIEnv* env;
      jclass type;
      ~ClassReference () {
        env->DeleteLocalRef (type);
      }
    } reference{env, type};
    const auto method = [&] (const char* name, const char* signature) {
      const auto result = env->GetMethodID (type, name, signature);
      if (!result || env->ExceptionCheck ()) {
        env->ExceptionClear ();
        throw StateError ("ENOTREADY");
      }
      return result;
    };
    next->key = method ("recordKey", "(ILjava/lang/String;I[B)[B");
    next->recovery = method ("recoverPush", "(Ljava/lang/String;)Ljava/lang/String;");
    next->application = method ("applicationContext", "()Landroid/content/Context;");
    next->services = method ("nativeServices", "(Ljava/lang/String;Landroid/os/CancellationSignal;)Ljava/lang/String;");
    const auto localSignal = env->FindClass ("android/os/CancellationSignal");
    if (!localSignal || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOTREADY");
    }
    next->signalClass = static_cast<jclass> (env->NewGlobalRef (localSignal));
    env->DeleteLocalRef (localSignal);
    if (!next->signalClass || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOMEM");
    }
    next->signalConstructor = env->GetMethodID (next->signalClass, "<init>", "()V");
    if (!next->signalConstructor || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOTREADY");
    }
    next->signalCancel = env->GetMethodID (next->signalClass, "cancel", "()V");
    if (!next->signalCancel || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("ENOTREADY");
    }
    const auto directory = method ("recordDirectory", "()Ljava/lang/String;");
    auto path = static_cast<jstring> (env->CallObjectMethod (object, directory));
    if (env->ExceptionCheck () || !path) {
      env->ExceptionClear ();
      throw StateError ("ENOTREADY");
    }
    next->directory = android::StringWrap (env, path).str ();
    env->DeleteLocalRef (path);
    if (!std::filesystem::path (next->directory).is_absolute ())
      throw StateError ("EINVAL");
    binding = std::move (next);
  }
  RecordStore AndroidPlatform::openRecords (const std::string& name, const RecordIdentity& identity) {
    std::lock_guard lock (bindingMutex);
    if (closing)
      throw StateError ("ESHUTDOWN");
    if (!binding)
      throw StateError ("ENOTREADY");
    if (!name.starts_with ("product."))
      throw StateError ("EINVAL");
    return RecordStore (std::filesystem::path (binding->directory) / "native-records-v2.sqlite", binding, name,
                        identity);
  }
  std::vector<PushRecoveryRegistration> AndroidPlatform::discoverPushRegistrations (const PushRecoveryScope& scope) {
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    const auto values = access->recover (scope).as<JSON::Array> ();
    std::vector<PushRecoveryRegistration> result;
    for (const auto& item : values) {
      const auto value = item.as<JSON::Object> ();
      result.push_back ({value.get ("id").as<JSON::String> ().value (), value.get ("kind").as<JSON::String> ().value (),
                         value.get ("phase").as<JSON::String> ().value (),
                         value.get ("revision").as<JSON::String> ().value ()});
    }
    return result;
  }
  bool AndroidPlatform::unregisterPushIfCurrent (const PushRecoveryScope& scope, const std::string& id,
                                                 const std::string& revision) {
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    if (id.empty ())
      throw StateError ("EINVAL");
    return access->recover (scope, id, revision).as<JSON::Boolean> ().value ();
  }
  void AndroidPlatform::shutdown () {
    {
      std::lock_guard lock (bindingMutex);
      closing = true;
    }
    CallbackRegistry::shutdown ();
  }
  PushJournalInspection AndroidPlatform::inspectPushJournalRecovery () {
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    const auto result = access->request (JSON::Object::Entries{{"action", "inspectJournal"}}).as<JSON::Object> ();
    return {result.get ("revision").as<JSON::String> ().value (), result.get ("reason").as<JSON::String> ().value (),
            result.get ("readable").as<JSON::Boolean> ().value (),
            result.get ("recoverable").as<JSON::Boolean> ().value ()};
  }
  PushJournalReceipt AndroidPlatform::quarantineUnreadablePushJournal (const PushJournalConsent& consent) {
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    const auto result = access
                            ->request (JSON::Object::Entries{
                                {"action", "quarantineJournal"},
                                {"expectedRevision", consent.expectedRevision},
                                {"recoveryId", consent.recoveryId},
                                {"installationScopeConfirmed", consent.installationScopeConfirmed},
                                {"archiveAndStopLocalDeliveryConfirmed", consent.archiveAndStopLocalDeliveryConfirmed}})
                            .as<JSON::Object> ();
    return {result.get ("recoveryId").as<JSON::String> ().value (), result.get ("epoch").as<JSON::String> ().value (),
            result.get ("changed").as<JSON::Boolean> ().value ()};
  }
  std::function<std::string (const std::string&, const std::string&, const std::string&, const ExecutionCancellation*)>
  AndroidPlatform::serviceAccess () {
    const auto owner = invocationGuard ();
    if (!owner ())
      throw StateError ("EACCES");
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    return [access, owner] (const auto& feature, const auto& action, const auto& input, const auto* cancellation) {
      if (!owner ())
        throw StateError ("EACCES");
      return access->service (feature, action, input, cancellation);
    };
  }
  NativeHttpsResponse AndroidPlatform::https (const NativeHttpsRequest& request, const ExecutionCancellation& signal) {
    const auto owner = invocationSignal ();
    if (owner.cancelled () || signal.cancelled ())
      throw StateError ("ABORT_ERR");
    const auto linked = std::make_shared<ExecutionCancellation> ();
    auto parent = owner.onCancel ([linked] { linked->cancel (); });
    auto requested = signal.onCancel ([linked] { linked->cancel (); });
    JSON::Object headers;
    for (const auto& [name, value] : request.headers)
      headers[name] = value;
    JSON::Object input = JSON::Object::Entries{{"url", request.url}, {"method", request.method}, {"headers", headers},
      {"body", request.body ? JSON::Any (service::bytes (*request.body)) : JSON::Any (nullptr)},
      {"timeoutMs", request.timeoutMs}, {"maximumResponseBytes", request.maximumResponseBytes}};
    const auto result = JSON::parse (serviceAccess () ("https", "request", input.str (), linked.get ())).as<JSON::Object> ();
    return {static_cast<int> (result.get ("status").as<JSON::Number> ().value ()),
            service::bytes (result.get ("body").as<JSON::String> ().value ())};
  }
  std::string AndroidPlatform::submitNotification (const std::string& options, const std::string& targetRef) {
    const auto owner = invocationSignal ();
    if (owner.cancelled ())
      throw StateError ("ABORT_ERR");
    JSON::Object input = JSON::Object::Entries{{"presentation", JSON::parse (options).as<JSON::Object> ()}, {"targetRef", targetRef}};
    return serviceAccess () ("notifications", "submit", input.str (), &owner);
  }
  std::string AndroidPlatform::configureNotificationChannels (const std::string& channels) {
    const auto owner = invocationSignal ();
    if (owner.cancelled ())
      throw StateError ("ABORT_ERR");
    JSON::Object input = JSON::Object::Entries{{"channels", JSON::parse (channels).as<JSON::Array> ()}};
    return serviceAccess () ("notifications", "configure", input.str (), &owner);
  }
  std::string AndroidPlatform::notificationStatus (const std::vector<std::string>& channels) {
    JSON::Array values;
    for (const auto& id : channels)
      values.push (id);
    JSON::Object input = JSON::Object::Entries{{"channels", values}};
    return serviceAccess () ("notifications", "status", input.str (), nullptr);
  }
  std::string AndroidPlatform::activeNotifications () {
    return serviceAccess () ("notifications", "active", "{}", nullptr);
  }
  std::string AndroidPlatform::notificationResponses () {
    return serviceAccess () ("notifications", "responses", "{}", nullptr);
  }
  bool AndroidPlatform::acknowledgeNotificationResponse (const std::string& id) {
    JSON::Object input = JSON::Object::Entries{{"receiptId", id}};
    return JSON::parse (serviceAccess () ("notifications", "acknowledge", input.str (), nullptr)).as<JSON::Boolean> ().value ();
  }
  void AndroidPlatform::cancelNotification (const std::string& id, const std::string& tag) {
    JSON::Object input = JSON::Object::Entries{{"id", id}, {"tag", tag}};
    serviceAccess () ("notifications", "cancel", input.str (), nullptr);
  }
  AndroidV1Records AndroidPlatform::openV1Records (const std::string& name, const RecordIdentity& identity) {
    const auto access = serviceAccess ();
    return AndroidV1Records ([access, name, identity] (const std::string& action, const std::string& options) {
      JSON::Object input = JSON::Object::Entries{{"name", name}, {"identity", JSON::Object::Entries{
        {"authority", identity.authority}, {"account", identity.account}, {"identity", identity.identity}}},
        {"input", JSON::parse (options).as<JSON::Object> ()}};
      return access ("records.v1", action, input.str (), nullptr);
    });
  }
  void AndroidPlatform::withApplication (const std::function<void (JNIEnv*, jobject)>& work) {
    if (!work || !ownsInvocationThread ())
      throw StateError ("EACCES");
    std::shared_ptr<Binding> access;
    {
      std::lock_guard lock (bindingMutex);
      access = binding;
    }
    if (!access)
      throw StateError ("ENOTREADY");
    {
      std::lock_guard lock (access->mutex);
      if (!access->active)
        throw StateError ("EACCES");
    }
    android::JNIEnvironmentAttachment attachment (access->jvm, JNI_VERSION_1_6);
    auto env = attachment.env;
    if (!env)
      throw StateError ("ENOTREADY");
    auto application = env->CallObjectMethod (access->object, access->application);
    if (!application || env->ExceptionCheck ()) {
      env->ExceptionClear ();
      if (application)
        env->DeleteLocalRef (application);
      throw StateError ("EIO");
    }
    struct Local {
      JNIEnv* env;
      jobject object;
      ~Local () {
        env->DeleteLocalRef (object);
      }
    } reference{env, application};
    try {
      work (env, application);
    } catch (...) {
      if (env->ExceptionCheck ())
        env->ExceptionClear ();
      throw;
    }
    if (env->ExceptionCheck ()) {
      env->ExceptionClear ();
      throw StateError ("EIO");
    }
  }
  void AndroidPlatform::drain () {
    CallbackRegistry::drain ();
    std::lock_guard lock (bindingMutex);
    if (closing && binding) {
      std::lock_guard access (binding->mutex);
      binding->active = false;
    }
  }
}  // namespace oro::runtime::native
#endif
