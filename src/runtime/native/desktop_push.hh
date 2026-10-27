#ifndef ORO_RUNTIME_NATIVE_DESKTOP_PUSH_H
#define ORO_RUNTIME_NATIVE_DESKTOP_PUSH_H

#include "desktop_bus.hh"
#include "push_recovery.hh"
#include "push_journal.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include "../json.hh"
#include <span>

namespace oro::runtime::native {
  struct DesktopPushRegistration {
    std::string id;
    std::string kind;
    std::string authorityRef;
    std::string accountRef;
    std::string targetRef;
  };

  /** Handlers must reauthorize and tolerate duplicate delivery; no transport proves server IAM. */
  struct DesktopPushHandler {
    std::function<bool (const DesktopPushRegistration&, const ExecutionCancellation&)> authorize;
    std::function<bool (const DesktopPushRegistration&, std::span<const unsigned char>, const ExecutionCancellation&)>
        message;
    std::function<bool (const DesktopPushRegistration&, const JSON::Object&, const ExecutionCancellation&)> enrollment;
  };

  class DesktopPush {
   public:
    using Schedule = std::function<bool (std::function<void ()>)>;
    using JournalFactory = std::function<RecordStore (const std::string&)>;
    DesktopPush (std::shared_ptr<DesktopBus>, RecordStore, const std::string& applicationService, Schedule,
                 std::shared_ptr<void> exclusiveLease, std::function<void ()> changed = nullptr,
                 bool allowLoopbackHTTP = false, std::shared_ptr<PushJournalLedger> = nullptr, JournalFactory = {});
    ~DesktopPush ();
    void registerHandler (const std::string& kind, DesktopPushHandler);
    std::vector<std::string> distributors () const;
    std::vector<std::string> registeredKinds () const;
    std::string selectedDistributor () const;
    void selectDistributor (const std::string&);
    JSON::Object registerSubscription (const DesktopPushRegistration&, const std::string& vapid,
                                       const std::string& description);
    JSON::Object registration (const std::string& id) const;
    void acknowledge (const std::string& id, const std::string& generation);
    void unregister (const std::string& id);
    /** Trusted compiled code only, after product authorization and explicit recovery consent. */
    std::vector<PushRecoveryRegistration> discoverRegistrations (const PushRecoveryScope&);
    bool unregisterIfCurrent (const PushRecoveryScope&, const std::string& id, const std::string& revision);
    PushJournalInspection inspectJournalRecovery ();
    PushJournalReceipt quarantineUnreadableJournal (const PushJournalConsent&);
    void retryPending ();
    void resume ();
    void shutdown ();
    void drain ();

   private:
    struct State;
    std::shared_ptr<State> state;
    guint object = 0;
    bool unregisterScoped (const std::string&, const PushRecoveryScope*, const std::string&);
  };
}  // namespace oro::runtime::native
#endif
#endif
