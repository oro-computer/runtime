#include "services.hh"
#include "../debug.hh"

namespace oro::runtime::core {
  Services::Services (
    context::RuntimeContext& context,
    const Options& options
  )
    : state(context),
      backgroundOptions(options.background),
      backgroundService({
        context,
        options.features.useBackground && options.background.enabled,
        options.dispatcher,
        context.loop,
        *this
      }, this->backgroundOptions),
      ai({ context, options.features.useAI, options.dispatcher, context.loop, *this }),
      conduit({ context, options.features.useConduit, options.dispatcher, context.loop, *this }),
      cdp({ context, /* enabled via config / JS */ true, options.dispatcher, context.loop, *this }),
      broadcastChannel({ context, options.features.useBroadcashChannel, options.dispatcher, context.loop, *this }),
      dns({ context, options.features.useDNS, options.dispatcher, context.loop, *this }),
      diagnostics({ context, options.features.useDiagnostics, options.dispatcher, context.loop, *this }),
      fs({ context, options.features.useFS, options.dispatcher, context.loop, *this }),
      geolocation({ context, options.features.useGeolocation, options.dispatcher, context.loop, *this }),
      mediaDevices({ context, options.features.useMediaDevices, options.dispatcher, context.loop, *this }),
      networkStatus({ context, options.features.useNetworkStatus, options.dispatcher, context.loop, *this }),
      notifications({ context, options.features.useNotifications, options.dispatcher, context.loop, *this }),
      iroh({ context, options.features.useIroh, options.dispatcher, context.loop, *this }),
      ipfs({ context, options.features.useIPFS, options.dispatcher, context.loop, *this }),
      otp({ context, options.features.useOTP, options.dispatcher, context.loop, *this }),
      os({ context, options.features.useOS, options.dispatcher, context.loop, *this }),
      permissions({ context, options.features.usePermissions, options.dispatcher, context.loop, *this }),
      process({ context, options.features.useProcess, options.dispatcher, context.loop, *this }),
      sqlite({ context, options.features.useSQLite, options.dispatcher, context.loop, *this }),
      secureStorage({ context, options.features.useSecureStorage, options.dispatcher, context.loop, *this }),
      asn1({ context, options.features.useASN1, options.dispatcher, context.loop, *this }),
      platform({ context, options.features.usePlatform, options.dispatcher, context.loop, *this }),
      hid({ context, options.features.useHID, options.dispatcher, context.loop, *this }),
      hci({ context, options.features.useHCI, options.dispatcher, context.loop, *this }),
      bluetooth({ context, options.features.useBluetooth, options.dispatcher, context.loop, *this }),
      usb({ context, options.features.useUSB, options.dispatcher, context.loop, *this }),
      dbus({ context, options.features.useDBus, options.dispatcher, context.loop, *this, { .limit = 1 } }),
      timers({ context, options.features.useTimers, options.dispatcher, context.loop, *this }),
      udp({ context, options.features.useUDP, options.dispatcher, context.loop, *this }),
      tcp({ context, options.features.useTCP, options.dispatcher, context.loop, *this }),
      tls({ context, options.features.useTLS, options.dispatcher, context.loop, *this }),
      zlib({ context, options.features.useZlib, options.dispatcher, context.loop, *this }),
      httpBridge({ context, /* enabled via config */ true, options.dispatcher, context.loop, *this }),
      mcp({ context, options.features.useMCP, options.dispatcher, context.loop, *this }),
      xpc({ context, options.features.useXPC, options.dispatcher, context.loop, *this }),
      tar({ context, options.features.useTar, options.dispatcher, context.loop, *this }),
      update({ context, options.features.useUpdate, options.dispatcher, context.loop, *this })
  {}

