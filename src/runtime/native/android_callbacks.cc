#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_ANDROID
#include "android_platform.hh"
#include "../app.hh"
#include "../platform/android.hh"
#include <sodium.h>

extern "C" void oro_runtime_native_initialize (oro::runtime::Runtime*) __attribute__ ((weak));

namespace {
  using namespace oro::runtime;
  using namespace oro::runtime::native;
  std::string string (const JSON::Object& value, const std::string& key, size_t limit) {
    if (!value.has (key) || value.get (key).type != JSON::Type::String)
      throw StateError ("EINVAL");
    const auto result = value.get (key).as<JSON::String> ().value ();
    if (result.size () > limit || result.find ('\0') != std::string::npos)
      throw StateError ("EINVAL");
    return result;
  }
  std::string outcome (CallbackOutcome value) {
    switch (value) {
      case CallbackOutcome::Completed:
        return "completed";
      case CallbackOutcome::Retry:
        return "retry";
      case CallbackOutcome::Conflict:
        return "conflict";
      case CallbackOutcome::Unauthorized:
        return "unauthorized";
      case CallbackOutcome::Unknown:
        return "unknown";
    }
    throw StateError ("EIO");
  }
  bool fixedCode (const std::string& value) {
    return !value.empty () && value.size () <= 64 && value.front () >= 'A' && value.front () <= 'Z' &&
           value.find_first_not_of ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos;
  }
  JSON::Any dispatch (JNIEnv* env, jobject self, const std::string& command, jlong handle, jint category,
                      const std::string& source, jbyteArray data) {
    const auto app = app::App::sharedApplication ();
    if (!app || app->runtime.userConfig["android_native_callbacks_enabled"] != "true")
      throw StateError ("EACCES");
    const auto backend = AndroidPlatform::fromRuntime (app->runtime);
    if (command == "initialize") {
      if (!oro_runtime_native_initialize)
        throw StateError ("ENOTREADY");
      backend->bind (env, self);
      oro_runtime_native_initialize (&app->runtime);
      JSON::Array descriptors;
      for (const auto& value : backend->seal ()) {
        JSON::Array operations;
        for (const auto& operation : value.operations)
          operations.push (operation);
        descriptors.push (JSON::Object::Entries{
            {"type", static_cast<int> (value.type)}, {"kind", value.kind}, {"operations", operations}});
      }
      return descriptors;
    }
    if (command == "begin") {
      if (category < 0 || category > static_cast<int> (CallbackType::Extension))
        throw StateError ("EINVAL");
      const auto task = JSON::parse (source).as<JSON::Object> ();
      return static_cast<int64_t> (backend->begin (
          static_cast<CallbackType> (category),
          {string (task, "id", 128), string (task, "kind", 64), string (task, "authorityRef", 128),
           string (task, "accountRef", 128), string (task, "targetRef", 128), string (task, "operationRef", 128)}));
    }
    if (handle <= 0)
      throw StateError ("EINVAL");
    if (command == "cancel") {
      backend->cancel (handle);
      return nullptr;
    }
    if (command == "close") {
      backend->close (handle);
      return nullptr;
    }
    if (command == "authorize")
      return backend->authorize (handle);
    if (command == "run")
      return outcome (backend->run (handle));
    if (command == "extension") {
      const auto input = JSON::parse (source).as<JSON::Object> ();
      if (!input.has ("payload") || input.get ("payload").type != JSON::Type::Object)
        throw StateError ("EINVAL");
      return JSON::parse (backend->extension (handle, string (input, "action", 64), input.get ("payload").str ()));
    }
    if (command == "push-authorize")
      return backend->push (handle, "authorize");
    if (command == "push-enrollment") {
      if (JSON::parse (source).type != JSON::Type::Object)
        throw StateError ("EINVAL");
      return backend->push (handle, "enrollment", {}, source);
    }
    if (command == "push-message") {
      const auto size = data ? env->GetArrayLength (data) : 0;
      if (size > 4096)
        throw StateError ("E2BIG");
      RecordBytes bytes (size);
      struct Erase {
        RecordBytes& bytes;
        ~Erase () {
          sodium_memzero (bytes.data (), bytes.size ());
        }
      } erase{bytes};
      if (size)
        env->GetByteArrayRegion (data, 0, size, reinterpret_cast<jbyte*> (bytes.data ()));
      if (env->ExceptionCheck ()) {
        env->ExceptionClear ();
        throw StateError ("EIO");
      }
      return backend->push (handle, "message", bytes);
    }
    throw StateError ("EINVAL");
  }
}  // namespace
extern "C" jstring ANDROID_EXTERNAL (app, RuntimeNativeCallbacks,
                                     nativeCall) (JNIEnv* env, jobject self, jstring command, jlong handle,
                                                  jint category, jstring input, jbyteArray bytes) {
  JSON::Object response;
  try {
    if (!command || !input || env->GetStringLength (command) > 32 || env->GetStringLength (input) > 196608)
      throw StateError ("E2BIG");
    const auto source = android::StringWrap (env, input).str ();
    if (source.size () > 196608)
      throw StateError ("E2BIG");
    response["data"] = dispatch (env, self, android::StringWrap (env, command).str (), handle, category, source, bytes);
  } catch (const StateError& error) {
    response["err"] = JSON::Object::Entries{{"code", fixedCode (error.code) ? error.code : "EIO"}};
  } catch (...) {
    response["err"] = JSON::Object::Entries{{"code", "EIO"}};
  }
  return android::StringWrap (env, response.str ()).j_str ();
}
#endif
