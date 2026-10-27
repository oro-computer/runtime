#ifndef ORO_RUNTIME_NATIVE_DESKTOP_NOTIFICATIONS_H
#define ORO_RUNTIME_NATIVE_DESKTOP_NOTIFICATIONS_H

#include "desktop_bus.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
namespace oro::runtime::native {
  struct DesktopNotification {
    std::string id;
    std::string tag;
    std::string title;
    std::string body;
    std::string icon;
    std::string category;
    std::string group;
    std::vector<std::pair<std::string, std::string>> actions;
    bool silent = false;
    bool renotify = false;
    bool ongoing = false;
    bool groupSummary = false;
    unsigned char urgency = 1;
  };

  /** Native desktop notification transport. Submission does not prove display, sound or user attention. */
  class DesktopNotifications {
  public:
    using Response = std::function<void(const std::string&, const std::string&, const std::string&)>;
    DesktopNotifications(std::shared_ptr<DesktopBus>, const std::string& applicationId,
                         const std::string& applicationName, Response);
    ~DesktopNotifications();
    std::vector<std::string> capabilities ();
    uint32_t submit (const DesktopNotification&);
    void close (const std::string& id, const std::string& tag = "");
    std::vector<std::pair<std::string, std::string>> list () const;

  private:
    struct State;
    std::shared_ptr<State> state;
    std::shared_ptr<DesktopBus> bus;
    std::vector<guint> subscriptions;
  };
}  // namespace oro::runtime::native
#endif
#endif
