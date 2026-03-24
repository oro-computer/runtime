#ifndef ORO_RUNTIME_IROH_UNIFFI_H
#define ORO_RUNTIME_IROH_UNIFFI_H

#include "../platform.hh"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define ORO_IROH_HEADER_ALLOW_INCOMPLETE 1
#include <iroh/oro_iroh.h>
#undef ORO_IROH_HEADER_ALLOW_INCOMPLETE

namespace oro::runtime::iroh::uniffi {
  struct Error {
    int32_t code = 0;
    String message;

    inline bool ok () const {
      return code == 0;
    }

    inline explicit operator bool () const {
      return ok();
    }
  };

  template <typename T>
  struct Result {
    T value;
    Error error;

    inline bool ok () const {
      return error.ok();
    }

    inline explicit operator bool () const {
      return ok();
    }
  };

  inline bool isTimeout (const Error& error) {
    return error.code == ORO_IROH_ERROR_CODE_TIMEOUT;
  }

  struct ConnectionStats {
    uint64_t maxDatagramSize = 0;
    uint64_t rttMicros = 0;
    uint64_t lostPackets = 0;
    uint64_t sentPackets = 0;
  };

  namespace detail {
    inline String takeString (char* ptr) {
      if (ptr == nullptr) {
        return {};
      }
      String value(ptr);
      oro_iroh_string_free(ptr);
      return value;
    }

    inline Error takeError (oro_iroh_error_t& raw) {
      Error result;
      result.code = raw.code;
      if (raw.message != nullptr) {
        result.message = takeString(raw.message);
        raw.message = nullptr;
      }
      oro_iroh_error_clear(&raw);
      return result;
    }

    inline std::vector<uint8_t> takeBytes (oro_iroh_bytes_t& raw) {
      if (raw.data == nullptr || raw.len == 0) {
        oro_iroh_bytes_free(&raw);
        return {};
      }
      std::vector<uint8_t> out(raw.data, raw.data + raw.len);
      oro_iroh_bytes_free(&raw);
      return out;
    }
  } // namespace detail

  enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
  };

  struct NodeOptions {
    bool enableDocs = false;
    std::optional<uint64_t> gcIntervalMillis;
    std::optional<String> ipv4Addr;
    std::optional<String> ipv6Addr;
    enum class Discovery {
      Default = 0,
      None = 1
    } discovery = Discovery::Default;
    std::vector<uint8_t> secretKey;
  };

  class Node;
  class Net;
  class Endpoint;
  class Connection;
  class SendStream;
  class RecvStream;
  class NodeAddr;

  struct Unit {};

  class Node {
    public:
      Node () = default;
      explicit Node (oro_iroh_node_t* handle) : handle(handle) {}

      Node (Node&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      Node& operator = (Node&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      Node (const Node&) = delete;
      Node& operator = (const Node&) = delete;

      ~Node () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      inline explicit operator bool () const {
        return valid();
      }

      inline oro_iroh_node_t* get () const {
        return handle;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_node_free(handle);
          handle = nullptr;
        }
      }

      static Result<Node> create (const NodeOptions& options) {
        oro_iroh_node_options_t rawOptions{};
        rawOptions.enable_docs = options.enableDocs;
        if (options.gcIntervalMillis.has_value()) {
          rawOptions.has_gc_interval = true;
          rawOptions.gc_interval_millis = options.gcIntervalMillis.value();
        } else {
          rawOptions.has_gc_interval = false;
          rawOptions.gc_interval_millis = 0;
        }
        rawOptions.ipv4_addr = options.ipv4Addr ? options.ipv4Addr->c_str() : nullptr;
        rawOptions.ipv6_addr = options.ipv6Addr ? options.ipv6Addr->c_str() : nullptr;
        rawOptions.discovery = static_cast<int32_t>(options.discovery);
        rawOptions.secret_key = options.secretKey.empty() ? nullptr : options.secretKey.data();
        rawOptions.secret_key_len = options.secretKey.size();

        oro_iroh_node_t* handle = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_node_memory(&rawOptions, &handle, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Node>{{}, error};
        }
        return Result<Node>{Node(handle), error};
      }

      Result<Net> net () const;
      Result<Endpoint> endpoint () const;

    private:
      oro_iroh_node_t* handle = nullptr;
  };

  class Net {
    public:
      Net () = default;
      explicit Net (oro_iroh_net_t* handle) : handle(handle) {}

      Net (Net&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      Net& operator = (Net&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      Net (const Net&) = delete;
      Net& operator = (const Net&) = delete;

      ~Net () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_net_free(handle);
          handle = nullptr;
        }
      }

      Result<String> nodeId () const {
        char* raw = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_net_node_id(handle, &raw, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (raw != nullptr) {
            oro_iroh_string_free(raw);
          }
          return Result<String>{{}, error};
        }
        return Result<String>{detail::takeString(raw), error};
      }

      Result<NodeAddr> nodeAddr () const;

      Result<Unit> addNodeAddr (const NodeAddr& addr) const;
      Result<std::optional<String>> homeRelay () const {
        char* raw = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_net_home_relay(handle, &raw, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (raw != nullptr) {
            oro_iroh_string_free(raw);
          }
          return Result<std::optional<String>>{{}, error};
        }
        if (raw == nullptr) {
          return Result<std::optional<String>>{std::nullopt, error};
        }
        return Result<std::optional<String>>{detail::takeString(raw), error};
      }

      inline oro_iroh_net_t* get () const {
        return handle;
      }

    private:
      oro_iroh_net_t* handle = nullptr;
  };

  class NodeAddr {
    public:
      NodeAddr () = default;
      explicit NodeAddr (oro_iroh_node_addr_t* handle) : handle(handle) {}

      NodeAddr (NodeAddr&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      NodeAddr& operator = (NodeAddr&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      NodeAddr (const NodeAddr&) = delete;
      NodeAddr& operator = (const NodeAddr&) = delete;

      ~NodeAddr () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_node_addr_free(handle);
          handle = nullptr;
        }
      }

      static Result<NodeAddr> fromString (const String& value) {
        oro_iroh_node_addr_t* rawAddr = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_node_addr_from_string(value.c_str(), &rawAddr, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<NodeAddr>{{}, error};
        }
        return Result<NodeAddr>{NodeAddr(rawAddr), error};
      }

      Result<String> toString () const {
        char* raw = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_node_addr_to_string(handle, &raw, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (raw != nullptr) {
            oro_iroh_string_free(raw);
          }
          return Result<String>{{}, error};
        }
        return Result<String>{detail::takeString(raw), error};
      }

      inline oro_iroh_node_addr_t* get () const {
        return handle;
      }

    private:
      oro_iroh_node_addr_t* handle = nullptr;
  };

  class SendStream {
    public:
      SendStream () = default;
      explicit SendStream (oro_iroh_send_stream_t* handle) : handle(handle) {}

      SendStream (SendStream&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      SendStream& operator = (SendStream&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      SendStream (const SendStream&) = delete;
      SendStream& operator = (const SendStream&) = delete;

      ~SendStream () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_send_stream_free(handle);
          handle = nullptr;
        }
      }

      Result<uint64_t> write (
        const std::vector<uint8_t>& data,
        std::optional<uint64_t> timeoutMs = std::nullopt
      ) const {
        uint64_t written = 0;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_send_stream_write(
          handle,
          data.empty() ? nullptr : data.data(),
          data.size(),
          timeoutMs.value_or(0),
          &written,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<uint64_t>{{}, error};
        }
        return Result<uint64_t>{written, error};
      }

      Result<Unit> writeAll (const std::vector<uint8_t>& data) const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_send_stream_write_all(
          handle,
          data.data(),
          data.size(),
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<Unit> finish () const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_send_stream_finish(handle, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<String> id () const {
        char* raw = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_send_stream_id(handle, &raw, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (raw != nullptr) {
            oro_iroh_string_free(raw);
          }
          return Result<String>{{}, error};
        }
        return Result<String>{detail::takeString(raw), error};
      }

      inline oro_iroh_send_stream_t* get () const {
        return handle;
      }

    private:
      oro_iroh_send_stream_t* handle = nullptr;
  };

  class RecvStream {
    public:
      RecvStream () = default;
      explicit RecvStream (oro_iroh_recv_stream_t* handle) : handle(handle) {}

      RecvStream (RecvStream&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      RecvStream& operator = (RecvStream&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      RecvStream (const RecvStream&) = delete;
      RecvStream& operator = (const RecvStream&) = delete;

      ~RecvStream () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_recv_stream_free(handle);
          handle = nullptr;
        }
      }

      Result<std::vector<uint8_t>> read (
        uint32_t sizeLimit,
        std::optional<uint64_t> timeoutMs = std::nullopt
      ) const {
        oro_iroh_bytes_t rawBytes{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_read(
          handle,
          sizeLimit,
          timeoutMs.value_or(0),
          &rawBytes,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::vector<uint8_t>>{{}, error};
        }
        return Result<std::vector<uint8_t>>{detail::takeBytes(rawBytes), error};
      }

      Result<std::vector<uint8_t>> readToEnd (
        uint32_t sizeLimit,
        std::optional<uint64_t> timeoutMs = std::nullopt
      ) const {
        oro_iroh_bytes_t rawBytes{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_read_to_end(
          handle,
          sizeLimit,
          timeoutMs.value_or(0),
          &rawBytes,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::vector<uint8_t>>{{}, error};
        }
        return Result<std::vector<uint8_t>>{detail::takeBytes(rawBytes), error};
      }

      Result<std::vector<uint8_t>> readExact (
        uint32_t size,
        std::optional<uint64_t> timeoutMs = std::nullopt
      ) const {
        oro_iroh_bytes_t rawBytes{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_read_exact(
          handle,
          size,
          timeoutMs.value_or(0),
          &rawBytes,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::vector<uint8_t>>{{}, error};
        }
        return Result<std::vector<uint8_t>>{detail::takeBytes(rawBytes), error};
      }

      Result<Unit> stop (uint64_t errorCode) const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_stop(handle, errorCode, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<std::optional<uint64_t>> receivedReset () const {
        bool present = false;
        uint64_t code = 0;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_received_reset(handle, &present, &code, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<std::optional<uint64_t>>{{}, error};
        }
        if (!present) {
          return Result<std::optional<uint64_t>>{std::nullopt, error};
        }
        return Result<std::optional<uint64_t>>{std::optional<uint64_t>(code), error};
      }

      Result<String> id () const {
        char* raw = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_recv_stream_id(handle, &raw, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (raw != nullptr) {
            oro_iroh_string_free(raw);
          }
          return Result<String>{{}, error};
        }
        return Result<String>{detail::takeString(raw), error};
      }

      inline oro_iroh_recv_stream_t* get () const {
        return handle;
      }

    private:
      oro_iroh_recv_stream_t* handle = nullptr;
  };

  class Connection {
    public:
      Connection () = default;
      explicit Connection (oro_iroh_connection_t* handle) : handle(handle) {}

      Connection (Connection&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      Connection& operator = (Connection&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      Connection (const Connection&) = delete;
      Connection& operator = (const Connection&) = delete;

      ~Connection () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_connection_free(handle);
          handle = nullptr;
        }
      }

      Result<Unit> close (uint64_t errorCode = 0, const std::vector<uint8_t>& reason = {}) const {
        oro_iroh_error_t rawError{};
        const uint8_t* ptr = reason.empty() ? nullptr : reason.data();
        auto ok = oro_iroh_connection_close(
          handle,
          errorCode,
          ptr,
          reason.size(),
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<String> closedReason () const {
        char* out = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_closed(handle, &out, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (out != nullptr) {
            oro_iroh_string_free(out);
          }
          return Result<String>{{}, error};
        }
        return Result<String>{detail::takeString(out), error};
      }

      Result<Unit> sendDatagram (
        const std::vector<uint8_t>& data,
        std::optional<uint64_t> timeoutMs = std::nullopt
      ) const {
        oro_iroh_error_t rawError{};
        const uint8_t* ptr = data.empty() ? nullptr : data.data();
        auto ok = oro_iroh_connection_send_datagram(
          handle,
          ptr,
          data.size(),
          timeoutMs.value_or(0),
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<std::vector<uint8_t>> readDatagram (std::optional<uint64_t> timeoutMs = std::nullopt) const {
        oro_iroh_bytes_t rawBytes{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_read_datagram(
          handle,
          timeoutMs.value_or(0),
          &rawBytes,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::vector<uint8_t>>{{}, error};
        }
        return Result<std::vector<uint8_t>>{detail::takeBytes(rawBytes), error};
      }

      Result<size_t> maxDatagramSize () const {
        size_t size = 0;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_max_datagram_size(handle, &size, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<size_t>{{}, error};
        }
        return Result<size_t>{size, error};
      }

      Result<ConnectionStats> stats () const {
        oro_iroh_connection_stats_t rawStats{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_stats(handle, &rawStats, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<ConnectionStats>{{}, error};
        }
        ConnectionStats stats;
        stats.maxDatagramSize = rawStats.max_datagram_size;
        stats.rttMicros = rawStats.rtt_micros;
        stats.lostPackets = rawStats.lost_packets;
        stats.sentPackets = rawStats.sent_packets;
        return Result<ConnectionStats>{stats, error};
      }

      Result<std::pair<SendStream, RecvStream>> openBi () const {
        oro_iroh_send_stream_t* send = nullptr;
        oro_iroh_recv_stream_t* recv = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_open_bi(handle, &send, &recv, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (send != nullptr) {
            oro_iroh_send_stream_free(send);
          }
          if (recv != nullptr) {
            oro_iroh_recv_stream_free(recv);
          }
          return Result<std::pair<SendStream, RecvStream>>{{}, error};
        }
        return Result<std::pair<SendStream, RecvStream>>{
          std::make_pair(SendStream(send), RecvStream(recv)),
          error
        };
      }

      Result<SendStream> openUni () const {
        oro_iroh_send_stream_t* send = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_open_uni(handle, &send, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (send != nullptr) {
            oro_iroh_send_stream_free(send);
          }
          return Result<SendStream>{{}, error};
        }
        return Result<SendStream>{SendStream(send), error};
      }

      Result<std::pair<SendStream, RecvStream>> acceptBi () const {
        oro_iroh_send_stream_t* send = nullptr;
        oro_iroh_recv_stream_t* recv = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_accept_bi(handle, &send, &recv, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (send != nullptr) {
            oro_iroh_send_stream_free(send);
          }
          if (recv != nullptr) {
            oro_iroh_recv_stream_free(recv);
          }
          return Result<std::pair<SendStream, RecvStream>>{{}, error};
        }
        return Result<std::pair<SendStream, RecvStream>>{
          std::make_pair(SendStream(send), RecvStream(recv)),
          error
        };
      }

      Result<RecvStream> acceptUni () const {
        oro_iroh_recv_stream_t* recv = nullptr;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_accept_uni(handle, &recv, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (recv != nullptr) {
            oro_iroh_recv_stream_free(recv);
          }
          return Result<RecvStream>{{}, error};
        }
        return Result<RecvStream>{RecvStream(recv), error};
      }

      Result<uint64_t> rtt () const {
        uint64_t value = 0;
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_rtt(handle, &value, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<uint64_t>{{}, error};
        }
        return Result<uint64_t>{value, error};
      }

      Result<Unit> waitClosed () const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_wait_closed(handle, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<std::optional<std::vector<uint8_t>>> alpn () const {
        bool present = false;
        oro_iroh_bytes_t rawBytes{};
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_connection_alpn(handle, &present, &rawBytes, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::optional<std::vector<uint8_t>>>{std::optional<std::vector<uint8_t>>{}, error};
        }
        if (!present) {
          oro_iroh_bytes_free(&rawBytes);
          return Result<std::optional<std::vector<uint8_t>>>{std::nullopt, error};
        }
        return Result<std::optional<std::vector<uint8_t>>>{
          std::optional<std::vector<uint8_t>>(detail::takeBytes(rawBytes)),
          error
        };
      }

      inline oro_iroh_connection_t* get () const {
        return handle;
      }

    private:
      oro_iroh_connection_t* handle = nullptr;
  };

  class Endpoint {
    public:
      Endpoint () = default;
      explicit Endpoint (oro_iroh_endpoint_t* handle) : handle(handle) {}

      Endpoint (Endpoint&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
      }

      Endpoint& operator = (Endpoint&& other) noexcept {
        if (this != &other) {
          reset();
          handle = other.handle;
          other.handle = nullptr;
        }
        return *this;
      }

      Endpoint (const Endpoint&) = delete;
      Endpoint& operator = (const Endpoint&) = delete;

      ~Endpoint () {
        reset();
      }

      inline bool valid () const {
        return handle != nullptr;
      }

      void reset () {
        if (handle != nullptr) {
          oro_iroh_endpoint_free(handle);
          handle = nullptr;
        }
      }

      Result<Unit> setAlpns (const std::vector<std::vector<uint8_t>>& alpns) const {
        std::vector<oro_iroh_bytes_t> raw;
        raw.reserve(alpns.size());
        for (const auto& entry : alpns) {
          oro_iroh_bytes_t bytes{};
          bytes.data = entry.empty() ? nullptr : const_cast<uint8_t*>(entry.data());
          bytes.len = entry.size();
          raw.push_back(bytes);
        }

        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_endpoint_set_alpns(
          handle,
          raw.empty() ? nullptr : raw.data(),
          raw.size(),
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<std::pair<Connection, std::vector<uint8_t>>> accept (
        const std::optional<std::vector<uint8_t>>& expected
      ) const {
        oro_iroh_connection_t* rawConn = nullptr;
        oro_iroh_bytes_t rawAlpn{};
        oro_iroh_error_t rawError{};

        const uint8_t* expectedPtr = nullptr;
        size_t expectedLen = 0;
        if (expected && !expected->empty()) {
          expectedPtr = expected->data();
          expectedLen = expected->size();
        }

        auto ok = oro_iroh_endpoint_accept(
          handle,
          expectedPtr,
          expectedLen,
          &rawConn,
          &rawAlpn,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (rawConn != nullptr) {
            oro_iroh_connection_free(rawConn);
          }
          oro_iroh_bytes_free(&rawAlpn);
          return Result<std::pair<Connection, std::vector<uint8_t>>>{{}, error};
        }

        std::vector<uint8_t> negotiated = detail::takeBytes(rawAlpn);
        return Result<std::pair<Connection, std::vector<uint8_t>>>{
          std::make_pair(Connection(rawConn), std::move(negotiated)),
          error
        };
      }

      Result<std::pair<Connection, std::vector<uint8_t>>> acceptAny () const {
        oro_iroh_connection_t* rawConn = nullptr;
        oro_iroh_bytes_t rawAlpn{};
        oro_iroh_error_t rawError{};

        auto ok = oro_iroh_endpoint_accept_any(
          handle,
          &rawConn,
          &rawAlpn,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (rawConn != nullptr) {
            oro_iroh_connection_free(rawConn);
          }
          oro_iroh_bytes_free(&rawAlpn);
          return Result<std::pair<Connection, std::vector<uint8_t>>>{{}, error};
        }

        std::vector<uint8_t> negotiated = detail::takeBytes(rawAlpn);
        return Result<std::pair<Connection, std::vector<uint8_t>>>{
          std::make_pair(Connection(rawConn), std::move(negotiated)),
          error
        };
      }

      Result<Unit> close () const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_endpoint_close(handle, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<Unit> networkChange () const {
        oro_iroh_error_t rawError{};
        auto ok = oro_iroh_endpoint_network_change(handle, &rawError);
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          return Result<Unit>{{}, error};
        }
        return Result<Unit>{Unit{}, error};
      }

      Result<Connection> connect (const NodeAddr& addr, const std::vector<uint8_t>& alpn) const {
        oro_iroh_connection_t* conn = nullptr;
        oro_iroh_error_t rawError{};
        const uint8_t* ptr = alpn.empty() ? nullptr : alpn.data();
        auto ok = oro_iroh_endpoint_connect(
          handle,
          addr.get(),
          ptr,
          alpn.size(),
          &conn,
          &rawError
        );
        auto error = detail::takeError(rawError);
        if (!ok || !error.ok()) {
          if (conn != nullptr) {
            oro_iroh_connection_free(conn);
          }
          return Result<Connection>{{}, error};
        }
        return Result<Connection>{Connection(conn), error};
      }

      inline oro_iroh_endpoint_t* get () const {
        return handle;
      }

    private:
      oro_iroh_endpoint_t* handle = nullptr;
  };

  inline Result<Net> Node::net () const {
    oro_iroh_net_t* raw = nullptr;
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_node_net(handle, &raw, &rawError);
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      if (raw != nullptr) {
        oro_iroh_net_free(raw);
      }
      return Result<Net>{{}, error};
    }
    return Result<Net>{Net(raw), error};
  }

  inline Result<Endpoint> Node::endpoint () const {
    oro_iroh_endpoint_t* raw = nullptr;
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_node_endpoint(handle, &raw, &rawError);
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      if (raw != nullptr) {
        oro_iroh_endpoint_free(raw);
      }
      return Result<Endpoint>{{}, error};
    }
    return Result<Endpoint>{Endpoint(raw), error};
  }

  inline Result<NodeAddr> Net::nodeAddr () const {
    oro_iroh_node_addr_t* raw = nullptr;
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_net_node_addr(handle, &raw, &rawError);
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      if (raw != nullptr) {
        oro_iroh_node_addr_free(raw);
      }
      return Result<NodeAddr>{{}, error};
    }
    return Result<NodeAddr>{NodeAddr(raw), error};
  }

  inline Result<Unit> Net::addNodeAddr (const NodeAddr& addr) const {
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_net_add_node_addr(handle, addr.get(), &rawError);
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      return Result<Unit>{{}, error};
    }
    return Result<Unit>{Unit{}, error};
  }

  inline Error setLogLevel (LogLevel level) {
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_set_log_level(static_cast<int>(level), &rawError);
    auto error = detail::takeError(rawError);
    if (!ok && error.ok()) {
      error.code = -1;
      error.message = "setLogLevel failed";
    }
    return error;
  }

  inline Result<std::vector<uint8_t>> pathToKey (
    const String& path,
    const std::optional<String>& prefix,
    const std::optional<String>& root
  ) {
    oro_iroh_bytes_t rawBytes{};
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_path_to_key(
      path.c_str(),
      prefix ? prefix->c_str() : nullptr,
      root ? root->c_str() : nullptr,
      &rawBytes,
      &rawError
    );
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      oro_iroh_bytes_free(&rawBytes);
      return Result<std::vector<uint8_t>>{{}, error};
    }
    return Result<std::vector<uint8_t>>{detail::takeBytes(rawBytes), error};
  }

  inline Result<String> keyToPath (
    const std::vector<uint8_t>& key,
    const std::optional<String>& prefix,
    const std::optional<String>& root
  ) {
    char* raw = nullptr;
    oro_iroh_error_t rawError{};
    auto ok = oro_iroh_key_to_path(
      key.data(),
      key.size(),
      prefix ? prefix->c_str() : nullptr,
      root ? root->c_str() : nullptr,
      &raw,
      &rawError
    );
    auto error = detail::takeError(rawError);
    if (!ok || !error.ok()) {
      if (raw != nullptr) {
        oro_iroh_string_free(raw);
      }
      return Result<String>{{}, error};
    }
    return Result<String>{detail::takeString(raw), error};
  }
} // namespace oro::runtime::iroh::uniffi

#endif // ORO_RUNTIME_IROH_UNIFFI_H
