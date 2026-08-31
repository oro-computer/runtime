#ifndef ORO_RUNTIME_IROH_H
#define ORO_RUNTIME_IROH_H

#include "platform.hh"
#include "string.hh"

#include <functional>
#include <memory>
#include <mutex>
#include <cctype>
#include <array>
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <thread>

#include "iroh/uniffi.hh"
#include "iroh/uniffi_manager.hh"

namespace oro::runtime::iroh {
  using types::Lock;
  using types::Mutex;
  using types::String;
  using types::Vector;

  enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
  };

  enum class EndpointResult : uint8_t {
    Ok = 0,
    BindError = 1,
    AcceptFailed = 2,
    AcceptUniFailed = 3,
    AcceptBiFailed = 4,
    ConnectUniError = 5,
    ConnectBiError = 6,
    ConnectError = 7,
    AddrError = 8,
    SendError = 9,
    ReadError = 10,
    Timeout = 11,
    CloseError = 12,
    IncomingError = 13,
    ConnectionTypeError = 14
  };

  enum class AddrResult : uint8_t {
    Ok = 0,
    InvalidUrl = 1,
    InvalidSocketAddr = 2,
    InvalidNodeAddr = 3
  };

  enum class KeyResult : uint8_t {
    Ok = 0,
    InvalidPublicKey = 1,
    InvalidSecretKey = 2
  };

  enum class ConnectionType : uint8_t {
    Direct = 0,
    Relay = 1,
    Mixed = 2,
    None = 3
  };

  enum class RelayMode : uint8_t {
    Disabled = 0,
    Default = 1
  };

  enum class DiscoveryConfig : uint8_t {
    Default = 0,
    None = 1
  };

  inline bool decodeBase32 (const String& literal, Vector<uint8_t>& out) {
    static const std::array<int8_t, 256> table = []() {
      std::array<int8_t, 256> map{};
      map.fill(-1);
      const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
      for (int i = 0; i < 32; ++i) {
        const unsigned char upper = static_cast<unsigned char>(alphabet[i]);
        const unsigned char lower = static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(alphabet[i]))
        );
        map[upper] = static_cast<int8_t>(i);
        map[lower] = static_cast<int8_t>(i);
      }
      map[static_cast<unsigned char>('=')] = 0;
      return map;
    }();

    Vector<uint8_t> buffer;
    buffer.reserve((literal.size() * 5) / 8);
    uint32_t bits = 0;
    int bitCount = 0;

    for (auto ch : literal) {
      if (ch == '=') {
        break;
      }
      int8_t value = table[static_cast<unsigned char>(ch)];
      if (value < 0) {
        return false;
      }
      bits = (bits << 5) | static_cast<uint32_t>(value);
      bitCount += 5;
      if (bitCount >= 8) {
        bitCount -= 8;
        buffer.push_back(static_cast<uint8_t>((bits >> bitCount) & 0xFF));
      }
    }

    out = std::move(buffer);
    return true;
  }

  struct Result {
    int code = 0;
    String message;
    inline bool ok () const {
      return code == 0;
    }
  };

  inline const char* toString (LogLevel level) {
    switch (level) {
      case LogLevel::Trace: return "trace";
      case LogLevel::Debug: return "debug";
      case LogLevel::Info:  return "info";
      case LogLevel::Warn:  return "warn";
      case LogLevel::Error: return "error";
      case LogLevel::Off:   return "off";
    }
    return "info";
  }

  inline const char* toString (EndpointResult result) {
    switch (result) {
      case EndpointResult::Ok: return "ok";
      case EndpointResult::BindError: return "bind-error";
      case EndpointResult::AcceptFailed: return "accept-failed";
      case EndpointResult::AcceptUniFailed: return "accept-uni-failed";
      case EndpointResult::AcceptBiFailed: return "accept-bi-failed";
      case EndpointResult::ConnectUniError: return "connect-uni-error";
      case EndpointResult::ConnectBiError: return "connect-bi-error";
      case EndpointResult::ConnectError: return "connect-error";
      case EndpointResult::AddrError: return "addr-error";
      case EndpointResult::SendError: return "send-error";
      case EndpointResult::ReadError: return "read-error";
      case EndpointResult::Timeout: return "timeout";
      case EndpointResult::CloseError: return "close-error";
      case EndpointResult::IncomingError: return "incoming-error";
      case EndpointResult::ConnectionTypeError: return "connection-type-error";
    }
    return "unknown";
  }

  inline const char* toString (AddrResult result) {
    switch (result) {
      case AddrResult::Ok: return "ok";
      case AddrResult::InvalidUrl: return "invalid-url";
      case AddrResult::InvalidSocketAddr: return "invalid-socketaddr";
      case AddrResult::InvalidNodeAddr: return "invalid-nodeaddr";
    }
    return "unknown";
  }

  inline const char* toString (KeyResult result) {
    switch (result) {
      case KeyResult::Ok: return "ok";
      case KeyResult::InvalidPublicKey: return "invalid-public-key";
      case KeyResult::InvalidSecretKey: return "invalid-secret-key";
    }
    return "unknown";
  }

  inline const char* toString (ConnectionType type) {
    switch (type) {
      case ConnectionType::Direct: return "direct";
      case ConnectionType::Relay: return "relay";
      case ConnectionType::Mixed: return "mixed";
      case ConnectionType::None: return "none";
    }
    return "unknown";
  }

  inline bool fromString (const String& level, LogLevel& out) {
    const auto lowered = runtime::string::toLowerCase(level);
    static const Vector<std::pair<String, LogLevel>> table {
      {"trace", LogLevel::Trace},
      {"debug", LogLevel::Debug},
      {"info",  LogLevel::Info},
      {"warn",  LogLevel::Warn},
      {"error", LogLevel::Error},
      {"off",   LogLevel::Off}
    };
    for (const auto& entry : table) {
      if (lowered == entry.first) {
        out = entry.second;
        return true;
      }
    }
    return false;
  }

  class SecretKey {
    public:
      SecretKey () = default;
      explicit SecretKey (String encoded) : literal(std::move(encoded)) {}

      static std::pair<SecretKey, KeyResult> fromBase32 (const String& value) {
        if (value.empty()) {
          return {SecretKey(), KeyResult::InvalidSecretKey};
        }

        Vector<uint8_t> decoded;
        if (!decodeBase32(value, decoded) || decoded.size() != 32) {
          return {SecretKey(), KeyResult::InvalidSecretKey};
        }

        return {SecretKey(value), KeyResult::Ok};
      }

      String toBase32 () const {
        return literal;
      }

      bool empty () const {
        return literal.empty();
      }

    private:
      String literal;
  };

  class PublicKey {
    public:
      PublicKey () = default;
      explicit PublicKey (String encoded) : literal(std::move(encoded)) {}

      static std::pair<PublicKey, KeyResult> fromBase32 (const String& value) {
        if (value.empty()) {
          return {PublicKey(), KeyResult::InvalidPublicKey};
        }

        Vector<uint8_t> decoded;
        if (!decodeBase32(value, decoded) || decoded.size() != 32) {
          return {PublicKey(), KeyResult::InvalidPublicKey};
        }

        return {PublicKey(value), KeyResult::Ok};
      }

      String toBase32 () const {
        return literal;
      }

      bool empty () const {
        return literal.empty();
      }

    private:
      String literal;
  };

  class SocketAddrV4 {
    public:
      SocketAddrV4 () = default;
      explicit SocketAddrV4 (String addr) : literal(std::move(addr)) {}

      static std::pair<SocketAddrV4, AddrResult> fromString (const String& value) {
        if (value.empty()) {
          return {SocketAddrV4(), AddrResult::InvalidSocketAddr};
        }
        return {SocketAddrV4(value), AddrResult::Ok};
      }

      String toString () const {
        return literal;
      }

    private:
      String literal;
  };

  class SocketAddrV6 {
    public:
      SocketAddrV6 () = default;
      explicit SocketAddrV6 (String addr) : literal(std::move(addr)) {}

      static std::pair<SocketAddrV6, AddrResult> fromString (const String& value) {
        if (value.empty()) {
          return {SocketAddrV6(), AddrResult::InvalidSocketAddr};
        }
        return {SocketAddrV6(value), AddrResult::Ok};
      }

      String toString () const {
        return literal;
      }

    private:
      String literal;
  };

  class Url {
    public:
      Url () = default;
      explicit Url (String value) : literal(std::move(value)) {}

      String toString () const {
        return literal;
      }

      void set (String value) {
        literal = std::move(value);
      }

    private:
      String literal;
  };

  class NodeAddr {
    public:
      NodeAddr () = default;
      explicit NodeAddr (String value) : literal(std::move(value)) {}

      static std::pair<NodeAddr, AddrResult> fromString (const String& value) {
        if (value.empty()) {
          return {NodeAddr(), AddrResult::InvalidNodeAddr};
        }
        return {NodeAddr(value), AddrResult::Ok};
      }

      String toString () const {
        return literal;
      }

    private:
      String literal;
  };

  class EndpointConfig {
    public:
      EndpointConfig () = default;

      void setSecretKey (SecretKey&& key) {
        secretKey = key.toBase32();
      }

      void setRelayMode (RelayMode mode) {
        relay = mode;
      }

      void setDiscovery (DiscoveryConfig config) {
        discovery = config;
      }

      void addAlpn (const Vector<uint8_t>& value) {
        alpns.push_back(value);
      }

      const std::optional<String>& secret () const {
        return secretKey;
      }

      const Vector<Vector<uint8_t>>& alpnValues () const {
        return alpns;
      }

      const std::optional<RelayMode>& relayMode () const {
        return relay;
      }

      const std::optional<DiscoveryConfig>& discoveryMode () const {
        return discovery;
      }

    private:
      std::optional<String> secretKey;
      std::optional<RelayMode> relay;
      std::optional<DiscoveryConfig> discovery;
      Vector<Vector<uint8_t>> alpns;
  };

  inline uniffi::NodeOptions::Discovery toUniffiDiscovery (
    const std::optional<DiscoveryConfig>& config
  ) {
    if (config && *config == DiscoveryConfig::None) {
      return uniffi::NodeOptions::Discovery::None;
    }
    return uniffi::NodeOptions::Discovery::Default;
  }

  class SendStream {
    public:
      SendStream () = default;
      explicit SendStream (std::shared_ptr<uniffi::SendStream> handle) : handle(std::move(handle)) {}

      SendStream (SendStream&& other) noexcept {
        std::scoped_lock lock(other.mutex);
        handle = std::move(other.handle);
      }

      SendStream& operator = (SendStream&& other) noexcept {
        if (this != &other) {
          std::scoped_lock lock(this->mutex, other.mutex);
          handle = std::move(other.handle);
        }
        return *this;
      }

      SendStream (const SendStream&) = delete;
      SendStream& operator = (const SendStream&) = delete;

      EndpointResult write (
        const Vector<uint8_t>& data,
        const std::optional<uint64_t>& timeoutMs = std::nullopt
      ) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::SendError;
        }
        auto result = this->handle->write(data, timeoutMs);
        if (result.ok()) {
          return EndpointResult::Ok;
        }
        if (uniffi::isTimeout(result.error)) {
          return EndpointResult::Timeout;
        }
        return EndpointResult::SendError;
      }

      EndpointResult finish () {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::CloseError;
        }
        auto result = this->handle->finish();
        return result.ok() ? EndpointResult::Ok : EndpointResult::CloseError;
      }

      String id () const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return String();
        }
        auto result = this->handle->id();
        return result.ok() ? result.value : String();
      }

    private:
      mutable Mutex mutex;
      std::shared_ptr<uniffi::SendStream> handle;
  };

  class RecvStream {
    public:
      RecvStream () = default;
      explicit RecvStream (std::shared_ptr<uniffi::RecvStream> handle) : handle(std::move(handle)) {}

      RecvStream (RecvStream&& other) noexcept {
        std::scoped_lock lock(other.mutex);
        handle = std::move(other.handle);
      }

      RecvStream& operator = (RecvStream&& other) noexcept {
        if (this != &other) {
          std::scoped_lock lock(this->mutex, other.mutex);
          handle = std::move(other.handle);
        }
        return *this;
      }

      RecvStream (const RecvStream&) = delete;
      RecvStream& operator = (const RecvStream&) = delete;

      EndpointResult read (
        Vector<uint8_t>& buffer,
        const std::optional<uint64_t>& timeoutMs = std::nullopt
      ) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::ReadError;
        }
        auto result = this->handle->read(static_cast<uint32_t>(buffer.size()), timeoutMs);
        if (!result.ok()) {
          if (uniffi::isTimeout(result.error)) {
            return EndpointResult::Timeout;
          }
          return EndpointResult::ReadError;
        }
        buffer = std::move(result.value);
        return EndpointResult::Ok;
      }

      EndpointResult readToEnd (
        Vector<uint8_t>& buffer,
        size_t sizeLimit,
        const std::optional<uint64_t>& timeoutMs = std::nullopt
      ) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::ReadError;
        }
        auto result = this->handle->readToEnd(static_cast<uint32_t>(sizeLimit), timeoutMs);
        if (!result.ok()) {
          if (uniffi::isTimeout(result.error)) {
            return EndpointResult::Timeout;
          }
          return EndpointResult::ReadError;
        }
        buffer = std::move(result.value);
        return EndpointResult::Ok;
      }

      String id () const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return String();
        }
        auto result = this->handle->id();
        return result.ok() ? result.value : String();
      }

    private:
      mutable Mutex mutex;
      std::shared_ptr<uniffi::RecvStream> handle;
  };


  class Connection {
    public:
      Connection () = default;
      explicit Connection (std::shared_ptr<uniffi::Connection> handle)
        : handle(std::move(handle)) {}

      Connection (std::shared_ptr<uniffi::Connection> handle, Vector<uint8_t> negotiated)
        : handle(std::move(handle)), negotiatedAlpnCache(std::move(negotiated)), negotiatedCached(true) {}

      Connection (Connection&& other) noexcept {
        std::scoped_lock lock(other.mutex);
        handle = std::move(other.handle);
        closed = other.closed;
        other.closed = true;
      }

      Connection& operator = (Connection&& other) noexcept {
        if (this != &other) {
          std::scoped_lock lock(this->mutex, other.mutex);
          handle = std::move(other.handle);
          closed = other.closed;
          negotiatedAlpnCache = std::move(other.negotiatedAlpnCache);
          negotiatedCached = other.negotiatedCached;
          other.closed = true;
          other.negotiatedCached = true;
          other.negotiatedAlpnCache.clear();
        }
        return *this;
      }

      Connection (const Connection&) = delete;
      Connection& operator = (const Connection&) = delete;

      EndpointResult close (uint64_t errorCode = 0, Vector<uint8_t> reason = {}) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::CloseError;
        }
        if (this->closed) {
          return EndpointResult::Ok;
        }
        auto result = this->handle->close(errorCode, reason);
        if (!result.ok()) {
          return EndpointResult::CloseError;
        }
        this->closed = true;
        return EndpointResult::Ok;
      }

      EndpointResult waitClosed () {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::CloseError;
        }

        auto result = this->handle->waitClosed();
        if (!result.ok()) {
          return EndpointResult::CloseError;
        }
        this->closed = true;
        return EndpointResult::Ok;
      }

      EndpointResult writeDatagram (
        const Vector<uint8_t>& data,
        const std::optional<uint64_t>& timeoutMs = std::nullopt
      ) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::SendError;
        }
        auto result = this->handle->sendDatagram(data, timeoutMs);
        if (result.ok()) {
          return EndpointResult::Ok;
        }
        if (uniffi::isTimeout(result.error)) {
          return EndpointResult::Timeout;
        }
        return EndpointResult::SendError;
      }

      EndpointResult readDatagram (
        Vector<uint8_t>& out,
        const std::optional<uint64_t>& timeoutMs = std::nullopt
      ) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::ReadError;
        }
        auto result = this->handle->readDatagram(timeoutMs);
        if (!result.ok()) {
          if (uniffi::isTimeout(result.error)) {
            return EndpointResult::Timeout;
          }
          return EndpointResult::ReadError;
        }
        out = std::move(result.value);
        return EndpointResult::Ok;
      }

      EndpointResult negotiatedAlpn (Vector<uint8_t>& out) const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::ReadError;
        }
        if (!this->negotiatedCached) {
          auto result = this->handle->alpn();
          if (!result.ok()) {
            if (uniffi::isTimeout(result.error)) {
              return EndpointResult::Timeout;
            }
            return EndpointResult::ReadError;
          }
          if (result.value.has_value()) {
            this->negotiatedAlpnCache = result.value.value();
          } else {
            this->negotiatedAlpnCache.clear();
          }
          this->negotiatedCached = true;
        }

        out = this->negotiatedAlpnCache;
        return EndpointResult::Ok;
      }

      EndpointResult openBi (SendStream& send, RecvStream& recv) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::AcceptBiFailed;
        }
        auto result = this->handle->openBi();
        if (!result.ok()) {
          return EndpointResult::AcceptBiFailed;
        }
        send = SendStream(std::make_shared<uniffi::SendStream>(std::move(result.value.first)));
        recv = RecvStream(std::make_shared<uniffi::RecvStream>(std::move(result.value.second)));
        return EndpointResult::Ok;
      }

      EndpointResult openUni (SendStream& send) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::ConnectUniError;
        }
        auto result = this->handle->openUni();
        if (!result.ok()) {
          return EndpointResult::ConnectUniError;
        }
        send = SendStream(std::make_shared<uniffi::SendStream>(std::move(result.value)));
        return EndpointResult::Ok;
      }

      EndpointResult acceptBi (SendStream& send, RecvStream& recv) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::AcceptBiFailed;
        }
        auto result = this->handle->acceptBi();
        if (!result.ok()) {
          return EndpointResult::AcceptBiFailed;
        }
        send = SendStream(std::make_shared<uniffi::SendStream>(std::move(result.value.first)));
        recv = RecvStream(std::make_shared<uniffi::RecvStream>(std::move(result.value.second)));
        return EndpointResult::Ok;
      }

      EndpointResult acceptUni (RecvStream& recv) {
        Lock lock(this->mutex);
        if (!this->handle) {
          return EndpointResult::AcceptUniFailed;
        }
        auto result = this->handle->acceptUni();
        if (!result.ok()) {
          return EndpointResult::AcceptUniFailed;
        }
        recv = RecvStream(std::make_shared<uniffi::RecvStream>(std::move(result.value)));
        return EndpointResult::Ok;
      }

      EndpointResult stats () const {
        return EndpointResult::Ok;
      }

      size_t maxDatagramSize () const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return 0;
        }
        auto result = this->handle->maxDatagramSize();
        return result.ok() ? result.value : 0;
      }

      uint64_t rtt () const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return 0;
        }
        auto result = this->handle->rtt();
        return result.ok() ? result.value : 0;
      }

      double packetLoss () const {
        Lock lock(this->mutex);
        if (!this->handle) {
          return 0.0;
        }
        auto statsResult = this->handle->stats();
        if (!statsResult.ok()) {
          return 0.0;
        }
        const auto sent = statsResult.value.sentPackets;
        if (sent == 0) {
          return 0.0;
        }
        const auto lost = statsResult.value.lostPackets;
        return static_cast<double>(lost) / static_cast<double>(sent);
      }

    private:
      mutable Mutex mutex;
      std::shared_ptr<uniffi::Connection> handle;
      bool closed = false;
      mutable Vector<uint8_t> negotiatedAlpnCache;
      mutable bool negotiatedCached = false;
  };


  class Endpoint {
    public:
      using AcceptAnyCallback = std::function<void(EndpointResult, Vector<uint8_t>, Connection)>;
      using CallbackDispatcher = std::function<void(std::function<void()>)>;
      using ConnTypeCallback = std::function<void(EndpointResult, ConnectionType)>;

      Endpoint () = default;

      struct ConnectionTypeWatcherEntry {
        oro_iroh_conn_type_watcher_t* handle = nullptr;
        ConnTypeCallback callback;
        std::mutex mutex;
        bool active = true;
      };


      bool valid () const {
        Lock lock(this->mutex);
        return this->bound && !this->closed && this->endpointHandle != nullptr;
      }

      EndpointResult bind (const EndpointConfig& cfg, SocketAddrV4* ipv4, SocketAddrV6* ipv6) {
        Lock lock(this->mutex);

        if (this->bound && !this->closed && this->endpointHandle != nullptr) {
          return EndpointResult::Ok;
        }

        uniffi::NodeOptions options;
        options.enableDocs = false;
        options.discovery = toUniffiDiscovery(cfg.discoveryMode());
        if (ipv4 != nullptr) {
          options.ipv4Addr = ipv4->toString();
        }
        if (ipv6 != nullptr) {
          options.ipv6Addr = ipv6->toString();
        }

        if (cfg.secret()) {
          const auto literal = *cfg.secret();
          if (literal.empty()) {
            return EndpointResult::AddrError;
          }
          Vector<uint8_t> decoded;
          if (!decodeBase32(literal, decoded) || decoded.size() != 32) {
            return EndpointResult::AddrError;
          }
          options.secretKey.assign(decoded.begin(), decoded.end());
        }

        auto nodeResult = uniffi::Node::create(options);
        if (!nodeResult.ok()) {
          return EndpointResult::BindError;
        }

        auto netResult = nodeResult.value.net();
      if (!netResult.ok()) {
          return EndpointResult::BindError;
        }

        auto endpointResult = nodeResult.value.endpoint();
        if (!endpointResult.ok()) {
          return EndpointResult::BindError;
        }

        auto setAlpnResult = endpointResult.value.setAlpns(cfg.alpnValues());
        if (!setAlpnResult.ok()) {
          return EndpointResult::BindError;
        }

        this->nodeHandle = std::make_shared<uniffi::Node>(std::move(nodeResult.value));
        this->netHandle = std::make_shared<uniffi::Net>(std::move(netResult.value));
        this->endpointHandle = std::make_shared<uniffi::Endpoint>(std::move(endpointResult.value));
        this->config = cfg;
        this->bound = true;
        this->closed = false;
        return EndpointResult::Ok;
      }

      EndpointResult connect (const Vector<uint8_t>& alpn, NodeAddr& addr, Connection& connection) {
        Lock lock(this->mutex);
        if (!this->bound || this->closed || this->endpointHandle == nullptr) {
          return EndpointResult::ConnectError;
        }

        auto parsedAddr = uniffi::NodeAddr::fromString(addr.toString());
        if (!parsedAddr.ok()) {
          return EndpointResult::AddrError;
        }

        auto connResult = this->endpointHandle->connect(parsedAddr.value, alpn);
        if (!connResult.ok()) {
          return EndpointResult::ConnectError;
        }

        auto shared = std::make_shared<uniffi::Connection>(std::move(connResult.value));
        connection = Connection(shared);
        return EndpointResult::Ok;
      }

      EndpointResult accept (const Vector<uint8_t>& expectedAlpn, Connection& connection) {
        Lock lock(this->mutex);
        if (!this->bound || this->closed || this->endpointHandle == nullptr) {
          return EndpointResult::AcceptFailed;
        }

        std::optional<std::vector<uint8_t>> expected;
        if (!expectedAlpn.empty()) {
          expected = expectedAlpn;
        }

        auto acceptResult = this->endpointHandle->accept(expected);
        if (!acceptResult.ok()) {
          return EndpointResult::AcceptFailed;
        }

        auto& pair = acceptResult.value;
        auto shared = std::make_shared<uniffi::Connection>(std::move(pair.first));
        connection = Connection(shared, std::move(pair.second));
        return EndpointResult::Ok;
      }

      EndpointResult acceptAny (Vector<uint8_t>& negotiatedAlpn, Connection& connection) {
        Lock lock(this->mutex);
        if (!this->bound || this->closed || this->endpointHandle == nullptr) {
          return EndpointResult::AcceptFailed;
        }

        auto acceptResult = this->endpointHandle->acceptAny();
        if (!acceptResult.ok()) {
          return EndpointResult::AcceptFailed;
        }

        auto& pair = acceptResult.value;
        negotiatedAlpn = pair.second;
        auto shared = std::make_shared<uniffi::Connection>(std::move(pair.first));
        connection = Connection(shared, negotiatedAlpn);
        return EndpointResult::Ok;
      }

      void acceptAnyAsync (AcceptAnyCallback callback, CallbackDispatcher dispatcher) {
        if (!callback || !dispatcher) {
          return;
        }

        std::shared_ptr<uniffi::Endpoint> endpointHandleSnapshot;
        {
          Lock lock(this->mutex);
          if (!this->bound || this->closed || this->endpointHandle == nullptr) {
            callback(EndpointResult::AcceptFailed, Vector<uint8_t>{}, Connection{});
            return;
          }
          endpointHandleSnapshot = this->endpointHandle;
        }

        std::thread([handle = std::move(endpointHandleSnapshot), cb = std::move(callback), dispatcher = std::move(dispatcher)]() mutable {
          EndpointResult status = EndpointResult::AcceptFailed;
          Vector<uint8_t> negotiated;
          std::shared_ptr<Connection> connection;

          auto acceptResult = handle->acceptAny();
          if (acceptResult.ok()) {
            auto pair = std::move(acceptResult.value);
            negotiated.assign(pair.second.begin(), pair.second.end());
            auto sharedConn = std::make_shared<uniffi::Connection>(std::move(pair.first));
            connection = std::make_shared<Connection>(sharedConn, negotiated);
            status = EndpointResult::Ok;
          } else if (uniffi::isTimeout(acceptResult.error)) {
            status = EndpointResult::Timeout;
          }

          dispatcher([status, cb = std::move(cb), negotiated = std::move(negotiated), connection = std::move(connection)]() mutable {
            if (cb) {
              Connection result;
              if (connection) {
                result = std::move(*connection);
              }
              cb(status, std::move(negotiated), std::move(result));
            }
          });
        }).detach();
      }

      void watchConnectionType (const PublicKey& key, ConnTypeCallback callback) {
        Lock lock(this->mutex);
        const auto nodeId = key.toBase32();

        if (!callback) {
          this->stopConnectionTypeWatcherLocked(nodeId);
          return;
        }

        if (this->endpointHandle == nullptr) {
          callback(EndpointResult::ConnectionTypeError, ConnectionType::None);
          return;
        }

        this->stopConnectionTypeWatcherLocked(nodeId);

        auto& entryRef = this->connectionTypeWatchers[nodeId];
        if (!entryRef) {
          entryRef = std::make_shared<ConnectionTypeWatcherEntry>();
        }

        auto entry = entryRef;
        oro_iroh_conn_type_watcher_t* handleToCancel = nullptr;
        {
          std::lock_guard<std::mutex> guard(entry->mutex);
          entry->callback = callback;
          entry->active = true;
          handleToCancel = entry->handle;
          entry->handle = nullptr;
        }

        if (handleToCancel != nullptr) {
          oro_iroh_conn_type_watcher_cancel(handleToCancel);
        }

        oro_iroh_conn_type_watcher_t* rawWatcher = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_endpoint_watch_connection_type(
          this->endpointHandle->get(),
          nodeId.c_str(),
          &Endpoint::handleConnectionTypeEvent,
          entry.get(),
          &rawWatcher,
          &rawError
        );
        auto error = uniffi::detail::takeError(rawError);
        if (!ok || !error.ok()) {
          callback(EndpointResult::ConnectionTypeError, ConnectionType::None);
          {
            std::lock_guard<std::mutex> guard(entry->mutex);
            entry->active = false;
            entry->callback = {};
          }
          if (rawWatcher != nullptr) {
            oro_iroh_conn_type_watcher_cancel(rawWatcher);
          }
          return;
        }

        {
          std::lock_guard<std::mutex> guard(entry->mutex);
          entry->handle = rawWatcher;
        }
      }

      static void handleConnectionTypeEvent (
        void* userData,
        int32_t resultCode,
        int32_t typeCode,
        const char* /*directPtr*/,
        const char* /*relayPtr*/
      ) {
        auto* entry = reinterpret_cast<ConnectionTypeWatcherEntry*>(userData);
        if (entry == nullptr) {
          return;
        }

        ConnTypeCallback callback;
        {
          std::lock_guard<std::mutex> guard(entry->mutex);
          if (!entry->active || !entry->callback) {
            return;
          }
          callback = entry->callback;
        }

        if (!callback) {
          return;
        }

        EndpointResult result = EndpointResult::ConnectionTypeError;
        if (
          resultCode >= static_cast<int32_t>(EndpointResult::Ok) &&
          resultCode <= static_cast<int32_t>(EndpointResult::ConnectionTypeError)
        ) {
          result = static_cast<EndpointResult>(resultCode);
        }
        ConnectionType type = ConnectionType::None;
        switch (typeCode) {
          case 0: type = ConnectionType::Direct; break;
          case 1: type = ConnectionType::Relay; break;
          case 2: type = ConnectionType::Mixed; break;
          case 3: type = ConnectionType::None; break;
          default: type = ConnectionType::None; break;
        }

        callback(result, type);
      }

      EndpointResult homeRelay (Url& out) const {
        Lock lock(this->mutex);
        if (!this->netHandle) {
          return EndpointResult::AddrError;
        }

        auto relayResult = this->netHandle->homeRelay();
        if (!relayResult.ok()) {
          return EndpointResult::AddrError;
        }

        if (relayResult.value.has_value()) {
          out.set(relayResult.value.value());
        } else {
          out.set(String());
        }
        return EndpointResult::Ok;
      }

      EndpointResult nodeAddr (NodeAddr& out) const {
        Lock lock(this->mutex);
        if (!this->netHandle) {
          return EndpointResult::AddrError;
        }

        auto addrResult = this->netHandle->nodeAddr();
        if (!addrResult.ok()) {
          return EndpointResult::AddrError;
        }

        auto stringResult = addrResult.value.toString();
        if (!stringResult.ok()) {
          return EndpointResult::AddrError;
        }

        out = NodeAddr(stringResult.value);
        return EndpointResult::Ok;
      }

      EndpointResult networkChange () {
        Lock lock(this->mutex);
        if (!this->endpointHandle) {
          return EndpointResult::IncomingError;
        }
        auto result = this->endpointHandle->networkChange();
        if (!result.ok()) {
          return EndpointResult::IncomingError;
        }
        return EndpointResult::Ok;
      }

      EndpointResult close () {
        Lock lock(this->mutex);
        if (this->closed) {
          return EndpointResult::Ok;
        }
        if (!this->endpointHandle) {
          return EndpointResult::CloseError;
        }

        auto result = this->endpointHandle->close();
        if (!result.ok()) {
          return EndpointResult::CloseError;
        }

        for (const auto& watcherPair : this->connectionTypeWatchers) {
          this->stopConnectionTypeWatcherLocked(watcherPair.first);
        }
        this->connectionTypeWatchers.clear();
        this->endpointHandle.reset();
        this->netHandle.reset();
        this->nodeHandle.reset();
        this->closed = true;
        this->bound = false;
        return EndpointResult::Ok;
      }

    private:
      mutable Mutex mutex;
      bool bound = false;
      bool closed = false;
      EndpointConfig config;
      std::shared_ptr<uniffi::Node> nodeHandle;
      std::shared_ptr<uniffi::Net> netHandle;
      std::shared_ptr<uniffi::Endpoint> endpointHandle;
      std::unordered_map<String, std::shared_ptr<ConnectionTypeWatcherEntry>> connectionTypeWatchers;

      void stopConnectionTypeWatcherLocked (const String& nodeId) {
        auto it = this->connectionTypeWatchers.find(nodeId);
        if (it == this->connectionTypeWatchers.end()) {
          return;
        }

        auto entry = it->second;
        if (entry) {
          oro_iroh_conn_type_watcher_t* handle = nullptr;
          {
            std::lock_guard<std::mutex> guard(entry->mutex);
            entry->active = false;
            entry->callback = {};
            handle = entry->handle;
            entry->handle = nullptr;
          }
          if (handle != nullptr) {
            oro_iroh_conn_type_watcher_cancel(handle);
          }
        }
      }
  };

  class Library {
    public:
      static Library& shared ();

      bool init ();
      bool shutdown ();
      bool isInitialized () const;

      Result setLogLevel (LogLevel level);
      String version () const;
      Result pathToKey (
        const String& path,
        const std::optional<String>& prefix,
        const std::optional<String>& root,
        Vector<uint8_t>& out
      );
      Result keyToPath (
        const Vector<uint8_t>& key,
        const std::optional<String>& prefix,
        const std::optional<String>& root,
        String& out
      );

    private:
      Library ();
      ~Library ();

      Library (const Library&) = delete;
      Library (Library&&) = delete;
      Library& operator = (const Library&) = delete;
      Library& operator = (Library&&) = delete;

      mutable Mutex mutex;
      bool initialized = false;
#if ORO_RUNTIME_HAS_IROH_FFI
      uniffi::Manager manager;
#endif
  };
} // namespace oro::runtime::iroh

#endif // ORO_RUNTIME_IROH_H
