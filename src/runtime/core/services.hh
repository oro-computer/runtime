#ifndef ORO_RUNTIME_CORE_SERVICES_H
#define ORO_RUNTIME_CORE_SERVICES_H

#include "../context.hh"
#include "../options.hh"
#include "../background/options.hh"
#include "../deps.hh"

#include "state_manager.hh"
#include "services/ai.hh"
#include "services/dbus.hh"
#include "services/broadcast_channel.hh"
#include "services/conduit.hh"
#include "services/cdp.hh"
#include "services/diagnostics.hh"
#include "services/dns.hh"
#include "services/fs.hh"
#include "services/geolocation.hh"
#include "services/media_devices.hh"
#include "services/network_status.hh"
#include "services/notifications.hh"
#include "services/iroh.hh"
#include "services/otp.hh"
#include "services/os.hh"
#include "services/permissions.hh"
#include "services/ipfs.hh"
#include "services/usb.hh"
#include "services/hid.hh"
#include "services/bluetooth.hh"
#include "services/asn1.hh"
#include "services/platform.hh"
#include "services/process.hh"
#include "services/sqlite.hh"
#include "services/secure_storage.hh"
#include "services/background.hh"
#include "services/timers.hh"
#include "services/udp.hh"
#include "services/hci.hh"
#include "services/tls.hh"
#include "services/tcp.hh"
#include "services/http_bridge.hh"
#include "services/mcp.hh"
#include "services/xpc.hh"
#include "services/tar.hh"
#include "services/update.hh"
#include "services/zlib.hh"

namespace oro::runtime::core {
  struct Services {
    struct Features {
      bool useAI = true;
      bool useBroadcashChannel = true;
    #if !ORO_RUNTIME_PLATFORM_IOS
      bool useChildProcess = true;
    #endif
      bool useConduit = true;
      bool useDiagnostics = true;
      bool useDNS = true;
      bool useFS = true;
      bool useGeolocation = true;
      bool useMediaDevices = true;
      bool useNetworkStatus = true;
      bool useNotifications = true;
      bool useTar = true;
      bool useUpdate = true;
#if ORO_RUNTIME_HAS_IROH_FFI
      bool useIroh = true;
#else
      bool useIroh = false;
#endif
#if ORO_RUNTIME_HAVE_LIBIPFS
      bool useIPFS = true;
#else
      bool useIPFS = false;
#endif
#if ORO_RUNTIME_PLATFORM_ANDROID
      bool useOTP = true;
#else
      bool useOTP = false;
#endif
      bool useOS = true;
      bool usePermissions = true;
      bool usePlatform = true;
      bool useProcess = true;
      bool useSQLite = true;
      bool useSecureStorage = true;
      bool useBackground = false;
#if ORO_RUNTIME_HAVE_ASN1C
      bool useASN1 = true;
#else
      bool useASN1 = false;
#endif
      bool useTimers = true;
      bool useUDP = true;
      bool useTLS = true;
      bool useTCP = true;
      bool useMCP = true;
    #if ORO_RUNTIME_HAS_ZLIB
      bool useZlib = true;
    #else
      bool useZlib = false;
    #endif
      bool useBluetooth = true;
#if ORO_RUNTIME_PLATFORM_ANDROID
      bool useUSB = false;
#else
      bool useUSB = true;
#endif
#if ORO_RUNTIME_HAVE_DBUS
      bool useDBus = true;
#else
      bool useDBus = false;
#endif
#if ORO_RUNTIME_PLATFORM_IOS
      bool useHID = false;
#else
      bool useHID = true;
#endif
#if ORO_RUNTIME_PLATFORM_LINUX
      bool useHCI = true;
#else
      bool useHCI = false;
#endif
#if ORO_RUNTIME_PLATFORM_APPLE
      bool useXPC = true;
#else
      bool useXPC = false;
#endif
    };

    struct Options {
      context::Dispatcher& dispatcher;
      Features features;
      const background::Options& background;
    };

    Mutex mutex;

    StateManager state;
    background::Options backgroundOptions;

    core::services::Background backgroundService;

    core::services::BroadcastChannel broadcastChannel;
    core::services::AI ai;
    core::services::Conduit conduit;
    core::services::CDP cdp;
    core::services::Diagnostics diagnostics;
    core::services::DNS dns;
    core::services::FS fs;
    core::services::Geolocation geolocation;
    core::services::MediaDevices mediaDevices;
    core::services::NetworkStatus networkStatus;
    core::services::Notifications notifications;
    core::services::Iroh iroh;
    core::services::IPFS ipfs;
    core::services::OTP otp;
    core::services::OS os;
    core::services::Permissions permissions;
    core::services::Platform platform;
    core::services::Process process;
    core::services::SQLite sqlite;
    core::services::SecureStorage secureStorage;
    core::services::ASN1 asn1;
    core::services::HID hid;
    core::services::HCI hci;
    core::services::Bluetooth bluetooth;
    core::services::USB usb;
    core::services::DBus dbus;
    core::services::Timers timers;
    core::services::UDP udp;
    core::services::TLS tls;
    core::services::TCP tcp;
    core::services::Zlib zlib;
    core::services::HTTPBridge httpBridge;
    core::services::MCP mcp;
    core::services::XPC xpc;
    core::services::Tar tar;
    core::services::Update update;

    Services (context::RuntimeContext&, const Options&);
    Services () = delete;
    Services (const Services&) = delete;
    Services (Services&&) = delete;
    Services& operator = (const Services&) = delete;
    Services& operator = (Services&&) = delete;
    bool start ();
    bool stop ();
  };
}
#endif
