#ifndef ORO_RUNTIME_NATIVE_DESKTOP_BUS_H
#define ORO_RUNTIME_NATIVE_DESKTOP_BUS_H

#include <oro/platform.h>

#if ORO_RUNTIME_PLATFORM_LINUX
#include "records.hh"
#include <gio/gio.h>
#include <functional>
#include <mutex>
#include <set>

namespace oro::runtime::native {
  /** Private session connection. Construct/export on the application's GLib main context. */
  class DesktopBus {
  public:
    using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
    using Signal = std::function<void(const std::string&, GVariant*)>;
    using Method = std::function<void(const std::string&, const std::string&, GVariant*, GDBusMethodInvocation*)>;
    using Completion = std::function<void(Variant, const std::string&)>;

    DesktopBus();
    ~DesktopBus();
    DesktopBus(const DesktopBus&) = delete;
    DesktopBus& operator=(const DesktopBus&) = delete;
    Variant call (const std::string& destination, const std::string& path, const std::string& interface,
                  const std::string& method, GVariant* parameters, const GVariantType* resultType,
                  int timeoutMs = 3000);
    void callAsync (const std::string& destination, const std::string& path, const std::string& interface,
                    const std::string& method, GVariant* parameters, const GVariantType* resultType,
                    Completion, int timeoutMs = 3000);
    std::string owner (const std::string& name);
    std::vector<std::string> names ();
    void own (const std::string& name);
    guint subscribe (const std::string& sender, const std::string& path, const std::string& interface,
                     const std::string& member, Signal);
    void unsubscribe(guint);
    guint exportObject (const std::string& path, const std::string& xml, Method);
    void unexport(guint);
    void emitSignal (const std::string& path, const std::string& interface, const std::string& member,
                     GVariant* parameters);
    void callNoReply (const std::string& destination, const std::string& path, const std::string& interface,
                      const std::string& member, GVariant* parameters);
    const char* uniqueName () const;
    gulong onClosed (std::function<void()>);
    void removeClosedHandler (gulong);

  private:
    GDBusConnection* connection = nullptr;
    std::set<guint> subscriptions;
    std::set<guint> objects;
    std::mutex mutex;
  };
}  // namespace oro::runtime::native
#endif
#endif
