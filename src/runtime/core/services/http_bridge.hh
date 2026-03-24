#ifndef ORO_RUNTIME_CORE_SERVICES_HTTP_BRIDGE_H
#define ORO_RUNTIME_CORE_SERVICES_HTTP_BRIDGE_H

#include "../../core.hh"
#include "../../bridge.hh"
#include "../../ipc.hh"
#include "../../http.hh"

#include <thread>
#include <memory>

namespace httplib { class Server; }

namespace oro::runtime::core::services {
  // HTTPBridge: exposes the IPC Router over an HTTP server
  class HTTPBridge : public core::Service {
    public:
      HTTPBridge (const Options& options);
      ~HTTPBridge () noexcept override;

      bool start () override;
      bool stop () override;

      // Lightweight state
      Atomic<bool> running = false;
      String hostname = "127.0.0.1";
      int port = 0;
      String pathPrefix = "/ipc";
      bool allowCORS = false;
      String sharedKey = ""; // optional simple shared key header auth
      int timeoutMs = 32000;  // max wait for IPC result

    private:
      std::shared_ptr<httplib::Server> server;
      std::thread serverThread;

      void serverMain (SharedPointer<bridge::Bridge> bridge);
  };
}

#endif
