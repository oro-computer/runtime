#include "iroh.hh"

#include "../../bytes.hh"

namespace oro::runtime::core::services {
  namespace {
    constexpr const char* kSourceInit = "iroh.init";
    constexpr const char* kSourceShutdown = "iroh.shutdown";
    constexpr const char* kSourceStatus = "iroh.status";
    constexpr const char* kSourceSetLogLevel = "iroh.setLogLevel";
    constexpr const char* kSourcePathToKey = "iroh.pathToKey";
    constexpr const char* kSourceKeyToPath = "iroh.keyToPath";
    constexpr const char* kSourceEndpointCreate = "iroh.endpoint.create";
    constexpr const char* kSourceEndpointDestroy = "iroh.endpoint.destroy";
    constexpr const char* kSourceEndpointBind = "iroh.endpoint.bind";
    constexpr const char* kSourceEndpointHomeRelay = "iroh.endpoint.homeRelay";
    constexpr const char* kSourceEndpointNodeAddr = "iroh.endpoint.nodeAddr";
    constexpr const char* kSourceEndpointClose = "iroh.endpoint.close";
    constexpr const char* kSourceConnectionConnect = "iroh.connection.connect";
    constexpr const char* kSourceConnectionAccept = "iroh.connection.accept";
    constexpr const char* kSourceConnectionAcceptAny = "iroh.connection.acceptAny";
    constexpr const char* kSourceConnectionClose = "iroh.connection.close";
    constexpr const char* kSourceConnectionWaitClosed = "iroh.connection.waitClosed";
    constexpr const char* kSourceConnectionStats = "iroh.connection.stats";
    constexpr const char* kSourceConnectionDatagramWrite = "iroh.connection.datagram.write";
    constexpr const char* kSourceConnectionDatagramRead = "iroh.connection.datagram.read";
    constexpr const char* kSourceConnectionTypeWatch = "iroh.connectionType.watch";
    constexpr const char* kSourceConnectionOpenBi = "iroh.connection.openBi";
    constexpr const char* kSourceConnectionOpenUni = "iroh.connection.openUni";
    constexpr const char* kSourceConnectionAcceptBi = "iroh.connection.acceptBi";
    constexpr const char* kSourceConnectionAcceptUni = "iroh.connection.acceptUni";
    constexpr const char* kSourceStreamWrite = "iroh.stream.write";
    constexpr const char* kSourceStreamFinish = "iroh.stream.finish";
    constexpr const char* kSourceStreamRead = "iroh.stream.read";
    constexpr const char* kSourceStreamReadToEnd = "iroh.stream.readToEnd";

    inline JSON::Object::Entries makeDataPayload (const String& source, const JSON::Object::Entries& data) {
      JSON::Object::Entries payload;
      payload.emplace("source", source);
      payload.emplace("data", data);
      return payload;
    }

    inline JSON::Object::Entries makeErrorPayload (
      const String& source,
      const String& message,
      const String& code = ""
    ) {
      JSON::Object::Entries err;
      err.emplace("message", message);
      if (!code.empty()) {
        err.emplace("code", code);
      }

      JSON::Object::Entries payload;
      payload.emplace("source", source);
      payload.emplace("err", err);
      return payload;
    }

    inline JSON::Object::Entries makeEndpointError (
      const String& source,
      iroh::EndpointResult result,
      const String& detail = ""
    ) {
      const String code = iroh::toString(result);
      String message = "iroh endpoint error";
      if (!detail.empty()) {
        message = detail;
      }
      return makeErrorPayload(source, message, code);
    }

    inline JSON::Object::Entries makeKeyError (
      const String& source,
      iroh::KeyResult result,
      const String& detail = ""
    ) {
      const String code = iroh::toString(result);
      String message = "iroh key error";
      if (!detail.empty()) {
        message = detail;
      }
      return makeErrorPayload(source, message, code);
    }

    inline JSON::Object::Entries makeAddrError (
      const String& source,
      iroh::AddrResult result,
      const String& detail = ""
    ) {
      const String code = iroh::toString(result);
      String message = "iroh address error";
      if (!detail.empty()) {
        message = detail;
      }
      return makeErrorPayload(source, message, code);
    }

    inline JSON::Object::Entries makeLibraryError (
      const String& source,
      const iroh::Result& result,
      const String& fallback
    ) {
      String message = fallback;
      if (!result.message.empty()) {
        message = result.message;
      }
      return makeErrorPayload(source, message, String(std::to_string(result.code)));
    }
  }

#if ORO_RUNTIME_HAS_IROH_FFI
  struct Iroh::ConnectionTypeWatcher {
    String nodeId;
    iroh::PublicKey key;
    Callback callback;
    bool active = false;
  };

  struct Iroh::SendStreamEntry {
    ID id = 0;
    ID connectionId = 0;
    iroh::SendStream stream;
    bool finished = false;
  };

  struct Iroh::RecvStreamEntry {
    ID id = 0;
    ID connectionId = 0;
    iroh::RecvStream stream;
  };

  struct Iroh::ConnectionEntry {
    ID id = 0;
    ID endpointId = 0;
    iroh::Connection connection;
    bool closed = false;
    Vector<uint8_t> remoteAlpn;
  };

  struct Iroh::EndpointEntry {
    ID id = 0;
    iroh::Endpoint endpoint;
    iroh::EndpointConfig config;
    bool closed = false;
    Map<String, WatcherPtr> watchers;
  };

  Iroh::Iroh (const Options& options)
    : core::Service(options),
      allowService(options.enabled),
      cachedLogLevel(static_cast<int>(iroh::LogLevel::Info)) {
    this->enabled = this->allowService && this->hasLibrary();
  }

  Iroh::~Iroh () {
    this->stop();
  }

  bool Iroh::hasLibrary () const {
  #if ORO_RUNTIME_HAS_IROH_FFI
    return true;
  #else
    return false;
  #endif
  }

  bool Iroh::start () {
    const bool shouldEnable = this->allowService && this->hasLibrary();
    this->enabled = shouldEnable;
    return shouldEnable;
  }

