#include "zlib.hh"
#include "../../crypto.hh"
#include "../../http.hh"
#include "../../deps.hh"

#include <cstring>

namespace oro::runtime::core::services {
  using oro::runtime::crypto::rand64;
  using oro::runtime::compression::Mode;
  using oro::runtime::compression::Format;
  using oro::runtime::compression::Options;

  bool Zlib::start () {
    return this->enabled.load();
  }

  bool Zlib::stop () {
    Lock lock(this->mutex);
    this->streams.clear();
    return true;
  }

  bool Zlib::available () const {
  #if ORO_RUNTIME_HAS_ZLIB
    return true;
  #else
    return false;
  #endif
  }

  SharedPointer<Zlib::StreamDescriptor> Zlib::getStream (ID id) const {
    Lock lock(this->mutex);
    auto it = this->streams.find(id);
    if (it != this->streams.end()) {
      return it->second;
    }
    return nullptr;
  }

  void Zlib::removeStream (ID id) {
    Lock lock(this->mutex);
    this->streams.erase(id);
  }

  JSON::Object::Entries Zlib::makeError (
    const String& source,
    const String& code,
    const String& message
  ) const {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"code", code},
        {"message", message}
      }}
    };
  }

  void Zlib::buildBinaryResponse (
    const ipc::Message::Seq& seq,
    Vector<uint8_t>&& buffer,
    const Callback callback
  ) {
    const auto length = buffer.size();

    this->loop.dispatch([=, this, buffer = std::move(buffer)]() mutable {
      http::Headers headers;
      headers.set("content-type", "application/octet-stream");
      headers.set("content-length", static_cast<uint64_t>(length));
      const auto allocSize = length > 0
        ? static_cast<size_t>(length)
        : static_cast<size_t>(1);
      auto shared = std::make_shared<unsigned char[]>(allocSize);
      if (length > 0) {
        std::memcpy(shared.get(), buffer.data(), length);
      }

      QueuedResponse queuedResponse;
      queuedResponse.id = rand64();
      queuedResponse.body = shared;
      queuedResponse.length = length;
      queuedResponse.headers = headers.str();

      JSON::Object json;
      callback(seq, json, queuedResponse);
    });
  }

#if !ORO_RUNTIME_HAS_ZLIB

  void Zlib::deflate (
    const ipc::Message::Seq& seq,
    SharedPointer<unsigned char[]>,
    size_t,
    const CompressOptions&,
    const Callback callback
  ) {
    auto json = this->makeError(
      "zlib.deflate",
      "ERR_ZLIB_UNAVAILABLE",
      "zlib support is not available in this build"
    );
    callback(seq, json, QueuedResponse{});
  }

  void Zlib::inflate (
    const ipc::Message::Seq& seq,
    SharedPointer<unsigned char[]>,
    size_t,
    const CompressOptions&,
    const Callback callback
  ) {
    auto json = this->makeError(
      "zlib.inflate",
      "ERR_ZLIB_UNAVAILABLE",
      "zlib support is not available in this build"
    );
    callback(seq, json, QueuedResponse{});
  }

  void Zlib::openStream (
    const ipc::Message::Seq& seq,
    const StreamOptions&,
    const Callback callback
  ) {
    auto json = this->makeError(
      "zlib.stream.open",
      "ERR_ZLIB_UNAVAILABLE",
      "zlib support is not available in this build"
    );
    callback(seq, json, QueuedResponse{});
  }

  void Zlib::writeStream (
    const ipc::Message::Seq& seq,
    ID,
    SharedPointer<unsigned char[]>,
    size_t,
    bool,
    const Callback callback
  ) {
    auto json = this->makeError(
      "zlib.stream.write",
      "ERR_ZLIB_UNAVAILABLE",
      "zlib support is not available in this build"
    );
    callback(seq, json, QueuedResponse{});
  }

