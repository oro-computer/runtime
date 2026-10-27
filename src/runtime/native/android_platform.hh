#ifndef ORO_RUNTIME_NATIVE_ANDROID_PLATFORM_H
#define ORO_RUNTIME_NATIVE_ANDROID_PLATFORM_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_ANDROID
#include "callbacks.hh"
#include "push_recovery.hh"
#include "push_journal.hh"
#include "android_services.hh"
#include <jni.h>
#include <mutex>

namespace oro::runtime {
  class Runtime;
}
namespace oro::runtime::native {
  /** Process-owned compiled callbacks. Kotlin adapters own Android admission, scheduling and lifecycle. */
  class AndroidPlatform : public CallbackRegistry {
   public:
    static std::shared_ptr<AndroidPlatform> fromRuntime (Runtime&);
    explicit AndroidPlatform (Runtime&);
    ~AndroidPlatform () override;
    /** Name must start with product. and satisfy RecordStore name rules; otherwise EINVAL. No implicit v1 migration. */
    RecordStore openRecords (const std::string& name, const RecordIdentity&);
    std::vector<PushRecoveryRegistration> discoverPushRegistrations (const PushRecoveryScope&);
    bool unregisterPushIfCurrent (const PushRecoveryScope&, const std::string& id, const std::string& revision);
    PushJournalInspection inspectPushJournalRecovery ();
    PushJournalReceipt quarantineUnreadablePushJournal (const PushJournalConsent&);
    /** Owned callback/child thread only; shared Android trust, bounded I/O and event cancellation. */
    NativeHttpsResponse https (const NativeHttpsRequest&, const ExecutionCancellation&);
    /** JSON uses the same validated options as oro:notification; targetRef remains native-only. */
    std::string submitNotification (const std::string& optionsJson, const std::string& targetRef = "");
    std::string configureNotificationChannels (const std::string& channelsJson);
    std::string notificationStatus (const std::vector<std::string>& channels = {});
    std::string activeNotifications ();
    std::string notificationResponses ();
    bool acknowledgeNotificationResponse (const std::string& receiptId);
    void cancelNotification (const std::string& id, const std::string& tag = "");
    AndroidV1Records openV1Records (const std::string& name, const RecordIdentity&);
    /** Scoped JNI interop from an owned callback/cleanup only. Never retain the JNIEnv or local Application reference.
     */
    void withApplication (const std::function<void (JNIEnv*, jobject)>&);
    /** Runtime startup bridge only; retains Application context, never an Activity or WebView. */
    void bind (JNIEnv*, jobject);
    void shutdown () override;
    void drain () override;

   private:
    struct Binding;
    Runtime& runtime;
    std::shared_ptr<Binding> binding;
    std::mutex bindingMutex;
    bool closing = false;
    std::function<std::string (const std::string&, const std::string&, const std::string&, const ExecutionCancellation*)> serviceAccess ();
  };
}  // namespace oro::runtime::native
#endif
#endif
