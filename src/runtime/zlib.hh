#ifndef ORO_RUNTIME_ZLIB_H
#define ORO_RUNTIME_ZLIB_H

#include "platform.hh"
#include "deps.hh"

namespace oro::runtime::compression {
  enum class Mode {
    Deflate,
    Inflate
  };

  enum class Format {
    Zlib,
    Gzip,
    Raw
  };

  struct Options {
    Mode mode = Mode::Deflate;
    Format format = Format::Zlib;
    int level = 0;
    int windowBits = 15;
    int memLevel = 8;
    int strategy = 0;
  };

#if ORO_RUNTIME_HAS_ZLIB

  class Stream {
    public:
      explicit Stream (const Options& options);
      ~Stream ();

      bool ok () const;
      bool finished () const;

      bool write (
        const unsigned char* input,
        size_t inputSize,
        Vector<uint8_t>& output,
        int flush
      );

      bool finish (Vector<uint8_t>& output);

      const Options& options () const;

    private:
      Options opts;
      ::z_stream stream;
      bool initialized = false;
      bool done = false;

      bool init ();
      int effectiveWindowBits () const;
  };

  bool deflate (
    const unsigned char* input,
    size_t inputSize,
    Vector<uint8_t>& output,
    const Options& options
  );

  bool inflate (
    const unsigned char* input,
    size_t inputSize,
    Vector<uint8_t>& output,
    const Options& options
  );

#else

  class Stream {
    public:
      explicit Stream (const Options& options) : opts(options) {}
      ~Stream () = default;

      bool ok () const {
        return false;
      }

      bool finished () const {
        return true;
      }

      bool write (
        const unsigned char*,
        size_t,
        Vector<uint8_t>&,
        int
      ) {
        return false;
      }

      bool finish (Vector<uint8_t>&) {
        return false;
      }

      const Options& options () const {
        return this->opts;
      }

    private:
      Options opts;
  };

  inline bool deflate (
    const unsigned char*,
    size_t,
    Vector<uint8_t>&,
    const Options&
  ) {
    return false;
  }

  inline bool inflate (
    const unsigned char*,
    size_t,
    Vector<uint8_t>&,
    const Options&
  ) {
    return false;
  }

#endif
}

#endif

