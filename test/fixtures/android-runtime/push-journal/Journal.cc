#include "runtime/native/android_platform.hh"
#include <atomic>
#include <thread>

using namespace oro::runtime;
using namespace oro::runtime::native;
namespace {
  std::shared_ptr<AndroidPlatform> platform;
  std::atomic<bool> entered = false;
  std::atomic<bool> observedCancellation = false;
  std::atomic<bool> released = false;
}  // namespace
extern "C" void initialize_public_journal (Runtime* runtime) {
  platform = AndroidPlatform::fromRuntime (*runtime);
  platform->registerPush ("public.wait", {[] (const auto&, const auto& signal) {
                                            entered = true;
                                            while (!released) {
                                              if (signal.cancelled ())
                                                observedCancellation = true;
                                              std::this_thread::sleep_for (std::chrono::milliseconds (2));
                                            }
                                            return false;
                                          },
                                          [] (const auto&, auto, const auto&) { return false; },
                                          {}});
}
extern "C" JNIEXPORT jstring JNICALL Java_oro_runtime_qualification_pushjournal_PublicJournalFixture_run (
    JNIEnv* env, jobject, jstring action, jstring payload) {
  const auto text = [&] (jstring value) {
    const auto bytes = env->GetStringUTFChars (value, nullptr);
    const std::string result = bytes ? bytes : "";
    if (bytes)
      env->ReleaseStringUTFChars (value, bytes);
    return result;
  };
  std::string result;
  try {
    const auto operation = text (action);
    if (operation == "inspect") {
      const auto state = platform->inspectPushJournalRecovery ();
      result = "{\"revision\":\"" + state.revision + "\",\"reason\":\"" + state.reason +
               "\",\"readable\":" + (state.readable ? "true" : "false") +
               ",\"recoverable\":" + (state.recoverable ? "true" : "false") + "}";
    } else if (operation == "quarantine") {
      // Public fixed fixture payload: revision|recoveryId|both explicit consent bits.
      const auto input = text (payload);
      const auto first = input.find ('|');
      const auto second = input.find ('|', first + 1);
      if (first == std::string::npos || second == std::string::npos)
        throw StateError ("EINVAL");
      const auto consent = input.substr (second + 1) == "confirmed";
      const auto receipt = platform->quarantineUnreadablePushJournal (
          {input.substr (0, first), input.substr (first + 1, second - first - 1), consent, consent});
      result = std::string ("{\"changed\":") + (receipt.changed ? "true" : "false") + "}";
    } else if (operation == "busy-status") {
      result = std::string ("{\"entered\":") + (entered ? "true" : "false") +
               ",\"cancelObserved\":" + (observedCancellation ? "true" : "false") + "}";
    } else if (operation == "release") {
      released = true;
      result = "{\"released\":true}";
    } else
      throw StateError ("EINVAL");
  } catch (const StateError& error) {
    result = "{\"error\":\"" + error.code + "\"}";
  } catch (...) {
    result = "{\"error\":\"EIO\"}";
  }
  return env->NewStringUTF (result.c_str ());
}
