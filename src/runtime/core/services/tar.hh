#ifndef ORO_RUNTIME_CORE_SERVICES_TAR_H
#define ORO_RUNTIME_CORE_SERVICES_TAR_H

#include "../../core.hh"
#include "../../filesystem.hh"
#include "../../tar.hh"
#include "../../ipc.hh"

namespace oro::runtime::core::services {
  class Tar : public core::Service {
    public:
      using ID = uint64_t;

      struct Descriptor {
        ID id = 0;
        String path;
        bool writable = false;
        bool mmapEnabled = false;
        bool entryOpen = false;
        uint64_t entryRemaining = 0;
        bool finalized = false;

        std::unique_ptr<tar::ArchiveReader> reader;
        std::unique_ptr<tar::ArchiveWriter> writer;
        SharedPointer<unsigned char[]> buffer; // in-memory archive backing
        bool inMemory = false;
        uint64_t bufferSize = 0;

        Descriptor () = default;
        Descriptor (ID id, const String& path, bool writable, bool mmapEnabled)
          : id(id),
            path(path),
            writable(writable),
            mmapEnabled(mmapEnabled) {}
      };

      Tar (const Options& options)
        : core::Service(options) {}

      bool stop () override;

      SharedPointer<Descriptor> getDescriptor (ID id) const;
      void removeDescriptor (ID id);

      void open (
        const ipc::Message::Seq& seq,
        const String& path,
        bool writable,
        bool mmapEnabled,
        uint32_t uid,
        uint32_t gid,
        const String& uname,
        const String& gname,
        uint64_t mtime,
        bool hasUid,
        bool hasGid,
        bool hasUname,
        bool hasGname,
        bool hasMtime,
        const Callback callback
      );

      void openFromBuffer (
        const ipc::Message::Seq& seq,
        SharedPointer<unsigned char[]> buffer,
        size_t size,
        const Callback callback
      );

      void createInMemory (
        const ipc::Message::Seq& seq,
        uint32_t uid,
        uint32_t gid,
        const String& uname,
        const String& gname,
        uint64_t mtime,
        bool hasUid,
        bool hasGid,
        bool hasUname,
        bool hasGname,
        bool hasMtime,
        const Callback callback
      );

      void close (
        const ipc::Message::Seq& seq,
        ID id,
        const Callback callback
      );

      void listEntries (
        const ipc::Message::Seq& seq,
        ID id,
        const Callback callback
      );

      void statEntry (
        const ipc::Message::Seq& seq,
        ID id,
        const String& path,
        const Callback callback
      );

      void readEntry (
        const ipc::Message::Seq& seq,
        ID id,
        const String& path,
        uint64_t offset,
        uint32_t size,
        const Callback callback
      );

      void beginWriteEntry (
        const ipc::Message::Seq& seq,
        ID id,
        const String& path,
        const String& linkpath,
        uint32_t devmajor,
        uint32_t devminor,
        uint64_t size,
        uint32_t mode,
        uint64_t mtime,
        bool hasMtime,
        char type,
        uint32_t uid,
        uint32_t gid,
        const String& uname,
        const String& gname,
        bool hasUid,
        bool hasGid,
        bool hasUname,
        bool hasGname,
        uint64_t sparseSize,
        const String& sparse,
        const Callback callback
      );

      void writeEntryData (
        const ipc::Message::Seq& seq,
        ID id,
        SharedPointer<unsigned char[]> buffer,
        size_t size,
        const Callback callback
      );

      void finalize (
        const ipc::Message::Seq& seq,
        ID id,
        const Callback callback
      );

      void getBuffer (
        const ipc::Message::Seq& seq,
        ID id,
        const Callback callback
      );

    private:
      mutable Mutex mutex;
      Map<ID, SharedPointer<Descriptor>> descriptors;

      JSON::Object::Entries describeEntry (const tar::Entry& entry) const;
      JSON::Object::Entries makeError (
        const String& source,
        const String& code,
        const String& message
      ) const;
  };
}

#endif