  bool Iroh::stop () {
  #if ORO_RUNTIME_HAS_IROH_FFI
    {
      Lock lock(this->mutex);
      this->sendStreams.clear();
      this->recvStreams.clear();
      this->connections.clear();
      this->endpoints.clear();
    }
    this->cachedLogLevel.store(static_cast<int>(iroh::LogLevel::Info));
    iroh::Library::shared().shutdown();
    return true;
  #else
    return false;
  #endif
  }

  void Iroh::respondError (
    const Callback& cb,
    const String& seq,
    const String& source,
    const String& message,
    const String& code
  ) {
    cb(seq, makeErrorPayload(source, message, code), QueuedResponse{});
  }

  JSON::Object::Entries Iroh::makeStatusData () const {
    JSON::Object::Entries data;
  #if ORO_RUNTIME_HAS_IROH_FFI
    auto& library = iroh::Library::shared();
    data.emplace("initialized", library.isInitialized());
    data.emplace("version", library.version());
  #else
    data.emplace("initialized", false);
    data.emplace("version", String());
  #endif

    const auto levelValue = this->cachedLogLevel.load();
    const auto level = static_cast<iroh::LogLevel>(levelValue);
    JSON::Object::Entries logLevel;
    logLevel.emplace("value", levelValue);
    logLevel.emplace("name", String(iroh::toString(level)));
    data.emplace("logLevel", logLevel);
    return data;
  }

  JSON::Object::Entries Iroh::describeConnectionStats (const ConnectionPtr& entry) const {
    JSON::Object::Entries stats;
#if ORO_RUNTIME_HAS_IROH_FFI
    if (!entry) {
      return stats;
    }

    stats.emplace("connectionId", std::to_string(entry->id));
    stats.emplace("maxDatagramSize", static_cast<int64_t>(entry->connection.maxDatagramSize()));
    stats.emplace("rtt", static_cast<int64_t>(entry->connection.rtt()));
    stats.emplace("packetLoss", entry->connection.packetLoss());
#endif
    return stats;
  }

  Iroh::EndpointPtr Iroh::getEndpoint (ID id) const {
    Lock lock(this->mutex);
    auto it = this->endpoints.find(id);
    if (it != this->endpoints.end()) {
      return it->second;
    }
    return nullptr;
  }

  Iroh::ConnectionPtr Iroh::getConnection (ID id) const {
    Lock lock(this->mutex);
    auto it = this->connections.find(id);
    if (it != this->connections.end()) {
      return it->second;
    }
    return nullptr;
  }

  Iroh::SendStreamPtr Iroh::getSendStream (ID id) const {
    Lock lock(this->mutex);
    auto it = this->sendStreams.find(id);
    if (it != this->sendStreams.end()) {
      return it->second;
    }
    return nullptr;
  }

  Iroh::RecvStreamPtr Iroh::getRecvStream (ID id) const {
    Lock lock(this->mutex);
    auto it = this->recvStreams.find(id);
    if (it != this->recvStreams.end()) {
      return it->second;
    }
    return nullptr;
  }

  void Iroh::removeStreamsForConnectionLocked (ID connectionId) {
    for (auto it = this->sendStreams.begin(); it != this->sendStreams.end();) {
      const auto& entry = it->second;
      if (entry && entry->connectionId == connectionId) {
        it = this->sendStreams.erase(it);
      } else {
        ++it;
      }
    }

    for (auto it = this->recvStreams.begin(); it != this->recvStreams.end();) {
      const auto& entry = it->second;
      if (entry && entry->connectionId == connectionId) {
        it = this->recvStreams.erase(it);
      } else {
        ++it;
      }
    }
  }

  void Iroh::removeConnectionLocked (ID id) {
    auto it = this->connections.find(id);
    if (it != this->connections.end()) {
      this->removeStreamsForConnectionLocked(id);
      this->connections.erase(it);
    }
  }

