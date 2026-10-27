#ifndef ORO_RUNTIME_CORE_SERVICES_PLATFORM_H
#define ORO_RUNTIME_CORE_SERVICES_PLATFORM_H

#include "../../ipc.hh"
#include "../../core.hh"
#if ORO_RUNTIME_PLATFORM_LINUX
#include "../../native/linux_platform.hh"
#elif ORO_RUNTIME_PLATFORM_ANDROID
#include "../../native/android_platform.hh"
#endif

namespace oro::runtime::core::services {
  class Platform : public core::Service {
    public:
      Atomic<bool> wasFirstDOMContentLoadedEventDispatched = false;

      Platform (const Options& options)
        : core::Service(options)
      {}

      ~Platform () override;
      bool start () override;
      bool stop () override;
#if ORO_RUNTIME_PLATFORM_LINUX
      std::shared_ptr<native::LinuxPlatform> nativeLinux ();
#elif ORO_RUNTIME_PLATFORM_ANDROID
      std::shared_ptr<native::AndroidPlatform> nativeAndroid ();
      bool shutdownNativeCallbacks ();
#endif

      void event (
        const ipc::Message::Seq&,
        const String&,
        const String&,
        const String&,
        const String&,
        const Callback
      );
      void openExternal (const ipc::Message::Seq&, const String&, const Callback);
      void revealFile ( const ipc::Message::Seq&, const String&, const Callback);

    private:
#if ORO_RUNTIME_PLATFORM_LINUX
      std::mutex nativeMutex;
      std::shared_ptr<native::LinuxPlatform> linuxPlatform;
#elif ORO_RUNTIME_PLATFORM_ANDROID
      std::mutex nativeMutex;
      std::shared_ptr<native::AndroidPlatform> androidPlatform;
#endif
  };
}
#endif
