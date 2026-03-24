#include "ipfs.hh"

#include "../../debug.hh"
#include "../../string.hh"

#if ORO_RUNTIME_HAVE_LIBIPFS
#include <libipfs.h>
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace oro::runtime::core::services {
  namespace {
    constexpr const char* kSourceStart = "ipfs.start";
    constexpr const char* kSourceStop = "ipfs.stop";
    constexpr const char* kSourceStatus = "ipfs.status";
    constexpr const char* kSourceAdd = "ipfs.add";
    constexpr const char* kSourceGet = "ipfs.get";
    constexpr const char* kSourcePin = "ipfs.pin";
    constexpr const char* kSourceUnpin = "ipfs.unpin";
    constexpr const char* kSourceGC = "ipfs.gc";
    constexpr const char* kSourcePeerId = "ipfs.peerId";
    constexpr const char* kSourceAddPeer = "ipfs.addPeer";
    constexpr const char* kSourceRemovePeer = "ipfs.removePeer";

    struct NativeResult {
      bool ok = false;
      nlohmann::json data = nullptr;
      String errorMessage;
      String errorCode;
    };

    static inline String toLowerCopy (String value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return value;
    }

    static JSON::Any toRuntimeJSON (const nlohmann::json& value) {
      using value_t = nlohmann::json::value_t;

      switch (value.type()) {
        case value_t::null:
        case value_t::discarded:
          return JSON::Any(nullptr);
        case value_t::object: {
          JSON::Object::Entries entries;
          for (const auto& item : value.items()) {
            entries.emplace(item.key(), toRuntimeJSON(item.value()));
          }
          return JSON::Object(entries);
        }
        case value_t::array: {
          JSON::Array::Entries entries;
          entries.reserve(value.size());
          for (const auto& element : value) {
            entries.emplace_back(toRuntimeJSON(element));
          }
          return JSON::Array(entries);
        }
        case value_t::string:
          return JSON::Any(value.get<String>());
        case value_t::boolean:
          return JSON::Any(value.get<bool>());
        case value_t::number_integer:
          return JSON::Any(static_cast<int64_t>(value.get<int64_t>()));
        case value_t::number_unsigned:
          return JSON::Any(static_cast<uint64_t>(value.get<uint64_t>()));
        case value_t::number_float:
          return JSON::Any(value.get<double>());
        case value_t::binary:
        default:
          return JSON::Any(nullptr);
      }
    }

    static JSON::Object::Entries makeDataPayload (const String& source, const JSON::Any& data) {
      JSON::Object::Entries payload;
      payload.emplace("source", source);
      payload.emplace("data", data);
      return payload;
    }

    static JSON::Object::Entries makeErrorPayload (
      const String& source,
      const String& message,
      const String& type = "NativeError",
      const String& code = ""
    ) {
      JSON::Object::Entries err;
      err.emplace("type", type);
      err.emplace("message", message);
      if (!code.empty()) {
        err.emplace("code", code);
      }

      JSON::Object::Entries payload;
      payload.emplace("source", source);
      payload.emplace("err", err);
      return payload;
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    static NativeResult parseNativeResult (char* raw, const String& fallbackMessage) {
      NativeResult result;

      if (raw == nullptr) {
        result.errorMessage = fallbackMessage.empty() ? String("libipfs returned empty result") : fallbackMessage;
        return result;
      }

      String buffer(raw);
      std::free(raw);

      if (buffer.empty()) {
        result.errorMessage = fallbackMessage.empty() ? String("libipfs returned empty result") : fallbackMessage;
        return result;
      }

      try {
        auto json = nlohmann::json::parse(buffer);
        const auto statusIt = json.find("Status");
        String status = statusIt != json.end() && statusIt->is_string()
          ? statusIt->get<String>()
          : String();

        result.ok = toLowerCopy(status) == "success";

        if (result.ok) {
          const auto dataIt = json.find("Data");
          if (dataIt != json.end()) {
            result.data = *dataIt;
          } else {
            result.data = nullptr;
          }
        } else {
          const auto dataIt = json.find("Data");
          if (dataIt != json.end()) {
            if (dataIt->is_object()) {
              if (auto errIt = dataIt->find("error"); errIt != dataIt->end() && errIt->is_string()) {
                result.errorMessage = errIt->get<String>();
              } else if (auto msgIt = dataIt->find("message"); msgIt != dataIt->end() && msgIt->is_string()) {
                result.errorMessage = msgIt->get<String>();
              }
              if (auto codeIt = dataIt->find("code"); codeIt != dataIt->end() && codeIt->is_string()) {
                result.errorCode = codeIt->get<String>();
              }
            } else if (dataIt->is_string()) {
              result.errorMessage = dataIt->get<String>();
            }
          }

          if (result.errorMessage.empty()) {
            result.errorMessage = fallbackMessage.empty()
              ? String("libipfs operation failed")
              : fallbackMessage;
          }
        }
      } catch (const nlohmann::json::exception& err) {
        result.ok = false;
        result.errorMessage = fallbackMessage.empty()
          ? String("failed to parse libipfs response: ") + err.what()
          : fallbackMessage + ": " + err.what();
      }

      return result;
    }
#endif
  }

  IPFS::IPFS (const Options& options)
    : core::Service(options) {
#if ORO_RUNTIME_HAVE_LIBIPFS
    this->enabled = true;
    this->hasLibrary = true;
#else
    this->enabled = false;
    this->hasLibrary = false;
#endif
  }

  IPFS::~IPFS () {
    this->stop();
  }

  bool IPFS::start () {
    return this->hasLibrary;
  }

  bool IPFS::stop () {
    bool shouldStop = false;
    {
      Lock lock(this->mutex);
      shouldStop = this->nodeStarted;
      this->nodeStarted = false;
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    if (shouldStop) {
      auto result = parseNativeResult(Stop(), "failed to stop libipfs node");
      if (!result.ok) {
        debug("ipfs.stop(): %s", result.errorMessage.c_str());
      }
    }
#endif

    return true;
  }

  void IPFS::dispatchUnavailable (
    const ipc::Message::Seq& seq,
    const String& source,
    const Callback callback
  ) const {
    callback(seq, makeErrorPayload(source, "libipfs native library unavailable", "NotSupportedError"), QueuedResponse{});
  }

  void IPFS::dispatchError (
    const ipc::Message::Seq& seq,
    const String& source,
    const String& message,
    const Callback callback
  ) const {
    callback(seq, makeErrorPayload(source, message), QueuedResponse{});
  }

  void IPFS::dispatchError (
    const ipc::Message::Seq& seq,
    const String& source,
    const String& message,
    const String& code,
    const Callback callback
  ) const {
    callback(seq, makeErrorPayload(source, message, "NativeError", code), QueuedResponse{});
  }

  void IPFS::dispatchData (
    const ipc::Message::Seq& seq,
    const String& source,
    const JSON::Any& data,
    const Callback callback
  ) const {
    callback(seq, makeDataPayload(source, data), QueuedResponse{});
  }

  bool IPFS::ensureLibraryAvailable (
    const ipc::Message::Seq& seq,
    const String& source,
    const Callback callback
  ) const {
    if (!this->hasLibrary) {
      this->dispatchUnavailable(seq, source, callback);
      return false;
    }
    return true;
  }

  void IPFS::nodeStart (
    const ipc::Message::Seq& seq,
    const StartOptions& options,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceStart, callback)) {
      return;
    }

    StartOptions startOptions = options;
    {
      Lock lock(this->mutex);
      if (startOptions.repoPath.empty()) {
        startOptions.repoPath = this->repoPath;
      }
      if (startOptions.port <= 0 && this->port > 0) {
        startOptions.port = this->port;
      }
    }

    if (startOptions.repoPath.empty()) {
      this->dispatchError(seq, kSourceStart, "A repository path is required", callback);
      return;
    }

    if (startOptions.port <= 0) {
      this->dispatchError(seq, kSourceStart, "A positive port value is required", callback);
      return;
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() mutable {
      auto result = parseNativeResult(
        Start(const_cast<char*>(startOptions.repoPath.c_str()), static_cast<GoInt>(startOptions.port)),
        "failed to start libipfs node"
      );

      if (result.ok) {
        Lock lock(this->mutex);
        this->nodeStarted = true;
        this->repoPath = startOptions.repoPath;
        this->port = startOptions.port;

        if (result.data.is_object()) {
          if (auto peerIt = result.data.find("peerId"); peerIt != result.data.end() && peerIt->is_string()) {
            this->lastPeerId = peerIt->get<String>();
          }
        }
      }

      this->loop.dispatch([=, this, result = std::move(result)]() mutable {
        if (result.ok) {
          JSON::Object::Entries dataEntries;
          if (result.data.is_object()) {
            for (const auto& entry : result.data.items()) {
              dataEntries.emplace(entry.key(), toRuntimeJSON(entry.value()));
            }
          }
          dataEntries.emplace("repoPath", this->repoPath);
          dataEntries.emplace("port", static_cast<int64_t>(this->port));
          if (!this->lastPeerId.empty()) {
            dataEntries.emplace("peerId", this->lastPeerId);
          }
          this->dispatchData(seq, kSourceStart, JSON::Object(dataEntries), callback);
        } else {
          this->dispatchError(seq, kSourceStart, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::nodeStop (
    const ipc::Message::Seq& seq,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceStop, callback)) {
      return;
    }

    bool alreadyStopped = false;
    {
      Lock lock(this->mutex);
      alreadyStopped = !this->nodeStarted;
      this->nodeStarted = false;
    }

    if (alreadyStopped) {
      JSON::Object::Entries data;
      data.emplace("stopped", true);
      this->dispatchData(seq, kSourceStop, JSON::Object(data), callback);
      return;
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(Stop(), "failed to stop libipfs node");
      if (result.ok) {
        Lock lock(this->mutex);
        this->nodeStarted = false;
      }

      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("stopped", true);
          this->dispatchData(seq, kSourceStop, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceStop, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::nodeStatus (
    const ipc::Message::Seq& seq,
    const Callback callback
  ) {
    JSON::Object::Entries data;
    data.emplace("available", this->hasLibrary);

    {
      Lock lock(this->mutex);
      data.emplace("started", this->nodeStarted);
      if (!this->repoPath.empty()) {
        data.emplace("repoPath", this->repoPath);
      }
      if (this->port > 0) {
        data.emplace("port", static_cast<int64_t>(this->port));
      }
      if (!this->lastPeerId.empty()) {
        data.emplace("peerId", this->lastPeerId);
      }
    }

    this->dispatchData(seq, kSourceStatus, JSON::Object(data), callback);
  }

  void IPFS::addFile (
    const ipc::Message::Seq& seq,
    const String& path,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceAdd, callback)) {
      return;
    }

    if (path.empty()) {
      this->dispatchError(seq, kSourceAdd, "A file path is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceAdd, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(Add(const_cast<char*>(path.c_str())), "failed to add path to libipfs");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Any payload = result.data.is_null()
            ? JSON::Any(JSON::Object::Entries{})
            : toRuntimeJSON(result.data);
          this->dispatchData(seq, kSourceAdd, payload, callback);
        } else {
          this->dispatchError(seq, kSourceAdd, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::fetch (
    const ipc::Message::Seq& seq,
    const String& cid,
    const String& destination,
    bool pinResult,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceGet, callback)) {
      return;
    }

    if (cid.empty()) {
      this->dispatchError(seq, kSourceGet, "A CID or path is required", callback);
      return;
    }

    if (destination.empty()) {
      this->dispatchError(seq, kSourceGet, "A destination path is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceGet, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(
        Get(
          const_cast<char*>(cid.c_str()),
          const_cast<char*>(destination.c_str()),
          pinResult ? static_cast<GoUint8>(1) : static_cast<GoUint8>(0)
        ),
        "failed to fetch from libipfs"
      );

      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("cid", cid);
          data.emplace("path", destination);
          data.emplace("pinned", pinResult);
          this->dispatchData(seq, kSourceGet, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceGet, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::pin (
    const ipc::Message::Seq& seq,
    const String& cid,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourcePin, callback)) {
      return;
    }

    if (cid.empty()) {
      this->dispatchError(seq, kSourcePin, "A CID or path is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourcePin, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(Pin(const_cast<char*>(cid.c_str())), "failed to pin content");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("cid", cid);
          data.emplace("pinned", true);
          this->dispatchData(seq, kSourcePin, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourcePin, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::unpin (
    const ipc::Message::Seq& seq,
    const String& cid,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceUnpin, callback)) {
      return;
    }

    if (cid.empty()) {
      this->dispatchError(seq, kSourceUnpin, "A CID or path is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceUnpin, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(Unpin(const_cast<char*>(cid.c_str())), "failed to unpin content");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("cid", cid);
          data.emplace("pinned", false);
          this->dispatchData(seq, kSourceUnpin, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceUnpin, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::garbageCollect (
    const ipc::Message::Seq& seq,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceGC, callback)) {
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceGC, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(GarbageCollect(), "failed to run garbage collection");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("collected", true);
          this->dispatchData(seq, kSourceGC, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceGC, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::peerId (
    const ipc::Message::Seq& seq,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourcePeerId, callback)) {
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourcePeerId, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(PeerID(), "failed to retrieve peer id");

      if (result.ok && result.data.is_object()) {
        if (auto peerIt = result.data.find("peerId"); peerIt != result.data.end() && peerIt->is_string()) {
          Lock lock(this->mutex);
          this->lastPeerId = peerIt->get<String>();
        }
      }

      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          this->dispatchData(seq, kSourcePeerId, toRuntimeJSON(result.data), callback);
        } else {
          this->dispatchError(seq, kSourcePeerId, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::addPeer (
    const ipc::Message::Seq& seq,
    const String& address,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceAddPeer, callback)) {
      return;
    }

    if (address.empty()) {
      this->dispatchError(seq, kSourceAddPeer, "A multiaddress is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceAddPeer, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(AddPeer(const_cast<char*>(address.c_str())), "failed to add peer");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("peer", address);
          data.emplace("added", true);
          this->dispatchData(seq, kSourceAddPeer, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceAddPeer, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }

  void IPFS::removePeer (
    const ipc::Message::Seq& seq,
    const String& address,
    const Callback callback
  ) {
    if (!this->ensureLibraryAvailable(seq, kSourceRemovePeer, callback)) {
      return;
    }

    if (address.empty()) {
      this->dispatchError(seq, kSourceRemovePeer, "A multiaddress is required", callback);
      return;
    }

    {
      Lock lock(this->mutex);
      if (!this->nodeStarted) {
        this->dispatchError(seq, kSourceRemovePeer, "IPFS node is not running", callback);
        return;
      }
    }

#if ORO_RUNTIME_HAVE_LIBIPFS
    this->queue.push([=, this]() {
      auto result = parseNativeResult(RemovePeer(const_cast<char*>(address.c_str())), "failed to remove peer");
      this->loop.dispatch([=, this, result = std::move(result)]() {
        if (result.ok) {
          JSON::Object::Entries data;
          data.emplace("peer", address);
          data.emplace("removed", true);
          this->dispatchData(seq, kSourceRemovePeer, JSON::Object(data), callback);
        } else {
          this->dispatchError(seq, kSourceRemovePeer, result.errorMessage, result.errorCode, callback);
        }
      });
    });
#endif
  }
}
