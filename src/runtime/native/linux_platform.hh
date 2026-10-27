#ifndef ORO_RUNTIME_NATIVE_LINUX_PLATFORM_H
#define ORO_RUNTIME_NATIVE_LINUX_PLATFORM_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include "desktop_push.hh"
#include "deferred.hh"
#include <gtk/gtk.h>
#include "../json.hh"

namespace oro::runtime::core::services {
  class Platform;
}
namespace oro::runtime { class Runtime; }

struct oapi_context;

namespace oro::runtime::native {
  /** Compiled Linux integration registry; pages cannot supply code or executor paths. */
  class LinuxPlatform {
  public:
    using Cancellation = std::shared_ptr<std::atomic<bool>>;
    using Extension = std::function<JSON::Any(const std::string&, const JSON::Object&, const ExecutionCancellation&)>;
    using Completion = std::function<void(const JSON::Any&, const std::string&)>;
    explicit LinuxPlatform(core::services::Platform&);
    ~LinuxPlatform();
    /** Native C++ facade for a loaded extension with context_get_router permission. Pins its code until process exit. */
    static std::shared_ptr<LinuxPlatform> fromExtension (const ::oapi_context*);
    static std::shared_ptr<LinuxPlatform> fromRuntime (Runtime&);
    /** Name must begin product. or EINVAL; call on an owned worker after exact identity authorization. */
    RecordStore openRecords (const std::string& name, const RecordIdentity&);
    void registerExecutor (bool continuous, const std::string& kind, ExecutionController::Executor);
    void registerExtension (const std::string& name, Extension);
    void registerPushHandler (const std::string& kind, DesktopPushHandler);
    /** Native account recovery after product authorization/consent; no renderer or diagnostic enumeration API. */
    std::vector<PushRecoveryRegistration> discoverPushRegistrations (const PushRecoveryScope&);
    bool unregisterPushIfCurrent (const PushRecoveryScope&, const std::string& id, const std::string& revision);
    PushJournalInspection inspectPushJournalRecovery ();
    PushJournalReceipt quarantineUnreadablePushJournal (const PushJournalConsent&);
    void registerDeferredRead (const std::string& kind, DeferredExecutor);
    void registerDeferredMaintenance (const std::string& kind, const std::set<std::string>& operations, bool retrySafe,
                                      DeferredExecutor);
    void initializeNativeServices ();
    /** Headless trusted startup only; expose authenticated native service after registering compiled handlers. */
    void serveNativeHost (std::function<void()> onBusLost = nullptr);
    bool receiveLauncherURI (const std::string&);
    void request (const std::string& requestKey, const std::string& feature, const std::string& action,
                  const JSON::Object&, Completion, GtkWindow* interactiveParent = nullptr);
    void cancelRequest (const std::string& requestKey);
    void cancelDocument (const std::string& prefix);
    void shutdown ();
    void restart ();

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
#endif