  void Iroh::init (const String& seq, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceInit, "Iroh FFI unavailable");
    }

    this->loop.dispatch([=, this]() {
      if (!iroh::Library::shared().init()) {
        return this->respondError(cb, seq, kSourceInit, "Failed to initialize Iroh");
      }

      cb(seq, makeDataPayload(kSourceInit, this->makeStatusData()), QueuedResponse{});
    });
  }

  void Iroh::shutdown (const String& seq, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceShutdown, "Iroh FFI unavailable");
    }

    this->loop.dispatch([=, this]() {
      if (!iroh::Library::shared().shutdown()) {
        return this->respondError(cb, seq, kSourceShutdown, "Failed to shutdown Iroh");
      }

      {
        Lock lock(this->mutex);
        this->sendStreams.clear();
        this->recvStreams.clear();
        this->connections.clear();
        this->endpoints.clear();
      }

      cb(seq, makeDataPayload(kSourceShutdown, this->makeStatusData()), QueuedResponse{});
    });
  }

  void Iroh::status (const String& seq, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceStatus, "Iroh FFI unavailable");
    }

    this->loop.dispatch([=, this]() {
      cb(seq, makeDataPayload(kSourceStatus, this->makeStatusData()), QueuedResponse{});
    });
  }

  void Iroh::setLogLevel (const String& seq, iroh::LogLevel level, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceSetLogLevel, "Iroh FFI unavailable");
    }

    this->loop.dispatch([=, this]() {
      auto result = iroh::Library::shared().setLogLevel(level);
      if (!result.ok()) {
        return cb(seq, makeLibraryError(kSourceSetLogLevel, result, "Failed to set log level"), QueuedResponse{});
      }

      this->cachedLogLevel.store(static_cast<int>(level));
      JSON::Object::Entries data;
      data.emplace("logLevel", JSON::Object::Entries {
        {"value", static_cast<int>(level)},
        {"name", String(iroh::toString(level))}
      });
      cb(seq, makeDataPayload(kSourceSetLogLevel, data), QueuedResponse{});
    });
  }

  void Iroh::pathToKey (
    const String& seq,
    const String& path,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourcePathToKey, "Iroh FFI unavailable");
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> keyBytes;
      auto result = iroh::Library::shared().pathToKey(path, prefix, root, keyBytes);

      this->loop.dispatch([this, seq, cb, result, keyBytes = std::move(keyBytes)]() mutable {
        if (!result.ok()) {
          return cb(seq, makeLibraryError(kSourcePathToKey, result, "Failed to derive key"), QueuedResponse{});
        }

        JSON::Object::Entries data;
        data.emplace("key", bytes::base64::encode(keyBytes));
        cb(seq, makeDataPayload(kSourcePathToKey, data), QueuedResponse{});
      });
    });
  }

  void Iroh::keyToPath (
    const String& seq,
    const Vector<uint8_t>& key,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceKeyToPath, "Iroh FFI unavailable");
    }

    this->queue.push([=, this]() {
      String path;
      auto result = iroh::Library::shared().keyToPath(key, prefix, root, path);

      this->loop.dispatch([this, seq, cb, result, path = std::move(path)]() mutable {
        if (!result.ok()) {
          return cb(seq, makeLibraryError(kSourceKeyToPath, result, "Failed to derive path"), QueuedResponse{});
        }

        JSON::Object::Entries data;
        data.emplace("path", path);
        cb(seq, makeDataPayload(kSourceKeyToPath, data), QueuedResponse{});
      });
    });
  }

  void Iroh::createEndpoint (const String& seq, ID id, const EndpointOptions& options, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointCreate, "Iroh FFI unavailable");
    }

    this->loop.dispatch([=, this]() {
      auto entry = std::make_shared<EndpointEntry>();
      entry->id = id;

      {
        Lock lock(this->mutex);
        if (this->endpoints.find(id) != this->endpoints.end()) {
          return this->respondError(cb, seq, kSourceEndpointCreate, "Endpoint already exists");
        }
      }

      if (options.secretKey) {
        auto parsed = iroh::SecretKey::fromBase32(*options.secretKey);
        if (parsed.second != iroh::KeyResult::Ok) {
          return cb(seq, makeKeyError(kSourceEndpointCreate, parsed.second, "Invalid secret key"), QueuedResponse{});
        }
        entry->config.setSecretKey(std::move(parsed.first));
      }

      if (options.relayMode) {
        entry->config.setRelayMode(*options.relayMode);
      }

      if (options.discovery) {
        entry->config.setDiscovery(*options.discovery);
      }

      for (const auto& alpn : options.alpns) {
        entry->config.addAlpn(alpn);
      }

      {
        Lock lock(this->mutex);
        this->endpoints.emplace(id, entry);
      }

      JSON::Object::Entries data;
      data.emplace("endpointId", std::to_string(id));
      cb(seq, makeDataPayload(kSourceEndpointCreate, data), QueuedResponse{});
    });
  }

  void Iroh::destroyEndpoint (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointDestroy, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(id);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceEndpointDestroy, "Unknown endpoint");
    }

    this->queue.push([=, this]() {
      Map<String, WatcherPtr> watchersCopy;
      {
        Lock lock(this->mutex);
        auto it = this->endpoints.find(id);
        if (it != this->endpoints.end()) {
          watchersCopy = it->second->watchers;
          it->second->watchers.clear();
        }
      }

      for (const auto& pair : watchersCopy) {
        if (pair.second) {
          endpoint->endpoint.watchConnectionType(pair.second->key, {});
        }
      }

      const auto closeResult = endpoint->endpoint.close();
      if (closeResult != iroh::EndpointResult::Ok) {
        this->loop.dispatch([=, this]() {
          cb(seq, makeEndpointError(kSourceEndpointDestroy, closeResult, "Failed to close endpoint"), QueuedResponse{});
        });
        return;
      }

      this->loop.dispatch([=, this]() {
        {
          Lock lock(this->mutex);
          for (auto it = this->connections.begin(); it != this->connections.end();) {
            const auto& connection = it->second;
            if (connection && connection->endpointId == id) {
              this->removeStreamsForConnectionLocked(connection->id);
              it = this->connections.erase(it);
            } else {
              ++it;
            }
          }
          this->endpoints.erase(id);
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(id));
        cb(seq, makeDataPayload(kSourceEndpointDestroy, data), QueuedResponse{});
      });
    });
  }

  void Iroh::bindEndpoint (const String& seq, ID id, const BindOptions& options, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointBind, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(id);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceEndpointBind, "Unknown endpoint");
    }

    this->queue.push([=, this]() {
      std::optional<iroh::SocketAddrV4> ipv4Addr;
      std::optional<iroh::SocketAddrV6> ipv6Addr;
      bool parseError = false;
      JSON::Object::Entries parseErrorPayload;

      if (options.ipv4) {
        auto parsed = iroh::SocketAddrV4::fromString(*options.ipv4);
        if (parsed.second != iroh::AddrResult::Ok) {
          parseError = true;
          parseErrorPayload = makeAddrError(kSourceEndpointBind, parsed.second, "Invalid IPv4 address");
        } else {
          ipv4Addr = std::move(parsed.first);
        }
      }

      if (!parseError && options.ipv6) {
        auto parsed = iroh::SocketAddrV6::fromString(*options.ipv6);
        if (parsed.second != iroh::AddrResult::Ok) {
          parseError = true;
          parseErrorPayload = makeAddrError(kSourceEndpointBind, parsed.second, "Invalid IPv6 address");
        } else {
          ipv6Addr = std::move(parsed.first);
        }
      }

      iroh::EndpointResult bindResult = iroh::EndpointResult::Ok;
      if (!parseError) {
        iroh::SocketAddrV4* ipv4Ptr = ipv4Addr ? &ipv4Addr.value() : nullptr;
        iroh::SocketAddrV6* ipv6Ptr = ipv6Addr ? &ipv6Addr.value() : nullptr;
        bindResult = endpoint->endpoint.bind(endpoint->config, ipv4Ptr, ipv6Ptr);
      }

      this->loop.dispatch([this, seq, cb, id, parseError, bindResult, parseErrorPayload = std::move(parseErrorPayload)]() mutable {
        if (parseError) {
          return cb(seq, parseErrorPayload, QueuedResponse{});
        }

        if (bindResult != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceEndpointBind, bindResult, "Failed to bind endpoint"), QueuedResponse{});
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(id));
        cb(seq, makeDataPayload(kSourceEndpointBind, data), QueuedResponse{});
      });
    });
  }

  void Iroh::getHomeRelay (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointHomeRelay, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(id);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceEndpointHomeRelay, "Unknown endpoint");
    }

    this->queue.push([=, this]() {
      iroh::Url url;
      const auto result = endpoint->endpoint.homeRelay(url);
      const String relay = result == iroh::EndpointResult::Ok ? url.toString() : "";

      this->loop.dispatch([this, seq, cb, id, relay, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceEndpointHomeRelay, result, "Failed to get home relay"), QueuedResponse{});
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(id));
        data.emplace("url", relay);
        cb(seq, makeDataPayload(kSourceEndpointHomeRelay, data), QueuedResponse{});
      });
    });
  }

  void Iroh::getNodeAddr (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointNodeAddr, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(id);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceEndpointNodeAddr, "Unknown endpoint");
    }

    this->queue.push([=, this]() {
      iroh::NodeAddr nodeAddr;
      const auto result = endpoint->endpoint.nodeAddr(nodeAddr);
      const String node = result == iroh::EndpointResult::Ok ? nodeAddr.toString() : "";

      this->loop.dispatch([this, seq, cb, id, node, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceEndpointNodeAddr, result, "Failed to get node address"), QueuedResponse{});
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(id));
        data.emplace("nodeAddr", node);
        cb(seq, makeDataPayload(kSourceEndpointNodeAddr, data), QueuedResponse{});
      });
    });
  }

  void Iroh::connect (
    const String& seq,
    ID endpointId,
    ID connectionId,
    const ConnectOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionConnect, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(endpointId);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceConnectionConnect, "Unknown endpoint");
    }

    {
      Lock lock(this->mutex);
      if (this->connections.find(connectionId) != this->connections.end()) {
        return this->respondError(cb, seq, kSourceConnectionConnect, "Connection already exists");
      }
    }

    this->queue.push([=, this]() {
      auto parsed = iroh::NodeAddr::fromString(options.nodeAddr);
      const auto addrResult = parsed.second;
      if (addrResult != iroh::AddrResult::Ok) {
        this->loop.dispatch([this, seq, cb, addrResult]() {
          cb(seq, makeAddrError(kSourceConnectionConnect, addrResult, "Invalid node address"), QueuedResponse{});
        });
        return;
      }

      iroh::Connection connection;
      auto nodeAddr = std::move(parsed.first);
      const auto result = endpoint->endpoint.connect(options.alpn, nodeAddr, connection);

      Vector<uint8_t> negotiatedAlpn;
      if (result == iroh::EndpointResult::Ok) {
        static_cast<void>(connection.negotiatedAlpn(negotiatedAlpn));
      }

      auto entry = std::make_shared<ConnectionEntry>();
      entry->id = connectionId;
      entry->endpointId = endpointId;
      entry->connection = std::move(connection);
      entry->remoteAlpn = std::move(negotiatedAlpn);

      this->loop.dispatch([this, seq, cb, entry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionConnect, result, "Failed to connect"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->connections.emplace(entry->id, entry);
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(entry->endpointId));
        data.emplace("connectionId", std::to_string(entry->id));
        if (!entry->remoteAlpn.empty()) {
          data.emplace("alpn", bytes::base64::encode(entry->remoteAlpn));
        }
        cb(seq, makeDataPayload(kSourceConnectionConnect, data), QueuedResponse{});
      });
    });
  }

  void Iroh::accept (
    const String& seq,
    ID endpointId,
    ID connectionId,
    const AcceptOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionAccept, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(endpointId);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceConnectionAccept, "Unknown endpoint");
    }

    {
      Lock lock(this->mutex);
      if (this->connections.find(connectionId) != this->connections.end()) {
        return this->respondError(cb, seq, kSourceConnectionAccept, "Connection already exists");
      }
    }

    Vector<uint8_t> expected = options.expectedAlpn.value_or(Vector<uint8_t>{});

    this->queue.push([this, seq, cb, endpoint, connectionId, endpointId, expected = std::move(expected)]() mutable {
      iroh::Connection connection;
      const auto result = endpoint->endpoint.accept(expected, connection);

      Vector<uint8_t> negotiatedAlpn;
      if (result == iroh::EndpointResult::Ok) {
        static_cast<void>(connection.negotiatedAlpn(negotiatedAlpn));
      }

      auto entry = std::make_shared<ConnectionEntry>();
      entry->id = connectionId;
      entry->endpointId = endpointId;
      entry->connection = std::move(connection);
      entry->remoteAlpn = std::move(negotiatedAlpn);

      this->loop.dispatch([this, seq, cb, entry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionAccept, result, "Failed to accept connection"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->connections.emplace(entry->id, entry);
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(entry->endpointId));
        data.emplace("connectionId", std::to_string(entry->id));
        if (!entry->remoteAlpn.empty()) {
          data.emplace("alpn", bytes::base64::encode(entry->remoteAlpn));
        }
        cb(seq, makeDataPayload(kSourceConnectionAccept, data), QueuedResponse{});
      });
    });
  }

  void Iroh::acceptAny (
    const String& seq,
    ID endpointId,
    ID connectionId,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionAcceptAny, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(endpointId);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceConnectionAcceptAny, "Unknown endpoint");
    }

    {
      Lock lock(this->mutex);
      if (this->connections.find(connectionId) != this->connections.end()) {
        return this->respondError(cb, seq, kSourceConnectionAcceptAny, "Connection already exists");
      }
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> alpn;
      iroh::Connection connection;
      const auto result = endpoint->endpoint.acceptAny(alpn, connection);

      auto entry = std::make_shared<ConnectionEntry>();
      entry->id = connectionId;
      entry->endpointId = endpointId;
      entry->connection = std::move(connection);
      entry->remoteAlpn = alpn;

      this->loop.dispatch([this, seq, cb, entry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionAcceptAny, result, "Failed to accept connection"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->connections.emplace(entry->id, entry);
        }

        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(entry->endpointId));
        data.emplace("connectionId", std::to_string(entry->id));
        if (!entry->remoteAlpn.empty()) {
          data.emplace("alpn", bytes::base64::encode(entry->remoteAlpn));
        }
        cb(seq, makeDataPayload(kSourceConnectionAcceptAny, data), QueuedResponse{});
      });
    });
  }

  void Iroh::closeEndpoint (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceEndpointClose, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(id);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceEndpointClose, "Unknown endpoint");
    }

    this->queue.push([=, this]() {
      Map<String, WatcherPtr> watchersCopy;
      {
        Lock lock(this->mutex);
        auto it = this->endpoints.find(id);
        if (it != this->endpoints.end()) {
          watchersCopy = it->second->watchers;
          it->second->watchers.clear();
        }
      }

      for (const auto& pair : watchersCopy) {
        if (pair.second) {
          endpoint->endpoint.watchConnectionType(pair.second->key, {});
        }
      }

      const auto closeResult = endpoint->endpoint.close();
      endpoint->closed = closeResult == iroh::EndpointResult::Ok;
      if (closeResult != iroh::EndpointResult::Ok) {
        this->loop.dispatch([=, this]() {
          cb(seq, makeEndpointError(kSourceEndpointClose, closeResult, "Failed to close endpoint"), QueuedResponse{});
        });
        return;
      }

      this->loop.dispatch([=, this]() {
        JSON::Object::Entries data;
        data.emplace("endpointId", std::to_string(id));
        cb(seq, makeDataPayload(kSourceEndpointClose, data), QueuedResponse{});
      });
    });
  }

  void Iroh::closeConnection (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionClose, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(id);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionClose, "Unknown connection");
    }

    this->queue.push([=, this]() {
      const auto result = connection->connection.close();
      if (result != iroh::EndpointResult::Ok) {
        this->loop.dispatch([=, this]() {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Connection close timed out")
            : String("Failed to close connection");
          cb(seq, makeEndpointError(kSourceConnectionClose, result, message), QueuedResponse{});
        });
        return;
      }
      connection->closed = true;

      this->loop.dispatch([=, this]() {
        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(id));
        cb(seq, makeDataPayload(kSourceConnectionClose, data), QueuedResponse{});
      });
    });
  }

  void Iroh::waitConnectionClosed (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionWaitClosed, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(id);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionWaitClosed, "Unknown connection");
    }

    this->queue.push([=, this]() {
      const auto result = connection->connection.waitClosed();
      connection->closed = true;

      this->loop.dispatch([this, seq, cb, id, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionWaitClosed, result, "Failed to wait for close"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->removeStreamsForConnectionLocked(id);
          this->connections.erase(id);
        }

        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(id));
        cb(seq, makeDataPayload(kSourceConnectionWaitClosed, data), QueuedResponse{});
      });
    });
  }

  void Iroh::connectionStats (const String& seq, ID id, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionStats, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(id);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionStats, "Unknown connection");
    }

    this->loop.dispatch([=, this]() {
      JSON::Object::Entries data = this->describeConnectionStats(this->getConnection(id));
      if (data.empty()) {
        return this->respondError(cb, seq, kSourceConnectionStats, "Connection unavailable");
      }
      cb(seq, makeDataPayload(kSourceConnectionStats, data), QueuedResponse{});
    });
  }

  void Iroh::writeDatagram (
    const String& seq,
    ID connectionId,
    const Vector<uint8_t>& data,
    const DatagramOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionDatagramWrite, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionDatagramWrite, "Unknown connection");
    }

    this->queue.push([=, this]() {
      const auto result = connection->connection.writeDatagram(data, options.timeoutMs);

      this->loop.dispatch([this, seq, cb, connectionId, result]() {
        if (result != iroh::EndpointResult::Ok) {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Datagram send timed out")
            : String("Failed to write datagram");
          return cb(seq, makeEndpointError(kSourceConnectionDatagramWrite, result, message), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("connectionId", std::to_string(connectionId));
        cb(seq, makeDataPayload(kSourceConnectionDatagramWrite, payload), QueuedResponse{});
      });
    });
  }

  void Iroh::readDatagram (
    const String& seq,
    ID connectionId,
    const DatagramOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionDatagramRead, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionDatagramRead, "Unknown connection");
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> data;
      const auto result = connection->connection.readDatagram(data, options.timeoutMs);

      this->loop.dispatch([this, seq, cb, connectionId, result, data = std::move(data)]() mutable {
        if (result != iroh::EndpointResult::Ok) {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Datagram read timed out")
            : String("Failed to read datagram");
          return cb(seq, makeEndpointError(kSourceConnectionDatagramRead, result, message), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("connectionId", std::to_string(connectionId));
        payload.emplace("data", bytes::base64::encode(data));
        cb(seq, makeDataPayload(kSourceConnectionDatagramRead, payload), QueuedResponse{});
      });
    });
  }

  void Iroh::watchConnectionType (
    const String& seq,
    ID endpointId,
    const String& nodeId,
    bool enable,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionTypeWatch, "Iroh FFI unavailable");
    }

    auto endpoint = this->getEndpoint(endpointId);
    if (!endpoint) {
      return this->respondError(cb, seq, kSourceConnectionTypeWatch, "Unknown endpoint");
    }

    if (!enable) {
      WatcherPtr watcher;
      {
        Lock lock(this->mutex);
        auto it = endpoint->watchers.find(nodeId);
        if (it != endpoint->watchers.end()) {
          watcher = it->second;
          endpoint->watchers.erase(it);
        }
      }

      if (watcher) {
        endpoint->endpoint.watchConnectionType(watcher->key, {});
      }

      JSON::Object::Entries data;
      data.emplace("endpointId", std::to_string(endpointId));
      data.emplace("nodeId", nodeId);
      data.emplace("watching", false);
      return cb(seq, makeDataPayload(kSourceConnectionTypeWatch, data), QueuedResponse{});
    }

    auto parsed = iroh::PublicKey::fromBase32(nodeId);
    if (parsed.second != iroh::KeyResult::Ok) {
      return cb(seq, makeKeyError(kSourceConnectionTypeWatch, parsed.second, "Invalid node id"), QueuedResponse{});
    }

    auto watcher = std::make_shared<ConnectionTypeWatcher>();
    watcher->nodeId = nodeId;
    watcher->key = parsed.first;
    watcher->callback = cb;
    watcher->active = true;

    {
      Lock lock(this->mutex);
      endpoint->watchers[nodeId] = watcher;
    }

    auto endpointWeak = std::weak_ptr<EndpointEntry>(endpoint);

    endpoint->endpoint.watchConnectionType(
      watcher->key,
      [this, watcher, endpointWeak, endpointId](iroh::EndpointResult result, iroh::ConnectionType type) {
        this->dispatch([this, watcher, endpointWeak, endpointId, result, type]() {
          auto callback = watcher->callback;
          if (!callback) {
            return;
          }

          JSON::Object::Entries event;
          event.emplace("source", String("iroh.connectionType"));

          if (result == iroh::EndpointResult::Ok) {
            JSON::Object::Entries payload;
            payload.emplace("endpointId", std::to_string(endpointId));
            payload.emplace("nodeId", watcher->nodeId);
            payload.emplace("connectionType", JSON::Object::Entries {
              {"value", static_cast<int>(type)},
              {"name", String(iroh::toString(type))}
            });
            event.emplace("data", payload);
          } else {
            JSON::Object::Entries err;
            err.emplace("message", String("Connection type watcher failed"));
            err.emplace("code", String(iroh::toString(result)));
            err.emplace("endpointId", std::to_string(endpointId));
            err.emplace("nodeId", watcher->nodeId);
            event.emplace("err", err);

            watcher->active = false;

            if (auto locked = endpointWeak.lock()) {
              Lock lock(this->mutex);
              locked->watchers.erase(watcher->nodeId);
            }
          }

          callback("-1", event, QueuedResponse{});
        });
      }
    );

    JSON::Object::Entries data;
    data.emplace("endpointId", std::to_string(endpointId));
    data.emplace("nodeId", nodeId);
    data.emplace("watching", true);
    cb(seq, makeDataPayload(kSourceConnectionTypeWatch, data), QueuedResponse{});
  }

  void Iroh::openBidirectionalStream (
    const String& seq,
    ID connectionId,
    ID sendStreamId,
    ID recvStreamId,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionOpenBi, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionOpenBi, "Unknown connection");
    }

    {
      Lock lock(this->mutex);
      if (this->sendStreams.count(sendStreamId) || this->recvStreams.count(recvStreamId)) {
        return this->respondError(cb, seq, kSourceConnectionOpenBi, "Stream already exists");
      }
    }

    this->queue.push([=, this]() {
      iroh::SendStream send;
      iroh::RecvStream recv;
      const auto result = connection->connection.openBi(send, recv);

      auto sendEntry = std::make_shared<SendStreamEntry>();
      sendEntry->id = sendStreamId;
      sendEntry->connectionId = connectionId;
      sendEntry->stream = std::move(send);

      auto recvEntry = std::make_shared<RecvStreamEntry>();
      recvEntry->id = recvStreamId;
      recvEntry->connectionId = connectionId;
      recvEntry->stream = std::move(recv);

      this->loop.dispatch([this, seq, cb, connectionId, sendEntry, recvEntry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionOpenBi, result, "Failed to open bidirectional stream"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->sendStreams.emplace(sendEntry->id, sendEntry);
          this->recvStreams.emplace(recvEntry->id, recvEntry);
        }

        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(connectionId));
        data.emplace("sendStreamId", std::to_string(sendEntry->id));
        data.emplace("recvStreamId", std::to_string(recvEntry->id));
        cb(seq, makeDataPayload(kSourceConnectionOpenBi, data), QueuedResponse{});
      });
    });
  }

  void Iroh::openUnidirectionalStream (
    const String& seq,
    ID connectionId,
    ID sendStreamId,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionOpenUni, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionOpenUni, "Unknown connection");
    }

    {
      Lock lock(this->mutex);
      if (this->sendStreams.count(sendStreamId)) {
        return this->respondError(cb, seq, kSourceConnectionOpenUni, "Stream already exists");
      }
    }

    this->queue.push([=, this]() {
      iroh::SendStream send;
      const auto result = connection->connection.openUni(send);

      auto sendEntry = std::make_shared<SendStreamEntry>();
      sendEntry->id = sendStreamId;
      sendEntry->connectionId = connectionId;
      sendEntry->stream = std::move(send);

      this->loop.dispatch([this, seq, cb, connectionId, sendEntry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionOpenUni, result, "Failed to open unidirectional stream"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->sendStreams.emplace(sendEntry->id, sendEntry);
        }

        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(connectionId));
        data.emplace("sendStreamId", std::to_string(sendEntry->id));
        cb(seq, makeDataPayload(kSourceConnectionOpenUni, data), QueuedResponse{});
      });
    });
  }

  void Iroh::acceptBidirectionalStream (
    const String& seq,
    ID connectionId,
    ID sendStreamId,
    ID recvStreamId,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionAcceptBi, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionAcceptBi, "Unknown connection");
    }

    {
      Lock lock(this->mutex);
      if (this->sendStreams.count(sendStreamId) || this->recvStreams.count(recvStreamId)) {
        return this->respondError(cb, seq, kSourceConnectionAcceptBi, "Stream already exists");
      }
    }

    this->queue.push([=, this]() {
      iroh::SendStream send;
      iroh::RecvStream recv;
      const auto result = connection->connection.acceptBi(send, recv);

      auto sendEntry = std::make_shared<SendStreamEntry>();
      sendEntry->id = sendStreamId;
      sendEntry->connectionId = connectionId;
      sendEntry->stream = std::move(send);

      auto recvEntry = std::make_shared<RecvStreamEntry>();
      recvEntry->id = recvStreamId;
      recvEntry->connectionId = connectionId;
      recvEntry->stream = std::move(recv);

      this->loop.dispatch([this, seq, cb, connectionId, sendEntry, recvEntry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionAcceptBi, result, "Failed to accept bidirectional stream"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->sendStreams.emplace(sendEntry->id, sendEntry);
          this->recvStreams.emplace(recvEntry->id, recvEntry);
        }

        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(connectionId));
        data.emplace("sendStreamId", std::to_string(sendEntry->id));
        data.emplace("recvStreamId", std::to_string(recvEntry->id));
        cb(seq, makeDataPayload(kSourceConnectionAcceptBi, data), QueuedResponse{});
      });
    });
  }

  void Iroh::acceptUnidirectionalStream (
    const String& seq,
    ID connectionId,
    ID recvStreamId,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceConnectionAcceptUni, "Iroh FFI unavailable");
    }

    auto connection = this->getConnection(connectionId);
    if (!connection) {
      return this->respondError(cb, seq, kSourceConnectionAcceptUni, "Unknown connection");
    }

    {
      Lock lock(this->mutex);
      if (this->recvStreams.count(recvStreamId)) {
        return this->respondError(cb, seq, kSourceConnectionAcceptUni, "Stream already exists");
      }
    }

    this->queue.push([=, this]() {
      iroh::RecvStream recv;
      const auto result = connection->connection.acceptUni(recv);

      auto recvEntry = std::make_shared<RecvStreamEntry>();
      recvEntry->id = recvStreamId;
      recvEntry->connectionId = connectionId;
      recvEntry->stream = std::move(recv);

      this->loop.dispatch([this, seq, cb, connectionId, recvEntry, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceConnectionAcceptUni, result, "Failed to accept unidirectional stream"), QueuedResponse{});
        }

        {
          Lock lock(this->mutex);
          this->recvStreams.emplace(recvEntry->id, recvEntry);
        }

        JSON::Object::Entries data;
        data.emplace("connectionId", std::to_string(connectionId));
        data.emplace("recvStreamId", std::to_string(recvEntry->id));
        cb(seq, makeDataPayload(kSourceConnectionAcceptUni, data), QueuedResponse{});
      });
    });
  }

  void Iroh::sendStreamWrite (
    const String& seq,
    ID streamId,
    const Vector<uint8_t>& data,
    const StreamWriteOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceStreamWrite, "Iroh FFI unavailable");
    }

    auto stream = this->getSendStream(streamId);
    if (!stream) {
      return this->respondError(cb, seq, kSourceStreamWrite, "Unknown send stream");
    }

    this->queue.push([=, this]() {
      const auto result = stream->stream.write(data, options.timeoutMs);

      this->loop.dispatch([this, seq, cb, streamId, result]() {
        if (result != iroh::EndpointResult::Ok) {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Stream write timed out")
            : String("Failed to write stream");
          return cb(seq, makeEndpointError(kSourceStreamWrite, result, message), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("streamId", std::to_string(streamId));
        cb(seq, makeDataPayload(kSourceStreamWrite, payload), QueuedResponse{});
      });
    });
  }

  void Iroh::sendStreamFinish (const String& seq, ID streamId, const Callback cb) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceStreamFinish, "Iroh FFI unavailable");
    }

    auto stream = this->getSendStream(streamId);
    if (!stream) {
      return this->respondError(cb, seq, kSourceStreamFinish, "Unknown send stream");
    }

    this->queue.push([=, this]() {
      const auto result = stream->stream.finish();
      stream->finished = true;

      this->loop.dispatch([this, seq, cb, streamId, result]() {
        if (result != iroh::EndpointResult::Ok) {
          return cb(seq, makeEndpointError(kSourceStreamFinish, result, "Failed to finish stream"), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("streamId", std::to_string(streamId));
        cb(seq, makeDataPayload(kSourceStreamFinish, payload), QueuedResponse{});
      });
    });
  }

  void Iroh::recvStreamRead (
    const String& seq,
    ID streamId,
    const StreamReadOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceStreamRead, "Iroh FFI unavailable");
    }

    auto stream = this->getRecvStream(streamId);
    if (!stream) {
      return this->respondError(cb, seq, kSourceStreamRead, "Unknown recv stream");
    }

    if (options.length == 0) {
      return this->respondError(cb, seq, kSourceStreamRead, "Invalid length");
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> buffer(options.length);
      const auto result = stream->stream.read(buffer, options.timeoutMs);

      this->loop.dispatch([this, seq, cb, streamId, result, buffer = std::move(buffer)]() mutable {
        if (result != iroh::EndpointResult::Ok) {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Stream read timed out")
            : String("Failed to read stream");
          return cb(seq, makeEndpointError(kSourceStreamRead, result, message), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("streamId", std::to_string(streamId));
        payload.emplace("data", bytes::base64::encode(buffer));
        cb(seq, makeDataPayload(kSourceStreamRead, payload), QueuedResponse{});
      });
    });
  }

  void Iroh::recvStreamReadToEnd (
    const String& seq,
    ID streamId,
    const StreamReadToEndOptions& options,
    const Callback cb
  ) {
    if (!this->hasLibrary()) {
      return this->respondError(cb, seq, kSourceStreamReadToEnd, "Iroh FFI unavailable");
    }

    auto stream = this->getRecvStream(streamId);
    if (!stream) {
      return this->respondError(cb, seq, kSourceStreamReadToEnd, "Unknown recv stream");
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> buffer;
      const auto result = stream->stream.readToEnd(buffer, options.sizeLimit, options.timeoutMs);

      this->loop.dispatch([this, seq, cb, streamId, result, buffer = std::move(buffer)]() mutable {
        if (result != iroh::EndpointResult::Ok) {
          const String message = result == iroh::EndpointResult::Timeout
            ? String("Stream read timed out")
            : String("Failed to read stream");
          return cb(seq, makeEndpointError(kSourceStreamReadToEnd, result, message), QueuedResponse{});
        }

        JSON::Object::Entries payload;
        payload.emplace("streamId", std::to_string(streamId));
        payload.emplace("data", bytes::base64::encode(buffer));
        cb(seq, makeDataPayload(kSourceStreamReadToEnd, payload), QueuedResponse{});
      });
    });
  }
