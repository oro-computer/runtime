#include "tests.hh"
#include "src/runtime/tar.hh"

#include <cstring>
#include <memory>

namespace oro::Tests {
  void tar (Harness& t) {
    using oro::runtime::tar::ArchiveWriter;
    using oro::runtime::tar::ArchiveReader;
    using oro::runtime::tar::Sink;
    using oro::runtime::tar::Source;
    using oro::runtime::SharedPointer;

    t.test("tar: base-256 size encoding round-trip for large size", [](auto t) {
      // Construct an in-memory tar header with a very large size that
      // cannot be represented as standard octal, and ensure the header
      // size field uses base-256 encoding that decodes back to the original.
      constexpr uint64_t kLargeSize = (uint64_t(1) << 33); // just over 8 GiB
      constexpr size_t kCapacity = 2048;

      auto bytes = std::make_shared<unsigned char[]>(kCapacity);
      std::memset(bytes.get(), 0, kCapacity);

      struct MemorySink : public Sink {
        SharedPointer<unsigned char[]> data;
        size_t capacity;
        uint64_t offsetValue = 0;

        MemorySink (SharedPointer<unsigned char[]> d, size_t cap)
          : data(std::move(d)),
            capacity(cap) {}

        bool write (const void* src, size_t size) override {
          if (!data || offsetValue + size > capacity) {
            return false;
          }
          std::memcpy(data.get() + offsetValue, src, size);
          offsetValue += size;
          return true;
        }

        bool flush () override {
          return true;
        }

        uint64_t offset () const override {
          return offsetValue;
        }
      };

      // Write a single entry archive into memory.
      auto* sinkPtr = new MemorySink(bytes, kCapacity);
      ArchiveWriter writer(std::unique_ptr<MemorySink>(sinkPtr));

      const oro::runtime::String path = "large.bin";
      const uint32_t mode = 0644;
      const uint64_t mtime = 0;

      t.assert(writer.beginEntry(path, kLargeSize, mode, mtime, '0'), "beginEntry succeeds for large size");
      t.assert(writer.endEntry(), "endEntry succeeds");
      t.assert(writer.finish(), "finish succeeds");

      const auto archiveSize = sinkPtr->offset();
      t.assert(archiveSize >= 512, "archive has at least one header block");

      // Decode the size field from the first header block to ensure base-256.
      struct HeaderView {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char chksum[8];
        char typeflag;
        char linkname[100];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        char prefix[155];
        char padding[12];
      };

      static_assert(sizeof(HeaderView) == 512, "HeaderView must be 512 bytes");
      auto* header = reinterpret_cast<const HeaderView*>(bytes.get());

      const unsigned char* sz = reinterpret_cast<const unsigned char*>(header->size);
      // Expect base-256 (high bit set)
      t.assert((sz[0] & 0x80u) != 0, "size field uses base-256 encoding");

      // Decode using the same semantics as parseNumber's base-256 branch.
      uint64_t decoded = 0;
      unsigned char first = sz[0] & 0x7f;
      decoded = first;
      for (size_t i = 1; i < sizeof(header->size); ++i) {
        decoded = (decoded << 8u) | sz[i];
      }

      t.equals(static_cast<int64_t>(decoded), static_cast<int64_t>(kLargeSize), "decoded base-256 size matches original");
    });

    t.test("tar: PAX long path round-trip via ArchiveWriter and ArchiveReader", [](auto t) {
      // Construct an in-memory tar archive with a path that exceeds the
      // ustar name/prefix limits so that ArchiveWriter emits a per-file
      // PAX header, and ensure ArchiveReader resolves the full path and
      // data correctly.
      constexpr size_t kCapacity = 16 * 1024;

      auto bytes = std::make_shared<unsigned char[]>(kCapacity);
      std::memset(bytes.get(), 0, kCapacity);

      struct MemorySink : public Sink {
        SharedPointer<unsigned char[]> data;
        size_t capacity;
        uint64_t offsetValue = 0;

        MemorySink (SharedPointer<unsigned char[]> d, size_t cap)
          : data(std::move(d)),
            capacity(cap) {}

        bool write (const void* src, size_t size) override {
          if (!data || offsetValue + size > capacity) {
            return false;
          }
          std::memcpy(data.get() + offsetValue, src, size);
          offsetValue += size;
          return true;
        }

        bool flush () override {
          return true;
        }

        uint64_t offset () const override {
          return offsetValue;
        }
      };

      struct MemorySource : public Source {
        SharedPointer<unsigned char[]> data;
        uint64_t length = 0;

        MemorySource (SharedPointer<unsigned char[]> d, uint64_t len)
          : data(std::move(d)),
            length(len) {}

        bool read (uint64_t offset, void* buffer, size_t size, size_t& bytesRead) override {
          bytesRead = 0;
          if (!data || buffer == nullptr) {
            return false;
          }
          if (offset >= length) {
            return true;
          }
          const auto remaining = length - offset;
          if (size > remaining) {
            size = static_cast<size_t>(remaining);
          }
          if (size == 0) {
            return true;
          }
          std::memcpy(
            buffer,
            data.get() + offset,
            size
          );
          bytesRead = size;
          return true;
        }

        uint64_t size () const override {
          return length;
        }
      };

      auto* sinkPtr = new MemorySink(bytes, kCapacity);
      ArchiveWriter writer(std::unique_ptr<MemorySink>(sinkPtr));

      const oro::runtime::String longPath =
        "very/long/path/that/exceeds/the/ustar/name/field/limits/and/therefore/needs/pax/header/filename.txt";
      const oro::runtime::String payload = "pax-long-path";
      const uint32_t mode = 0644;
      const uint64_t mtime = 123456789;

      t.assert(writer.beginEntry(longPath, payload.size(), mode, mtime, '0'), "beginEntry succeeds for long path");
      t.assert(writer.writeData(reinterpret_cast<const unsigned char*>(payload.data()), payload.size()), "writeData succeeds");
      t.assert(writer.endEntry(), "endEntry succeeds");
      t.assert(writer.finish(), "finish succeeds");

      const auto archiveSize = sinkPtr->offset();
      t.assert(archiveSize > 0, "archive has non-zero size");

      // Read back using ArchiveReader from an in-memory source.
      auto source = std::make_unique<MemorySource>(bytes, archiveSize);
      ArchiveReader reader(std::move(source));
      t.assert(reader.buildIndex(), "buildIndex succeeds");

      const auto& entries = reader.entries();
      t.assert(entries.size() == 1, "exactly one entry indexed");

      const auto& entry = entries[0];
      t.equals(entry.path, longPath, "entry.path matches full long path from PAX");
      t.equals(static_cast<int64_t>(entry.size), static_cast<int64_t>(payload.size()), "entry.size matches payload size");
      t.equals(static_cast<int64_t>(entry.mtime), static_cast<int64_t>(mtime), "entry.mtime matches");

      oro::runtime::Vector<uint8_t> out;
      t.assert(reader.read(entry, 0, payload.size(), out), "read succeeds");
      t.equals(static_cast<int64_t>(out.size()), static_cast<int64_t>(payload.size()), "read size matches payload size");
      t.equals(oro::runtime::String(reinterpret_cast<const char*>(out.data()), out.size()), payload, "entry payload round-trips correctly");
    });
  }
}