#else

  static Options toCompressionOptions (
    const Zlib::CompressOptions& options,
    bool inflate
  ) {
    Options result;
    result.mode = inflate ? Mode::Inflate : Mode::Deflate;

    if (options.gzip) {
      result.format = Format::Gzip;
    } else if (options.raw) {
      result.format = Format::Raw;
    } else {
      result.format = Format::Zlib;
    }

    result.level = options.level;
    if (result.level == 0) {
      result.level = Z_DEFAULT_COMPRESSION;
    }

    result.windowBits = 15;
    result.memLevel = 8;
    result.strategy = Z_DEFAULT_STRATEGY;
    return result;
  }

  void Zlib::deflate (
    const ipc::Message::Seq& seq,
    SharedPointer<unsigned char[]> input,
    size_t size,
    const CompressOptions& options,
    const Callback callback
  ) {
    if (!this->available()) {
      auto json = this->makeError(
        "zlib.deflate",
        "ERR_ZLIB_UNAVAILABLE",
        "zlib support is not available in this build"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> output;
      const auto opts = toCompressionOptions(options, false);

      const unsigned char* bytes = input ? input.get() : nullptr;
      const auto ok = compression::deflate(bytes, size, output, opts);
      if (!ok) {
        auto json = this->makeError(
          "zlib.deflate",
          "ERR_ZLIB_DEFLATE",
          "Failed to deflate input buffer"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      this->buildBinaryResponse(seq, std::move(output), callback);
    });
  }

  void Zlib::inflate (
    const ipc::Message::Seq& seq,
    SharedPointer<unsigned char[]> input,
    size_t size,
    const CompressOptions& options,
    const Callback callback
  ) {
    if (!this->available()) {
      auto json = this->makeError(
        "zlib.inflate",
        "ERR_ZLIB_UNAVAILABLE",
        "zlib support is not available in this build"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> output;
      const auto opts = toCompressionOptions(options, true);

      const unsigned char* bytes = input ? input.get() : nullptr;
      const auto ok = compression::inflate(bytes, size, output, opts);
      if (!ok) {
        auto json = this->makeError(
          "zlib.inflate",
          "ERR_ZLIB_INFLATE",
          "Failed to inflate input buffer"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      this->buildBinaryResponse(seq, std::move(output), callback);
    });
  }

  void Zlib::openStream (
    const ipc::Message::Seq& seq,
    const StreamOptions& options,
    const Callback callback
  ) {
    if (!this->available()) {
      auto json = this->makeError(
        "zlib.stream.open",
        "ERR_ZLIB_UNAVAILABLE",
        "zlib support is not available in this build"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      const auto opts = toCompressionOptions(options, options.inflate);
      auto descriptor = std::make_shared<StreamDescriptor>();
      descriptor->id = rand64();
      descriptor->options = options;
      descriptor->stream = std::make_unique<compression::Stream>(opts);

      if (!descriptor->stream->ok()) {
        this->loop.dispatch([=, this]() {
          auto json = this->makeError(
            "zlib.stream.open",
            "ERR_ZLIB_STREAM_INIT",
            "Failed to initialize zlib stream"
          );
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      {
        Lock lock(this->mutex);
        this->streams.insert_or_assign(descriptor->id, descriptor);
      }

      this->loop.dispatch([=, this]() {
        JSON::Object::Entries data {
          {"id", std::to_string(descriptor->id)}
        };

        JSON::Object::Entries json {
          {"source", "zlib.stream.open"},
          {"data", data}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void Zlib::writeStream (
    const ipc::Message::Seq& seq,
    ID id,
    SharedPointer<unsigned char[]> input,
    size_t size,
    bool finish,
    const Callback callback
  ) {
    auto descriptor = this->getStream(id);
    if (descriptor == nullptr || !descriptor->stream) {
      auto json = this->makeError(
        "zlib.stream.write",
        "ERR_ZLIB_STREAM_NOT_OPEN",
        "No zlib stream descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (descriptor->finished) {
      auto json = this->makeError(
        "zlib.stream.write",
        "ERR_ZLIB_STREAM_ENDED",
        "Zlib stream has already finished"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> output;
      const unsigned char* bytes = input ? input.get() : nullptr;

      const int flush = finish ? Z_FINISH : Z_SYNC_FLUSH;
      const auto ok = descriptor->stream->write(bytes, size, output, flush);
      if (!ok) {
        this->loop.dispatch([=, this]() {
          auto json = this->makeError(
            "zlib.stream.write",
            "ERR_ZLIB_STREAM_WRITE",
            "Failed to process zlib stream chunk"
          );
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (descriptor->stream->finished()) {
        descriptor->finished = true;
        this->removeStream(descriptor->id);
      }

      this->buildBinaryResponse(seq, std::move(output), callback);
    });
  }

#endif
}
