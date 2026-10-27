#include "push_journal.hh"
#if ORO_RUNTIME_PLATFORM_ANDROID
#include "../app.hh"
#include "../platform/android.hh"

using namespace oro::runtime;

extern "C" jstring ANDROID_EXTERNAL (app, NativePushJournal, control) (JNIEnv* env, jobject self, jstring source,
                                                                       jbyteArray bytes) {
  JSON::Object result;
  try {
    if (!app::App::sharedApplication () || !source || !bytes || env->GetStringLength (source) > 4096)
      throw native::StateError ("EINVAL");
    const auto size = env->GetArrayLength (bytes);
    if (size < 0 || static_cast<size_t> (size) > native::PushJournalLedger::maximumArchiveBytes)
      throw native::StateError ("ENOSPC");
    auto type = env->GetObjectClass (self);
    if (!type)
      throw native::StateError ("EIO");
    const auto directory = env->GetMethodID (type, "directory", "()Ljava/lang/String;");
    if (!directory || env->ExceptionCheck ()) {
      env->DeleteLocalRef (type);
      throw native::StateError ("ENOTREADY");
    }
    const auto identity = env->GetMethodID (type, "applicationId", "()Ljava/lang/String;");
    env->DeleteLocalRef (type);
    if (!directory || !identity || env->ExceptionCheck ())
      throw native::StateError ("ENOTREADY");
    auto path = static_cast<jstring> (env->CallObjectMethod (self, directory));
    if (!path || env->ExceptionCheck ()) {
      if (path)
        env->DeleteLocalRef (path);
      throw native::StateError ("EIO");
    }
    auto application = static_cast<jstring> (env->CallObjectMethod (self, identity));
    if (!application || env->ExceptionCheck ()) {
      env->DeleteLocalRef (path);
      if (application)
        env->DeleteLocalRef (application);
      throw native::StateError ("EIO");
    }
    const auto directoryPath = android::StringWrap (env, path).str ();
    const auto applicationId = android::StringWrap (env, application).str ();
    env->DeleteLocalRef (path);
    env->DeleteLocalRef (application);
    if (!std::filesystem::path (directoryPath).is_absolute ())
      throw native::StateError ("EINVAL");
    native::PushJournalLedger ledger (std::filesystem::path (directoryPath) / "selector-v1.sqlite", applicationId);
    const auto options = JSON::parse (android::StringWrap (env, source).str ()).as<JSON::Object> ();
    const auto text = [&] (const char* name) {
      if (!options.has (name) || options.get (name).type != JSON::Type::String)
        throw native::StateError ("EINVAL");
      return options.get (name).as<JSON::String> ().value ();
    };
    const auto boolean = [&] (const char* name) {
      if (!options.has (name) || options.get (name).type != JSON::Type::Boolean)
        throw native::StateError ("EINVAL");
      return options.get (name).as<JSON::Boolean> ().value ();
    };
    const auto action = text ("action");
    if (action == "epoch") {
      result["data"] = JSON::Object::Entries{{"epoch", ledger.epoch ()}};
    } else {
      native::RecordBytes archive (size);
      if (size)
        env->GetByteArrayRegion (bytes, 0, size, reinterpret_cast<jbyte*> (archive.data ()));
      if (env->ExceptionCheck ())
        throw native::StateError ("EIO");
      native::PushJournalSnapshot snapshot{text ("epoch"), text ("reason"), std::move (archive)};
      if (action == "inspect") {
        const auto state = ledger.inspect (snapshot);
        result["data"] = JSON::Object::Entries{{"revision", state.revision},
                                               {"reason", state.reason},
                                               {"readable", state.readable},
                                               {"recoverable", state.recoverable},
                                               {"scope", "installation"}};
      } else if (action == "quarantine") {
        const auto receipt =
            ledger.quarantine (snapshot,
                               {text ("expectedRevision"), text ("recoveryId"), boolean ("installationScopeConfirmed"),
                                boolean ("archiveAndStopLocalDeliveryConfirmed")},
                               boolean ("idle"));
        result["data"] =
            JSON::Object::Entries{{"recoveryId", receipt.recoveryId},  {"epoch", receipt.epoch},
                                  {"changed", receipt.changed},        {"archived", true},
                                  {"localJournalReplaced", true},      {"remoteUnregisterAttempted", false},
                                  {"serverRevocationConfirmed", false}};
      } else
        throw native::StateError ("EINVAL");
    }
  } catch (const native::StateError& error) {
    if (env->ExceptionCheck ())
      env->ExceptionClear ();
    result["error"] = error.code;
  } catch (...) {
    if (env->ExceptionCheck ())
      env->ExceptionClear ();
    result["error"] = "EIO";
  }
  return android::StringWrap (env, result.str ()).j_str ();
}
#endif
