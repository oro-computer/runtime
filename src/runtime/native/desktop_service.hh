#ifndef ORO_RUNTIME_NATIVE_DESKTOP_SERVICE_H
#define ORO_RUNTIME_NATIVE_DESKTOP_SERVICE_H

#include "desktop_bus.hh"
#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include "../json.hh"
namespace oro::runtime::native {
  class LinuxPlatform;
  /** Authenticated native GUI/service transport. Possession of a bus name grants no operation authority. */
  class DesktopService {
  public:
    DesktopService(std::shared_ptr<DesktopBus>, RecordStore, const std::string& service,
                   std::weak_ptr<LinuxPlatform> backend);
    ~DesktopService();
    void shutdown ();
    void changed (const std::string& name);

  private:
    struct State;
    std::shared_ptr<State> state;
    guint object = 0;
  };
  class DesktopServiceClient {
  public:
    DesktopServiceClient(std::shared_ptr<DesktopBus>, RecordStore, const std::string& service,
                         std::function<void(const std::string&)> changed = nullptr);
    ~DesktopServiceClient();
    JSON::Any request (const std::string& key, const std::string& feature, const std::string& action,
                       const JSON::Object&, const ExecutionCancellation&);
    void cancel (const std::string& key);

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
#endif
