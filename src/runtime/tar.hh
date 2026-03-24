#ifndef ORO_RUNTIME_TAR_H
#define ORO_RUNTIME_TAR_H

#include "platform.hh"

namespace oro::runtime::tar {
  struct SparseRegion {
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t dataOffset = 0;
  };

  struct Entry {
    String path;
    String linkpath;
    uint64_t size = 0;
    uint64_t storedSize = 0;
    uint64_t headerOffset = 0;
    uint64_t dataOffset = 0;
    uint32_t mode = 0;
    uint64_t mtime = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t devmajor = 0;
    uint32_t devminor = 0;
    String uname;
    String gname;
    char type = '0';
    Vector<SparseRegion> sparse;
    bool sparseFile = false;

    bool isFile () const {
      return type == '0' || type == '\0' || type == '7' || type == 'S';
    }

    bool isDirectory () const {
      return type == '5';
    }

    bool isSparse () const {
      return this->sparseFile;
    }
  };

  struct ArchiveStats {
    uint64_t size = 0;
    uint64_t entryCount = 0;
  };

  class Source {
    public:
      virtual ~Source () = default;
      virtual bool read (uint64_t offset, void* buffer, size_t size, size_t& bytesRead) = 0;
      virtual uint64_t size () const = 0;
  };

  class FileSource : public Source {
    public:
      FileSource (const String& path, bool enableMmap);
      ~FileSource () override;

      bool ok () const;
      bool read (uint64_t offset, void* buffer, size_t size, size_t& bytesRead) override;
      uint64_t size () const override;

    private:
      String path;
    #if !defined(_WIN32)
      int fd = -1;
    #else
      void* handle = nullptr;
    #endif
      uint64_t fileSize = 0;
      bool useMmap = false;
      const unsigned char* mapped = nullptr;
  };

  class ArchiveReader {
    public:
      using EntryList = Vector<Entry>;

      explicit ArchiveReader (std::unique_ptr<Source>&& source);
      ~ArchiveReader ();

      bool buildIndex (bool forceRebuild = false);

      const EntryList& entries () const;
      const Entry* find (const String& path) const;
      bool read (const Entry& entry, uint64_t offset, size_t length, Vector<uint8_t>& out);
      ArchiveStats stats () const;

    private:
      struct PaxMeta {
        String path;
        bool hasPath = false;
        String linkpath;
        bool hasLinkpath = false;
        String sparseName;
        bool hasSparseName = false;
        uint64_t sparseRealSize = 0;
        bool hasSparseRealSize = false;
        uint32_t sparseMajor = 0;
        uint32_t sparseMinor = 0;
        bool hasSparseMajor = false;
        bool hasSparseMinor = false;
        uint64_t sparseNumBlocks = 0;
        bool hasSparseNumBlocks = false;
        uint64_t sparsePendingOffset = 0;
        bool hasSparsePendingOffset = false;
        Vector<SparseRegion> sparse;
        bool hasSparse = false;
        uint64_t size = 0;
        bool hasSize = false;
        uint64_t mtime = 0;
        bool hasMtime = false;
        uint32_t uid = 0;
        uint32_t gid = 0;
        bool hasUid = false;
        bool hasGid = false;
        String uname;
        String gname;
        bool hasUname = false;
        bool hasGname = false;

        void clear () {
          path.clear();
          hasPath = false;
          linkpath.clear();
          hasLinkpath = false;
          sparseName.clear();
          hasSparseName = false;
          sparseRealSize = 0;
          hasSparseRealSize = false;
          sparseMajor = 0;
          sparseMinor = 0;
          hasSparseMajor = false;
          hasSparseMinor = false;
          sparseNumBlocks = 0;
          hasSparseNumBlocks = false;
          sparsePendingOffset = 0;
          hasSparsePendingOffset = false;
          sparse.clear();
          hasSparse = false;
          size = 0;
          hasSize = false;
          mtime = 0;
          hasMtime = false;
          uid = 0;
          gid = 0;
          hasUid = false;
          hasGid = false;
          uname.clear();
          gname.clear();
          hasUname = false;
          hasGname = false;
        }
      };

      std::unique_ptr<Source> source;
      EntryList index;
      Map<String, size_t> byPath;
      bool indexed = false;
      PaxMeta pendingPax;
      bool hasPendingPax = false;
      PaxMeta globalPax;
      bool hasGlobalPax = false;
      String pendingLongPath;
      bool hasPendingLongPath = false;
      String pendingLongLinkPath;
      bool hasPendingLongLinkPath = false;

