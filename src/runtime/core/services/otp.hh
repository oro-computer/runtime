#ifndef ORO_RUNTIME_CORE_SERVICES_OTP_H
#define ORO_RUNTIME_CORE_SERVICES_OTP_H

#include "../../core.hh"
#include "timers.hh"

namespace oro::runtime::core::services {
  using types::Map;
  using types::Mutex;
  using types::String;
  using types::UnorderedMap;
  using types::Vector;

  class OTP : public core::Service {
    public:
      struct Request {
        uint64_t id = 0;
        String seq;
        Callback callback = nullptr;
        String origin;
        String host;
        String anchor;
        Vector<String> transports;
        String hint;
        int index = -1;
        uint64_t timeoutMs = 0;
        core::services::Timers::ID timerId = 0;
        bool waiting = true;
      };

      using RequestMap = UnorderedMap<uint64_t, Request>;

      OTP (const Options& options);
      ~OTP ();

      bool start () override;
      bool stop () override;

      void get (
        const String& seq,
        const Map<String, String>& options,
        const Callback callback
      );

      void handleMessage (uint64_t id, const String& message);
      void handleCode (uint64_t id, const String& code);
      void handleTimeout (uint64_t id);
      void handleError (
        uint64_t id,
        const String& type,
        const String& message
      );

    private:
      Mutex mutex;
      RequestMap requests;

      bool startPlatformRequest (Request& request);
      void stopPlatformRequest (const Request& request);

#if ORO_RUNTIME_PLATFORM_ANDROID
      bool startAndroidRequest (Request& request);
      void stopAndroidRequest (const Request& request);
#endif

#if ORO_RUNTIME_PLATFORM_APPLE
      bool startAppleRequest (Request& request);
      void stopAppleRequest (const Request& request);
#endif

      bool parseMessageForCode (
        const Request& request,
        const String& message,
        String& code
      ) const;

      bool resolve (uint64_t id, const String& code);
      bool reject (
        uint64_t id,
        const String& type,
        const String& message
      );

      bool popRequest (uint64_t id, Request& request);
      bool hasTransportSMS (const Vector<String>& transports) const;
  };
}
#endif
