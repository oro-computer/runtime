#include "zlib.hh"

#if ORO_RUNTIME_HAS_ZLIB

#include <cstring>

namespace oro::runtime::compression {
  Stream::Stream (const Options& options)
    : opts(options) {
    std::memset(&this->stream, 0, sizeof(this->stream));
    this->initialized = this->init();
  }

  Stream::~Stream () {
    if (!this->initialized) {
      return;
    }

    if (this->opts.mode == Mode::Deflate) {
      ::deflateEnd(&this->stream);
    } else {
      ::inflateEnd(&this->stream);
    }
  }

  bool Stream::ok () const {
    return this->initialized;
  }

  bool Stream::finished () const {
    return this->done;
  }

  const Options& Stream::options () const {
    return this->opts;
  }

  int Stream::effectiveWindowBits () const {
    int bits = this->opts.windowBits;
    if (bits <= 0) {
      bits = 15;
    }

    switch (this->opts.format) {
      case Format::Raw:
        return -bits;
      case Format::Gzip:
        return bits + 16;
      case Format::Zlib:
      default:
        return bits;
    }
  }

  bool Stream::init () {
    this->stream.zalloc = Z_NULL;
    this->stream.zfree = Z_NULL;
    this->stream.opaque = Z_NULL;

    int level = this->opts.level;
    if (level < Z_DEFAULT_COMPRESSION) {
      level = Z_DEFAULT_COMPRESSION;
    }
    if (level > Z_BEST_COMPRESSION) {
      level = Z_BEST_COMPRESSION;
    }

    const int windowBits = this->effectiveWindowBits();
    const int memLevel = this->opts.memLevel > 0 ? this->opts.memLevel : 8;
    const int strategy = this->opts.strategy > 0 ? this->opts.strategy : Z_DEFAULT_STRATEGY;

    int rc = Z_OK;
    if (this->opts.mode == Mode::Deflate) {
      rc = ::deflateInit2(
        &this->stream,
        level,
        Z_DEFLATED,
        windowBits,
        memLevel,
        strategy
      );
    } else {
      rc = ::inflateInit2(
        &this->stream,
        windowBits
      );
    }

    return rc == Z_OK;
  }

  bool Stream::write (
    const unsigned char* input,
    size_t inputSize,
    Vector<uint8_t>& output,
    int flush
  ) {
    if (!this->initialized || this->done) {
      return false;
    }

    this->stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    this->stream.avail_in = static_cast<uInt>(inputSize);

    constexpr size_t kChunkSize = 64 * 1024;
    unsigned char buffer[kChunkSize];

    int rc = Z_OK;

    do {
      this->stream.next_out = buffer;
      this->stream.avail_out = static_cast<uInt>(kChunkSize);

      if (this->opts.mode == Mode::Deflate) {
        rc = ::deflate(&this->stream, flush);
      } else {
        rc = ::inflate(&this->stream, flush);
      }

      if (rc == Z_STREAM_ERROR || rc == Z_DATA_ERROR || rc == Z_MEM_ERROR) {
        return false;
      }

      const size_t produced = kChunkSize - this->stream.avail_out;
      if (produced > 0) {
        output.insert(output.end(), buffer, buffer + produced);
      }

      if (rc == Z_STREAM_END) {
        this->done = true;
        break;
      }
    } while (this->stream.avail_out == 0);

    return true;
  }

  bool Stream::finish (Vector<uint8_t>& output) {
    if (!this->initialized || this->done) {
      return false;
    }

    return this->write(nullptr, 0, output, Z_FINISH);
  }

  bool deflate (
    const unsigned char* input,
    size_t inputSize,
    Vector<uint8_t>& output,
    const Options& options
  ) {
    Options opts = options;
    opts.mode = Mode::Deflate;

    Stream stream(opts);
    if (!stream.ok()) {
      return false;
    }

    if (!stream.write(input, inputSize, output, Z_FINISH)) {
      return false;
    }

    return stream.finished();
  }

  bool inflate (
    const unsigned char* input,
    size_t inputSize,
    Vector<uint8_t>& output,
    const Options& options
  ) {
    Options opts = options;
    opts.mode = Mode::Inflate;

    Stream stream(opts);
    if (!stream.ok()) {
      return false;
    }

    if (!stream.write(input, inputSize, output, Z_FINISH)) {
      return false;
    }

    return stream.finished();
  }
}

#endif
