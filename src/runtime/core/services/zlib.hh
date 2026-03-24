#ifndef ORO_RUNTIME_CORE_SERVICES_ZLIB_H
#define ORO_RUNTIME_CORE_SERVICES_ZLIB_H

#include "../../core.hh"
#include "../../ipc.hh"
#include "../../zlib.hh"

namespace oro::runtime::core::services {
  class Zlib : public core::Service {
    public:
      using ID = uint64_t;

      struct CompressOptions {
        int level = 0;
        bool raw = false;
        bool gzip = false;
      };

      struct StreamOptions : public CompressOptions {
        bool inflate = false;
      };

      struct StreamDescriptor {
        ID id = 0;
        StreamOptions options;
        std::unique_ptr<compression::Stream> stream;
        Atomic<bool> finished = false;
      };

      Zlib (const Options& options)
        : core::Service(options) {}

      bool start () override;
      bool stop () override;

      bool available () const;

      void deflate (
        const ipc::Message::Seq& seq,
        SharedPointer<unsigned char[]> input,
        size_t size,
        const CompressOptions& options,
        const Callback callback
      );

      void inflate (
        const ipc::Message::Seq& seq,
        SharedPointer<unsigned char[]> input,
        size_t size,
        const CompressOptions& options,
        const Callback callback
      );

      void openStream (
        const ipc::Message::Seq& seq,
        const StreamOptions& options,
        const Callback callback
      );

      void writeStream (
        const ipc::Message::Seq& seq,
        ID id,
        SharedPointer<unsigned char[]> input,
        size_t size,
        bool finish,
        const Callback callback
      );

    private:
      mutable Mutex mutex;
      Map<ID, SharedPointer<StreamDescriptor>> streams;

      SharedPointer<StreamDescriptor> getStream (ID id) const;
      void removeStream (ID id);

      JSON::Object::Entries makeError (
        const String& source,
        const String& code,
        const String& message
      ) const;

      void buildBinaryResponse (
        const ipc::Message::Seq& seq,
        Vector<uint8_t>&& buffer,
        const Callback callback
      );
  };
}

#endif