      bool parseHeader (uint64_t offset, Entry& out);
      bool parsePaxExtendedHeader (const Entry& headerEntry, bool global);
      bool parsePaxSparseEntry (Entry& entry, PaxMeta& meta);
  };

  class MemorySource : public Source {
    public:
      MemorySource (SharedPointer<unsigned char[]> data, uint64_t size);
      ~MemorySource () override = default;

      bool read (uint64_t offset, void* buffer, size_t size, size_t& bytesRead) override;
      uint64_t size () const override;

    private:
      SharedPointer<unsigned char[]> data;
      uint64_t length = 0;
  };

  class Sink {
    public:
      virtual ~Sink () = default;
      virtual bool write (const void* data, size_t size) = 0;
      virtual bool flush () = 0;
      virtual uint64_t offset () const = 0;
  };

  class MemorySink : public Sink {
    public:
      MemorySink () = default;
      ~MemorySink () override = default;

      bool write (const void* data, size_t size) override;
      bool flush () override;
      uint64_t offset () const override;

      const Vector<unsigned char>& buffer () const {
        return this->data;
      }

    private:
      Vector<unsigned char> data;
  };

  class FileSink : public Sink {
    public:
      explicit FileSink (const String& path);
      ~FileSink () override;

      bool ok () const;
      bool write (const void* data, size_t size) override;
      bool flush () override;
      uint64_t offset () const override;

    private:
      String path;
      void* handle = nullptr;
      uint64_t currentOffset = 0;
  };

  class ArchiveWriter {
    public:
      struct EntryMeta {
        uint32_t uid;
        uint32_t gid;
        uint64_t mtime;
        String uname;
        String gname;
        bool hasUid;
        bool hasGid;
        bool hasMtime;
        bool hasUname;
        bool hasGname;

        EntryMeta ()
          : uid(0),
            gid(0),
            mtime(0),
            uname(),
            gname(),
            hasUid(false),
            hasGid(false),
            hasMtime(false),
            hasUname(false),
            hasGname(false) {}
      };

      explicit ArchiveWriter (std::unique_ptr<Sink>&& sink);
      ~ArchiveWriter ();

      bool beginEntry (
        const String& path,
        uint64_t size,
        uint32_t mode,
        uint64_t mtime,
        char type = '0',
        const String& linkpath = String(),
        uint32_t devmajor = 0,
        uint32_t devminor = 0,
        uint64_t sparseSize = 0,
        const Vector<SparseRegion>* sparse = nullptr
      );

      bool beginEntry (
        const String& path,
        uint64_t size,
        uint32_t mode,
        char type = '0',
        const String& linkpath = String(),
        uint32_t devmajor = 0,
        uint32_t devminor = 0,
        uint64_t sparseSize = 0,
        const Vector<SparseRegion>* sparse = nullptr,
        EntryMeta meta = {}
      );

      bool writeData (const unsigned char* data, size_t size);
      bool endEntry ();
      bool finish ();

      void setGlobalMeta (
        uint32_t uid,
        bool hasUid,
        uint32_t gid,
        bool hasGid,
        uint64_t mtime,
        bool hasMtime,
        const String& uname,
        bool hasUname,
        const String& gname,
        bool hasGname
      );

      Sink* sinkPtr () const {
        return this->sink.get();
      }

    private:
      struct GlobalMeta {
        uint32_t uid = 0;
        uint32_t gid = 0;
        uint64_t mtime = 0;
        String uname;
        String gname;
        bool hasUid = false;
        bool hasGid = false;
        bool hasMtime = false;
        bool hasUname = false;
        bool hasGname = false;
      };

      std::unique_ptr<Sink> sink;
      bool entryOpen = false;
      uint64_t declaredSize = 0;
      uint64_t writtenForEntry = 0;
      bool finished = false;
      GlobalMeta globalMeta;
      bool hasGlobalMeta = false;
      bool globalPaxWritten = false;

      bool writeHeader (
        const String& path,
        const String& linkpath,
        uint32_t devmajor,
        uint32_t devminor,
        uint64_t size,
        uint32_t mode,
        uint64_t mtime,
        char type,
        uint32_t uid,
        uint32_t gid,
        const String& uname,
        const String& gname
      );

      bool writeZeroBlock ();
  };
}

#endif
