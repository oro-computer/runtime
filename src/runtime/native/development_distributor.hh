#ifndef ORO_RUNTIME_NATIVE_DEVELOPMENT_DISTRIBUTOR_H
#define ORO_RUNTIME_NATIVE_DEVELOPMENT_DISTRIBUTOR_H

#include "desktop_bus.hh"
#if ORO_RUNTIME_PLATFORM_LINUX
namespace oro::runtime::native {
  /** Local developer Distributor2 transport. Run on a GLib main loop; no product IAM is inferred. */
  class DevelopmentDistributor {
  public:
    static constexpr const char* service = "org.unifiedpush.Distributor.OroDev";
    DevelopmentDistributor(std::shared_ptr<DesktopBus>, RecordStore, const std::string& origin,
                           bool allowLoopbackHTTP = false, const std::string& caFile = "");
    ~DevelopmentDistributor();
    static std::string validateOrigin (const std::string&, bool allowLoopbackHTTP);
    void shutdown ();

  private:
    struct State;
    std::shared_ptr<State> state;
    guint object = 0;
    guint timer = 0;
  };
}  // namespace oro::runtime::native
#endif
#endif