#else
  Iroh::Iroh (const Options& options)
    : core::Service(options),
      allowService(options.enabled),
      cachedLogLevel(static_cast<int>(iroh::LogLevel::Info)) {
    this->enabled = false;
  }

  Iroh::~Iroh () {
    this->stop();
  }

  bool Iroh::hasLibrary () const {
    return false;
  }

  bool Iroh::start () {
    this->enabled = false;
    return false;
  }

  bool Iroh::stop () {
    this->enabled = false;
    return false;
  }

  void Iroh::respondError (
    const Callback& cb,
    const String& seq,
    const String& source,
    const String& message,
    const String& code
  ) {
    cb(seq, makeErrorPayload(source, message, code), QueuedResponse{});
  }

  JSON::Object::Entries Iroh::makeStatusData () const {
    JSON::Object::Entries data;
    data.emplace("initialized", false);
    data.emplace("version", String());

    const auto levelValue = this->cachedLogLevel.load();
    const auto level = static_cast<iroh::LogLevel>(levelValue);
    JSON::Object::Entries logLevel;
    logLevel.emplace("value", levelValue);
    logLevel.emplace("name", String(iroh::toString(level)));
    data.emplace("logLevel", logLevel);
    return data;
  }

  void Iroh::init (const String& seq, const Callback cb) {
    this->respondError(cb, seq, kSourceInit, "Iroh FFI unavailable");
  }

  void Iroh::shutdown (const String& seq, const Callback cb) {
    this->respondError(cb, seq, kSourceShutdown, "Iroh FFI unavailable");
  }

  void Iroh::status (const String& seq, const Callback cb) {
    cb(seq, makeDataPayload(kSourceStatus, this->makeStatusData()), QueuedResponse{});
  }

  void Iroh::setLogLevel (const String& seq, iroh::LogLevel, const Callback cb) {
    this->respondError(cb, seq, kSourceSetLogLevel, "Iroh FFI unavailable");
  }

  void Iroh::pathToKey (
    const String& seq,
    const String&,
    const std::optional<String>&,
    const std::optional<String>&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourcePathToKey, "Iroh FFI unavailable");
  }

  void Iroh::keyToPath (
    const String& seq,
    const Vector<uint8_t>&,
    const std::optional<String>&,
    const std::optional<String>&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceKeyToPath, "Iroh FFI unavailable");
  }

  void Iroh::createEndpoint (
    const String& seq,
    ID,
    const EndpointOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceEndpointCreate, "Iroh FFI unavailable");
  }

  void Iroh::destroyEndpoint (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceEndpointDestroy, "Iroh FFI unavailable");
  }

  void Iroh::bindEndpoint (
    const String& seq,
    ID,
    const BindOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceEndpointBind, "Iroh FFI unavailable");
  }

  void Iroh::getHomeRelay (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceEndpointHomeRelay, "Iroh FFI unavailable");
  }

  void Iroh::getNodeAddr (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceEndpointNodeAddr, "Iroh FFI unavailable");
  }

  void Iroh::connect (
    const String& seq,
    ID,
    ID,
    const ConnectOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionConnect, "Iroh FFI unavailable");
  }

  void Iroh::accept (
    const String& seq,
    ID,
    ID,
    const AcceptOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionAccept, "Iroh FFI unavailable");
  }

  void Iroh::acceptAny (const String& seq, ID, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceConnectionAcceptAny, "Iroh FFI unavailable");
  }

  void Iroh::closeEndpoint (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceEndpointClose, "Iroh FFI unavailable");
  }

  void Iroh::closeConnection (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceConnectionClose, "Iroh FFI unavailable");
  }

  void Iroh::waitConnectionClosed (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceConnectionWaitClosed, "Iroh FFI unavailable");
  }

  void Iroh::connectionStats (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceConnectionStats, "Iroh FFI unavailable");
  }

  void Iroh::writeDatagram (
    const String& seq,
    ID,
    const Vector<uint8_t>&,
    const DatagramOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionDatagramWrite, "Iroh FFI unavailable");
  }

  void Iroh::readDatagram (
    const String& seq,
    ID,
    const DatagramOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionDatagramRead, "Iroh FFI unavailable");
  }

  void Iroh::watchConnectionType (
    const String& seq,
    ID,
    const String&,
    bool,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionTypeWatch, "Iroh FFI unavailable");
  }

  void Iroh::openBidirectionalStream (
    const String& seq,
    ID,
    ID,
    ID,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionOpenBi, "Iroh FFI unavailable");
  }

  void Iroh::openUnidirectionalStream (
    const String& seq,
    ID,
    ID,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionOpenUni, "Iroh FFI unavailable");
  }

  void Iroh::acceptBidirectionalStream (
    const String& seq,
    ID,
    ID,
    ID,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionAcceptBi, "Iroh FFI unavailable");
  }

  void Iroh::acceptUnidirectionalStream (
    const String& seq,
    ID,
    ID,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceConnectionAcceptUni, "Iroh FFI unavailable");
  }

  void Iroh::sendStreamWrite (
    const String& seq,
    ID,
    const Vector<uint8_t>&,
    const StreamWriteOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceStreamWrite, "Iroh FFI unavailable");
  }

  void Iroh::sendStreamFinish (const String& seq, ID, const Callback cb) {
    this->respondError(cb, seq, kSourceStreamFinish, "Iroh FFI unavailable");
  }

  void Iroh::recvStreamRead (
    const String& seq,
    ID,
    const StreamReadOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceStreamRead, "Iroh FFI unavailable");
  }

  void Iroh::recvStreamReadToEnd (
    const String& seq,
    ID,
    const StreamReadToEndOptions&,
    const Callback cb
  ) {
    this->respondError(cb, seq, kSourceStreamReadToEnd, "Iroh FFI unavailable");
  }
#endif
} // namespace oro::runtime::core::services
