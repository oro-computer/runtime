#include "xpc.hh"

#include "../../debug.hh"

namespace oro::runtime::core::services {
  std::shared_ptr<XPC::Backend> makeXPCBackend(XPC& svc) __attribute__((weak));

  namespace {
    static inline JSON::Object notSupported(const char* source, const char* message = "XPC is not available on this platform") {
      return JSON::Object::Entries {
        {"source", String(source)},
        {"err", JSON::Object::Entries {
          {"type", String("NotSupportedError")},
          {"message", String(message)}
        }}
      };
    }
  }

  XPC::XPC (const Options& options)
    : core::Service(options)
  {}

  XPC::~XPC () = default;

  JSON::Object XPC::availability() const {
    if (!this->enabled.load()) {
      return JSON::Object::Entries {
        {"source", String("xpc.availability")},
        {"data", JSON::Object::Entries {
          {"available", false},
          {"reason", String("XPC service disabled")}
        }}
      };
    }

    auto backendPtr = const_cast<XPC*>(this)->ensureBackend();
    if (!backendPtr) {
      return JSON::Object::Entries {
        {"source", String("xpc.availability")},
        {"data", JSON::Object::Entries {
          {"available", false},
          {"reason", String("XPC backend not available")}
        }}
      };
    }
    return backendPtr->availability();
  }

  void XPC::connect(const String& seq, const JSON::Any& options, const Callback cb) {
    if (!this->enabled.load()) {
      cb(seq, notSupported("xpc.connect", "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("xpc.connect"), QueuedResponse{});
      return;
    }
    backendPtr->connect(seq, options, cb);
  }

  void XPC::disconnect(const String& seq, ConnectionID id, const Callback cb) {
    if (!this->enabled.load()) {
      cb(seq, notSupported("xpc.disconnect", "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("xpc.disconnect"), QueuedResponse{});
      return;
    }
    backendPtr->disconnect(seq, id, cb);
  }

  void XPC::send(
    const String& seq,
    ConnectionID id,
    const JSON::Any& message,
    bool expectReply,
    uint64_t timeoutMs,
    const Callback cb
  ) {
    if (!this->enabled.load()) {
      cb(seq, notSupported("xpc.send", "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("xpc.send"), QueuedResponse{});
      return;
    }
    backendPtr->send(seq, id, message, expectReply, timeoutMs, cb);
  }

  void XPC::respond(
    const String& seq,
    MessageID id,
    bool isError,
    const JSON::Any& message,
    const Callback cb
  ) {
    if (!this->enabled.load()) {
      const auto source = isError ? "xpc.respondError" : "xpc.respond";
      cb(seq, notSupported(source, "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      const auto source = isError ? "xpc.respondError" : "xpc.respond";
      cb(seq, notSupported(source), QueuedResponse{});
      return;
    }
    backendPtr->respond(seq, id, isError, message, cb);
  }

  void XPC::suspend(const String& seq, ConnectionID id, const Callback cb) {
    if (!this->enabled.load()) {
      cb(seq, notSupported("xpc.suspend", "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("xpc.suspend"), QueuedResponse{});
      return;
    }
    backendPtr->suspend(seq, id, cb);
  }

  void XPC::resume(const String& seq, ConnectionID id, const Callback cb) {
    if (!this->enabled.load()) {
      cb(seq, notSupported("xpc.resume", "XPC service disabled"), QueuedResponse{});
      return;
    }

    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("xpc.resume"), QueuedResponse{});
      return;
    }
    backendPtr->resume(seq, id, cb);
  }

  XPC::Backend* XPC::ensureBackend() {
    if (!this->enabled.load()) {
      return nullptr;
    }

    if (!this->backend && makeXPCBackend != nullptr) {
      this->backend = makeXPCBackend(*this);
    }
    return this->backend.get();
  }
}