  bool Services::start () {
    Lock lock(this->mutex);

    if (!this->state.init()) {
      debug("Services: failed to initialize persistent state manager");
      return false;
    }

    struct NamedService { const char* name; Service* svc; };
    const auto services = Vector<NamedService> {
      NamedService{"background", &this->backgroundService},
      NamedService{"ai", &this->ai},
      NamedService{"conduit", &this->conduit},
      NamedService{"cdp", &this->cdp},
      NamedService{"broadcastChannel", &this->broadcastChannel},
      NamedService{"dns", &this->dns},
      NamedService{"diagnostics", &this->diagnostics},
      NamedService{"fs", &this->fs},
      NamedService{"geolocation", &this->geolocation},
      NamedService{"mediaDevices", &this->mediaDevices},
      NamedService{"networkStatus", &this->networkStatus},
      NamedService{"notifications", &this->notifications},
      NamedService{"iroh", &this->iroh},
      NamedService{"ipfs", &this->ipfs},
      NamedService{"ipfs", &this->ipfs},
      NamedService{"otp", &this->otp},
      NamedService{"os", &this->os},
      NamedService{"permissions", &this->permissions},
      NamedService{"platform", &this->platform},
      NamedService{"process", &this->process},
      NamedService{"sqlite", &this->sqlite},
      NamedService{"secureStorage", &this->secureStorage},
      NamedService{"asn1", &this->asn1},
      NamedService{"hid", &this->hid},
      NamedService{"hci", &this->hci},
      NamedService{"bluetooth", &this->bluetooth},
      NamedService{"usb", &this->usb},
      NamedService{"dbus", &this->dbus},
      NamedService{"timers", &this->timers},
      NamedService{"udp", &this->udp},
      NamedService{"tcp", &this->tcp},
      NamedService{"tls", &this->tls},
      NamedService{"zlib", &this->zlib},
      NamedService{"httpBridge", &this->httpBridge},
      NamedService{"mcp", &this->mcp},
      NamedService{"xpc", &this->xpc},
      NamedService{"tar", &this->tar},
      NamedService{"update", &this->update}
    };

    #if defined(DEBUG)
      size_t enabledCount = 0;
      for (const auto& e : services) {
        if (e.svc->enabled) {
          enabledCount++;
        }
      }

      const size_t disabledCount = services.size() - enabledCount;

      if (disabledCount > 0) {
        std::string disabledList;
        for (const auto& e : services) {
          if (!e.svc->enabled) {
            if (!disabledList.empty()) {
              disabledList += ", ";
            }
            disabledList += e.name;
          }
        }
      }
    #endif

    for (const auto& entry : services) {
      auto* service = entry.svc;
      if (!service->enabled) {
        continue;
      }
      const bool ok = service->start();
      if (!ok) {
        return false;
      }
    }

    return true;
  }

  bool Services::stop () {
    Lock lock(this->mutex);
    struct NamedService { const char* name; Service* svc; };
    const auto services = Vector<NamedService> {
      NamedService{"background", &this->backgroundService},
      NamedService{"ai", &this->ai},
      NamedService{"conduit", &this->conduit},
      NamedService{"cdp", &this->cdp},
      NamedService{"broadcastChannel", &this->broadcastChannel},
      NamedService{"dns", &this->dns},
      NamedService{"diagnostics", &this->diagnostics},
      NamedService{"fs", &this->fs},
      NamedService{"geolocation", &this->geolocation},
      NamedService{"mediaDevices", &this->mediaDevices},
      NamedService{"networkStatus", &this->networkStatus},
      NamedService{"notifications", &this->notifications},
      NamedService{"iroh", &this->iroh},
      NamedService{"otp", &this->otp},
      NamedService{"os", &this->os},
      NamedService{"permissions", &this->permissions},
      NamedService{"platform", &this->platform},
      NamedService{"process", &this->process},
      NamedService{"sqlite", &this->sqlite},
      NamedService{"secureStorage", &this->secureStorage},
      NamedService{"asn1", &this->asn1},
      NamedService{"hid", &this->hid},
      NamedService{"hci", &this->hci},
      NamedService{"bluetooth", &this->bluetooth},
      NamedService{"usb", &this->usb},
      NamedService{"dbus", &this->dbus},
      NamedService{"timers", &this->timers},
      NamedService{"udp", &this->udp},
      NamedService{"tcp", &this->tcp},
      NamedService{"tls", &this->tls},
      NamedService{"zlib", &this->zlib},
      NamedService{"httpBridge", &this->httpBridge},
      NamedService{"mcp", &this->mcp},
      NamedService{"xpc", &this->xpc},
      NamedService{"tar", &this->tar},
      NamedService{"update", &this->update}
    };

    #if defined(DEBUG)
      size_t enabledCount = 0;
      for (const auto& e : services) {
        if (e.svc->enabled) {
          enabledCount++;
        }
      }

      const size_t disabledCount = services.size() - enabledCount;

      if (disabledCount > 0) {
        std::string disabledList;
        for (const auto& e : services) {
          if (!e.svc->enabled) {
            if (!disabledList.empty()) {
              disabledList += ", ";
            }
            disabledList += e.name;
          }
        }
      }
    #endif

    for (const auto& entry : services) {
      auto* service = entry.svc;
      if (!service->enabled) {
        continue;
      }
      const bool ok = service->stop();
      if (!ok) {
        return false;
      }
    }

    this->state.shutdown();
    return true;
  }
}
