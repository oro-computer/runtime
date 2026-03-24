#include "tar.hh"

#include "filesystem.hh"

#include <cstddef>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <limits>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace oro::runtime::tar {
  namespace {
    constexpr size_t kBlockSize = 512;
    constexpr size_t kSparseOffsetFieldSize = 12;
    constexpr size_t kSparsePairSize = 24;

    struct Header {
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

    constexpr size_t kGnuSparsePairsOffset = 386;
    constexpr size_t kGnuSparsePairsCount = 4;
    constexpr size_t kGnuSparseIsExtendedOffset = 482;
    constexpr size_t kGnuSparseRealSizeOffset = 483;

    constexpr size_t kGnuSparseExtPairsCount = 21;
    constexpr size_t kGnuSparseExtIsExtendedOffset = 504;

    static bool isZeroBlock (const unsigned char* block, size_t size) {
      for (size_t i = 0; i < size; ++i) {
        if (block[i] != 0) {
          return false;
        }
      }
      return true;
    }

    static bool parseNumber (const char* data, size_t size, uint64_t& out) {
      out = 0;

      if (size == 0) {
        return true;
      }

      const auto* bytes = reinterpret_cast<const unsigned char*>(data);

      if (bytes[0] & 0x80) {
        uint64_t value = static_cast<uint64_t>(bytes[0] & 0x7fu);

        // The remaining bits are a signed two's complement number; we only
        // accept non-negative values (sign bit must be 0).
        if (value & 0x40u) {
          return false;
        }

        for (size_t i = 1; i < size; ++i) {
          if (value > (std::numeric_limits<uint64_t>::max() >> 8u)) {
            return false;
          }
          value = (value << 8u) | bytes[i];
        }

        out = value;
        return true;
      }

      size_t i = 0;
      while (i < size && data[i] == ' ') {
        i++;
      }

      if (i >= size || data[i] == '\0') {
        out = 0;
        return true;
      }

      uint64_t value = 0;
      for (; i < size; ++i) {
        const auto c = data[i];
        if (c == '\0' || c == ' ') {
          break;
        }
        if (c < '0' || c > '7') {
          return false;
        }
        if (value > (std::numeric_limits<uint64_t>::max() >> 3u)) {
          return false;
        }
        value = (value << 3u) + static_cast<uint64_t>(c - '0');
      }

      out = value;
      return true;
    }

    static bool parseMode (const char* data, size_t size, uint32_t& out) {
      uint64_t value = 0;
      if (!parseNumber(data, size, value)) {
        return false;
      }

      if (value > std::numeric_limits<uint32_t>::max()) {
        out = std::numeric_limits<uint32_t>::max();
      } else {
        out = static_cast<uint32_t>(value);
      }

      return true;
    }

    static bool parseTime (const char* data, size_t size, uint64_t& out) {
      return parseNumber(data, size, out);
    }

    static String parseString (const char* data, size_t size) {
      size_t len = 0;
      for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\0') {
          break;
        }
        len++;
      }
      return String(data, len);
    }

    static bool verifyChecksum (const Header& header) {
      const auto* raw = reinterpret_cast<const unsigned char*>(&header);
      uint64_t sumUnsigned = 0;
      int64_t sumSigned = 0;
      for (size_t i = 0; i < sizeof(Header); ++i) {
        if (i >= offsetof(Header, chksum) && i < offsetof(Header, chksum) + sizeof(header.chksum)) {
          sumUnsigned += static_cast<unsigned char>(' ');
          sumSigned += static_cast<unsigned char>(' ');
        } else {
          sumUnsigned += raw[i];
          sumSigned += static_cast<signed char>(raw[i]);
        }
      }

      uint64_t stored = 0;
      bool sawDigit = false;

      for (size_t i = 0; i < sizeof(header.chksum); ++i) {
        const auto c = header.chksum[i];
        if (c == '\0') {
          break;
        }

        if (c == ' ') {
          if (sawDigit) {
            break;
          }
          continue;
        }

        if (c < '0' || c > '7') {
          return false;
        }

        sawDigit = true;
        stored = (stored << 3u) + static_cast<uint64_t>(c - '0');
      }

      if (!sawDigit) {
        return false;
      }

      if (stored == sumUnsigned) {
        return true;
      }

      if (sumSigned >= 0 && stored == static_cast<uint64_t>(sumSigned)) {
        return true;
      }

      return false;
    }

    static String joinPath (const String& prefix, const String& name) {
      if (prefix.empty()) {
        return name;
      }
      if (name.empty()) {
        return prefix;
      }
      String path = prefix;
      if (!path.ends_with('/')) {
        path.push_back('/');
      }
      path += name;
      return path;
    }

    static String normalizePathKey (String path) {
      while (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        path.erase(0, 1);
      }

      while (path.size() >= 2 && path[0] == '.' && (path[1] == '/' || path[1] == '\\')) {
        path.erase(0, 2);
      }

      return path;
    }
  }

  FileSource::FileSource (const String& path, bool enableMmap)
    : path(path),
  #if !defined(_WIN32)
      fd(-1),
  #else
      handle(nullptr),
  #endif
      fileSize(0),
      useMmap(enableMmap),
      mapped(nullptr) {
    const auto resolved = filesystem::Resource::resolve(path);
    std::filesystem::path target;

    if (!resolved.empty()) {
      target = resolved;
    } else {
      target = std::filesystem::path(path);
    }

  #if !defined(_WIN32)
    this->fd = ::open(target.string().c_str(), O_RDONLY);
    if (this->fd < 0) {
      return;
    }

    struct stat st;
    if (::fstat(this->fd, &st) != 0) {
      ::close(this->fd);
      this->fd = -1;
      return;
    }

    this->fileSize = static_cast<uint64_t>(st.st_size);

    if (this->useMmap &&
        this->fileSize > 0 &&
        this->fileSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      void* addr = ::mmap(
        nullptr,
        static_cast<size_t>(this->fileSize),
        PROT_READ,
        MAP_PRIVATE,
        this->fd,
        0
      );

      if (addr != MAP_FAILED) {
        this->mapped = reinterpret_cast<unsigned const char*>(addr);
      } else {
        this->useMmap = false;
      }
    }
  #else
    HANDLE handle = CreateFileA(
      target.string().c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
      return;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size)) {
      CloseHandle(handle);
      return;
    }

    this->fileSize = static_cast<uint64_t>(size.QuadPart);
    this->handle = handle;
    this->useMmap = false;
  #endif
  }

  FileSource::~FileSource () {
  #if !defined(_WIN32)
    if (this->mapped != nullptr) {
      ::munmap(const_cast<unsigned char*>(this->mapped), static_cast<size_t>(this->fileSize));
      this->mapped = nullptr;
    }

    if (this->fd >= 0) {
      ::close(this->fd);
      this->fd = -1;
    }
  #else
    if (this->handle != nullptr && this->handle != INVALID_HANDLE_VALUE) {
      CloseHandle(static_cast<HANDLE>(this->handle));
      this->handle = nullptr;
    }
  #endif
  }

  bool FileSource::ok () const {
  #if !defined(_WIN32)
    return this->fd >= 0 && this->fileSize > 0;
  #else
    return this->handle != nullptr && this->handle != INVALID_HANDLE_VALUE && this->fileSize > 0;
  #endif
  }

  uint64_t FileSource::size () const {
    return this->fileSize;
  }

  bool FileSource::read (uint64_t offset, void* buffer, size_t size, size_t& bytesRead) {
    bytesRead = 0;
    if (!this->ok() || buffer == nullptr) {
      return false;
    }

    if (size == 0) {
      return true;
    }

    if (offset >= this->fileSize) {
      return true;
    }

    const auto remaining = this->fileSize - offset;
    if (size > remaining) {
      size = static_cast<size_t>(remaining);
    }

    if (size == 0) {
      return true;
    }

  #if !defined(_WIN32)
    if (this->mapped != nullptr) {
      std::memcpy(
        buffer,
        this->mapped + offset,
        size
      );
      bytesRead = size;
      return true;
    }

    ssize_t result = ::pread(
      this->fd,
      buffer,
      size,
      static_cast<off_t>(offset)
    );

    if (result < 0) {
      return false;
    }

    bytesRead = static_cast<size_t>(result);
    return true;
  #else
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    HANDLE handle = static_cast<HANDLE>(this->handle);

    if (!SetFilePointerEx(handle, li, nullptr, FILE_BEGIN)) {
      return false;
    }

    DWORD read = 0;
    if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &read, nullptr)) {
      return false;
    }

    bytesRead = static_cast<size_t>(read);
    return true;
  #endif
  }

  ArchiveReader::ArchiveReader (std::unique_ptr<Source>&& source)
    : source(std::move(source)) {
  }

  ArchiveReader::~ArchiveReader () = default;

  bool ArchiveReader::buildIndex (bool forceRebuild) {
    if (this->indexed && !forceRebuild) {
      return true;
    }

    this->index.clear();
    this->byPath.clear();
    this->pendingPax.clear();
    this->globalPax.clear();
    this->hasPendingPax = false;
    this->hasGlobalPax = false;
    this->pendingLongPath.clear();
    this->pendingLongLinkPath.clear();
    this->hasPendingLongPath = false;
    this->hasPendingLongLinkPath = false;

    if (!this->source) {
      return false;
    }

    const auto totalSize = this->source->size();
    uint64_t offset = 0;
    unsigned zeroBlocks = 0;

    Vector<unsigned char> block(kBlockSize);

    while (offset < totalSize) {
      const auto remainingArchive = totalSize - offset;
      if (remainingArchive < kBlockSize) {
        // Truncated header block; treat as an invalid archive.
        return false;
      }

      size_t bytesRead = 0;
      if (!this->source->read(offset, block.data(), kBlockSize, bytesRead) || bytesRead != kBlockSize) {
        return false;
      }

      if (isZeroBlock(block.data(), kBlockSize)) {
        zeroBlocks++;
        offset += kBlockSize;
        if (zeroBlocks >= 2) {
          break;
        }
        continue;
      }

      zeroBlocks = 0;

      Entry entry;
      if (!this->parseHeader(offset, entry)) {
        return false;
      }

      const uint64_t headerEnd = offset + kBlockSize;
      if (headerEnd > totalSize) {
        return false;
      }

      const uint64_t maxPayload = totalSize - headerEnd;
      uint64_t effectiveSize = entry.storedSize;
      if (entry.type != 'x' &&
          entry.type != 'g' &&
          entry.type != 'L' &&
          entry.type != 'K' &&
          entry.type != 'S' &&
          this->hasPendingPax &&
          this->pendingPax.hasSize &&
          !this->pendingPax.hasSparse) {
        effectiveSize = this->pendingPax.size;
        entry.storedSize = effectiveSize;
      }

      if (effectiveSize > maxPayload) {
        return false;
      }

      const auto padding = (effectiveSize % kBlockSize) ? (kBlockSize - (effectiveSize % kBlockSize)) : 0;
      if (effectiveSize + padding > maxPayload) {
        return false;
      }

      const auto nextOffset = headerEnd + effectiveSize + padding;

      // PAX extended header for the next file (per-file)
      if (entry.type == 'x') {
        if (entry.size > 0) {
          if (!this->parsePaxExtendedHeader(entry, false)) {
            return false;
          }
        }
        offset = nextOffset;
        continue;
      }

      // PAX global extended header for subsequent files
      if (entry.type == 'g') {
        if (entry.size > 0) {
          if (!this->parsePaxExtendedHeader(entry, true)) {
            return false;
          }
        }
        offset = nextOffset;
        continue;
      }

      // GNU tar long path / long link path extension entries.
      if (entry.type == 'L' || entry.type == 'K') {
        constexpr uint64_t kMaxLongPathSize = 1024 * 1024;
        if (entry.size > kMaxLongPathSize) {
          return false;
        }

        if (entry.size > std::numeric_limits<size_t>::max()) {
          return false;
        }

        const size_t payloadSize = static_cast<size_t>(entry.size);
        Vector<unsigned char> buffer(payloadSize);
        size_t bytesRead = 0;

        if (payloadSize > 0) {
          if (!this->source->read(entry.dataOffset, buffer.data(), payloadSize, bytesRead) || bytesRead != payloadSize) {
            return false;
          }
        }

        String value;
        if (payloadSize > 0) {
          value.assign(reinterpret_cast<const char*>(buffer.data()), payloadSize);
          const auto nul = value.find('\0');
          if (nul != String::npos) {
            value.resize(nul);
          }
          while (!value.empty() && (value.back() == '\n' || value.back() == '\0')) {
            value.pop_back();
          }
        }

        if (entry.type == 'L') {
          this->pendingLongPath = value;
          this->hasPendingLongPath = !value.empty();
        } else {
          this->pendingLongLinkPath = value;
          this->hasPendingLongLinkPath = !value.empty();
        }

        offset = nextOffset;
        continue;
      }

      // Apply any global PAX metadata to this entry first
      if (this->hasGlobalPax) {
        if (this->globalPax.hasMtime) {
          entry.mtime = this->globalPax.mtime;
        }
        if (this->globalPax.hasUid) {
          entry.uid = this->globalPax.uid;
        }
        if (this->globalPax.hasGid) {
          entry.gid = this->globalPax.gid;
        }
        if (this->globalPax.hasUname) {
          entry.uname = this->globalPax.uname;
        }
        if (this->globalPax.hasGname) {
          entry.gname = this->globalPax.gname;
        }
      }

      // Apply any pending per-file PAX metadata to this entry
      if (this->hasPendingPax) {
        if (this->pendingPax.hasPath) {
          entry.path = this->pendingPax.path;
        }
        if (this->pendingPax.hasLinkpath) {
          entry.linkpath = this->pendingPax.linkpath;
        }
        if (this->pendingPax.hasSparseName) {
          entry.path = this->pendingPax.sparseName;
        }
        if (this->pendingPax.hasSize && entry.type != 'S' && !this->pendingPax.hasSparse) {
          entry.size = this->pendingPax.size;
        }
        if (this->pendingPax.hasMtime) {
          entry.mtime = this->pendingPax.mtime;
        }
        if (this->pendingPax.hasUid) {
          entry.uid = this->pendingPax.uid;
        }
        if (this->pendingPax.hasGid) {
          entry.gid = this->pendingPax.gid;
        }
        if (this->pendingPax.hasUname) {
          entry.uname = this->pendingPax.uname;
        }
        if (this->pendingPax.hasGname) {
          entry.gname = this->pendingPax.gname;
        }

        if (this->pendingPax.hasSparse) {
          if (!this->parsePaxSparseEntry(entry, this->pendingPax)) {
            return false;
          }
        }

        this->pendingPax.clear();
        this->hasPendingPax = false;
      }

      if (this->hasPendingLongPath) {
        entry.path = this->pendingLongPath;
        this->pendingLongPath.clear();
        this->hasPendingLongPath = false;
      }

      if (this->hasPendingLongLinkPath) {
        entry.linkpath = this->pendingLongLinkPath;
        this->pendingLongLinkPath.clear();
        this->hasPendingLongLinkPath = false;
      }

      if (entry.path.size() > 0) {
        const auto idx = this->index.size();
        this->byPath.insert_or_assign(entry.path, idx);
        const auto normalized = normalizePathKey(entry.path);
        if (!normalized.empty() && normalized != entry.path) {
          this->byPath.insert_or_assign(normalized, idx);
        }

        if (entry.isDirectory()) {
          String alias = entry.path;
          if (alias.ends_with('/')) {
            alias.pop_back();
          } else {
            alias.push_back('/');
          }

          if (!alias.empty() && this->byPath.find(alias) == this->byPath.end()) {
            this->byPath.emplace(alias, idx);
          }

          const auto normalizedAlias = normalizePathKey(alias);
          if (!normalizedAlias.empty() &&
              normalizedAlias != alias &&
              this->byPath.find(normalizedAlias) == this->byPath.end()) {
            this->byPath.emplace(normalizedAlias, idx);
          }
        }

        this->index.push_back(entry);
      }

      offset = nextOffset;
    }

    this->indexed = true;
    return true;
  }

  const ArchiveReader::EntryList& ArchiveReader::entries () const {
    return this->index;
  }

  const Entry* ArchiveReader::find (const String& path) const {
    if (path.empty()) {
      return nullptr;
    }

    const auto it = this->byPath.find(path);
    if (it == this->byPath.end()) {
      return nullptr;
    }

    const auto idx = it->second;
    if (idx >= this->index.size()) {
      return nullptr;
    }

    return &this->index[idx];
  }

  bool ArchiveReader::read (const Entry& entry, uint64_t offset, size_t length, Vector<uint8_t>& out) {
    if (!this->source) {
      return false;
    }

    if (offset >= entry.size) {
      out.clear();
      return true;
    }

    const auto remaining = entry.size - offset;
    if (length > remaining) {
      length = static_cast<size_t>(remaining);
    }

    out.assign(length, 0);
    if (entry.isSparse()) {
      if (entry.sparse.empty()) {
        return true;
      }

      const uint64_t end = offset + static_cast<uint64_t>(length);

      for (const auto& region : entry.sparse) {
        if (region.length == 0) {
          continue;
        }

        const uint64_t regionStart = region.offset;
        const uint64_t regionEnd = region.offset + region.length;

        if (regionEnd <= offset || regionStart >= end) {
          continue;
        }

        const uint64_t readStart = std::max(offset, regionStart);
        const uint64_t readEnd = std::min(end, regionEnd);
        const uint64_t readLen64 = readEnd - readStart;
        if (readLen64 == 0) {
          continue;
        }

        if (readLen64 > std::numeric_limits<size_t>::max()) {
          out.clear();
          return false;
        }

        const size_t readLen = static_cast<size_t>(readLen64);
        const uint64_t srcOffset = region.dataOffset + (readStart - regionStart);
        const size_t dstOffset = static_cast<size_t>(readStart - offset);

        size_t bytesRead = 0;
        if (!this->source->read(srcOffset, out.data() + dstOffset, readLen, bytesRead) || bytesRead != readLen) {
          out.clear();
          return false;
        }
      }

      return true;
    }

    size_t bytesRead = 0;
    if (!this->source->read(entry.dataOffset + offset, out.data(), length, bytesRead)) {
      out.clear();
      return false;
    }

    if (bytesRead < length) {
      out.resize(bytesRead);
    }

    return true;
  }

  ArchiveStats ArchiveReader::stats () const {
    ArchiveStats s;
    if (this->source) {
      s.size = this->source->size();
    }
    s.entryCount = this->index.size();
    return s;
  }

  bool ArchiveReader::parseHeader (uint64_t offset, Entry& out) {
    if (!this->source) {
      return false;
    }

    Header header{};
    size_t bytesRead = 0;
    if (!this->source->read(offset, &header, sizeof(Header), bytesRead) || bytesRead != sizeof(Header)) {
      return false;
    }

    if (!verifyChecksum(header)) {
      return false;
    }

    const auto name = parseString(header.name, sizeof(header.name));
    out.type = header.typeflag ? header.typeflag : '0';
    const bool isGnu = std::memcmp(header.magic, "ustar ", sizeof(header.magic)) == 0;
    const auto prefix = (isGnu || out.type == 'S')
      ? String()
      : parseString(header.prefix, sizeof(header.prefix));

    out.path = joinPath(prefix, name);
    out.linkpath = parseString(header.linkname, sizeof(header.linkname));
    uint32_t mode = 0;
    if (!parseMode(header.mode, sizeof(header.mode), mode)) {
      return false;
    }
    out.mode = mode;

    uint64_t storedSize = 0;
    if (!parseNumber(header.size, sizeof(header.size), storedSize)) {
      return false;
    }
    out.size = storedSize;
    out.storedSize = storedSize;

    uint64_t mtime = 0;
    if (!parseTime(header.mtime, sizeof(header.mtime), mtime)) {
      return false;
    }
    out.mtime = mtime;

    uint64_t uid = 0;
    uint64_t gid = 0;
    uint64_t devmajor = 0;
    uint64_t devminor = 0;

    if (!parseNumber(header.uid, sizeof(header.uid), uid) ||
        !parseNumber(header.gid, sizeof(header.gid), gid) ||
        !parseNumber(header.devmajor, sizeof(header.devmajor), devmajor) ||
        !parseNumber(header.devminor, sizeof(header.devminor), devminor)) {
      return false;
    }

    out.uid = uid > std::numeric_limits<uint32_t>::max()
      ? std::numeric_limits<uint32_t>::max()
      : static_cast<uint32_t>(uid);
    out.gid = gid > std::numeric_limits<uint32_t>::max()
      ? std::numeric_limits<uint32_t>::max()
      : static_cast<uint32_t>(gid);
    out.devmajor = devmajor > std::numeric_limits<uint32_t>::max()
      ? std::numeric_limits<uint32_t>::max()
      : static_cast<uint32_t>(devmajor);
    out.devminor = devminor > std::numeric_limits<uint32_t>::max()
      ? std::numeric_limits<uint32_t>::max()
      : static_cast<uint32_t>(devminor);
    out.uname = parseString(header.uname, sizeof(header.uname));
    out.gname = parseString(header.gname, sizeof(header.gname));
    out.headerOffset = offset;
    out.dataOffset = offset + kBlockSize;
    out.sparse.clear();
    out.sparseFile = false;

    if (out.type == 'S') {
      const auto* raw = reinterpret_cast<const unsigned char*>(&header);
      uint64_t realSize = 0;
      if (!parseNumber(
        reinterpret_cast<const char*>(raw + kGnuSparseRealSizeOffset),
        kSparseOffsetFieldSize,
        realSize
      )) {
        return false;
      }

      Vector<SparseRegion> regions;

      uint64_t lastEnd = 0;
      bool sawTerminator = false;
      bool sawHeaderTerminator = false;

      auto acceptRegion = [&](uint64_t regionOffset, uint64_t regionLength) {
        if (regionLength == 0) {
          sawTerminator = true;
          return true;
        }

        if (regionOffset > realSize) {
          return false;
        }

        if (regionLength > realSize - regionOffset) {
          return false;
        }

        if (!regions.empty() && regionOffset < lastEnd) {
          return false;
        }

        SparseRegion region;
        region.offset = regionOffset;
        region.length = regionLength;
        regions.push_back(region);
        lastEnd = regionOffset + regionLength;
        return true;
      };

      for (size_t i = 0; i < kGnuSparsePairsCount; ++i) {
        const auto base = kGnuSparsePairsOffset + i * kSparsePairSize;
        uint64_t regionOffset = 0;
        uint64_t regionLength = 0;

        if (!parseNumber(reinterpret_cast<const char*>(raw + base), kSparseOffsetFieldSize, regionOffset) ||
            !parseNumber(reinterpret_cast<const char*>(raw + base + kSparseOffsetFieldSize), kSparseOffsetFieldSize, regionLength)) {
          return false;
        }

        if (!acceptRegion(regionOffset, regionLength)) {
          return false;
        }

        if (regionLength == 0) {
          sawHeaderTerminator = true;
          break;
        }
      }

      bool extended = raw[kGnuSparseIsExtendedOffset] != 0;
      if (extended && sawHeaderTerminator) {
        return false;
      }

      uint64_t extBlocks = 0;
      uint64_t extOffset = out.dataOffset;
      constexpr uint64_t kMaxSparseExtBlocks = 1024;

      while (extended) {
        if (extBlocks >= kMaxSparseExtBlocks) {
          return false;
        }

        unsigned char ext[kBlockSize] = {0};
        size_t bytesRead = 0;
        if (!this->source->read(extOffset, ext, kBlockSize, bytesRead) || bytesRead != kBlockSize) {
          return false;
        }

        extBlocks++;

        bool blockTerminator = false;
        for (size_t i = 0; i < kGnuSparseExtPairsCount; ++i) {
          const auto base = i * kSparsePairSize;
          uint64_t regionOffset = 0;
          uint64_t regionLength = 0;

          if (!parseNumber(reinterpret_cast<const char*>(ext + base), kSparseOffsetFieldSize, regionOffset) ||
              !parseNumber(reinterpret_cast<const char*>(ext + base + kSparseOffsetFieldSize), kSparseOffsetFieldSize, regionLength)) {
            return false;
          }

          if (!acceptRegion(regionOffset, regionLength)) {
            return false;
          }

          if (regionLength == 0) {
            blockTerminator = true;
            break;
          }
        }

        const bool nextExtended = ext[kGnuSparseExtIsExtendedOffset] != 0;
        if (blockTerminator && nextExtended) {
          return false;
        }

        extended = nextExtended;
        extOffset += kBlockSize;
      }

      if (!sawTerminator && realSize > 0) {
        return false;
      }

      uint64_t sum = 0;
      const uint64_t dataStart = offset + kBlockSize + extBlocks * kBlockSize;
      uint64_t cursor = dataStart;

      for (auto& region : regions) {
        region.dataOffset = cursor;
        cursor += region.length;
        sum += region.length;
      }

      if (sum != storedSize) {
        return false;
      }

      out.size = realSize;
      out.dataOffset = dataStart;
      out.storedSize = storedSize + extBlocks * kBlockSize;
      out.sparse = std::move(regions);
      out.sparseFile = true;
    }

    return true;
  }

  bool ArchiveReader::parsePaxExtendedHeader (const Entry& headerEntry, bool global) {
    if (!this->source || headerEntry.size == 0) {
      return true;
    }

    constexpr uint64_t kMaxPaxHeaderSize = 1024 * 1024;
    if (headerEntry.size > kMaxPaxHeaderSize) {
      return false;
    }

    if (headerEntry.size > std::numeric_limits<size_t>::max()) {
      return false;
    }

    const size_t payloadSize = static_cast<size_t>(headerEntry.size);
    Vector<unsigned char> buffer(payloadSize);
    size_t bytesRead = 0;
    if (!this->source->read(headerEntry.dataOffset, buffer.data(), payloadSize, bytesRead) || bytesRead != payloadSize) {
      return false;
    }

    PaxMeta& meta = global ? this->globalPax : this->pendingPax;

    size_t i = 0;
    while (i < bytesRead) {
      if (buffer[i] == '\0') {
        bool allZero = true;
        for (size_t j = i; j < bytesRead; ++j) {
          if (buffer[j] != '\0') {
            allZero = false;
            break;
          }
        }

        if (allZero) {
          break;
        }

        return false;
      }

      size_t len = 0;
      size_t start = i;

      // Parse decimal length
      while (i < bytesRead && buffer[i] != ' ') {
        const auto c = buffer[i];
        if (c < '0' || c > '9') {
          return false;
        }
        if (len > std::numeric_limits<size_t>::max() / 10) {
          return false;
        }
        len = len * 10 + static_cast<size_t>(c - '0');
        i++;
      }

      if (i >= bytesRead || buffer[i] != ' ') {
        return false;
      }

      i++; // skip space

      if (len == 0 || len > bytesRead - start) {
        return false;
      }

      const size_t recordEnd = start + len;
      const size_t keyStart = i;
      size_t eq = keyStart;

      while (eq < recordEnd && buffer[eq] != '=') {
        eq++;
      }

      if (eq >= recordEnd) {
        return false;
      }

      const size_t valueStart = eq + 1;
      size_t valueEnd = recordEnd;
      // strip trailing newline if present
      if (valueEnd > valueStart && buffer[valueEnd - 1] == '\n') {
        valueEnd--;
      }

      const String key(reinterpret_cast<const char*>(&buffer[keyStart]), eq - keyStart);
      const String value(reinterpret_cast<const char*>(&buffer[valueStart]), valueEnd - valueStart);

      if (!global && key == "path") {
        meta.path = value;
        meta.hasPath = true;
      } else if (!global && key == "linkpath") {
        meta.linkpath = value;
        meta.hasLinkpath = true;
      } else if (!global && key == "GNU.sparse.name") {
        meta.sparseName = value;
        meta.hasSparseName = true;
        meta.hasSparse = true;
      } else if (!global && key == "GNU.sparse.realsize") {
        try {
          meta.sparseRealSize = static_cast<uint64_t>(std::stoull(value));
          meta.hasSparseRealSize = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.size") {
        try {
          meta.sparseRealSize = static_cast<uint64_t>(std::stoull(value));
          meta.hasSparseRealSize = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.major") {
        try {
          meta.sparseMajor = static_cast<uint32_t>(std::stoul(value));
          meta.hasSparseMajor = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.minor") {
        try {
          meta.sparseMinor = static_cast<uint32_t>(std::stoul(value));
          meta.hasSparseMinor = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.numblocks") {
        try {
          meta.sparseNumBlocks = static_cast<uint64_t>(std::stoull(value));
          meta.hasSparseNumBlocks = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.map") {
        meta.sparse.clear();

        size_t start = 0;
        Vector<uint64_t> nums;
        nums.reserve(64);

        while (start < value.size()) {
          size_t end = value.find(',', start);
          if (end == String::npos) {
            end = value.size();
          }

          const auto token = value.substr(start, end - start);
          if (token.empty()) {
            return false;
          }

          try {
            nums.push_back(static_cast<uint64_t>(std::stoull(token)));
          } catch (...) {
            return false;
          }

          if (end == value.size()) {
            break;
          }

          start = end + 1;
        }

        if (nums.size() % 2 != 0) {
          return false;
        }

        for (size_t i = 0; i + 1 < nums.size(); i += 2) {
          SparseRegion region;
          region.offset = nums[i];
          region.length = nums[i + 1];
          meta.sparse.push_back(region);
        }

        meta.hasSparse = true;
      } else if (!global && key == "GNU.sparse.offset") {
        if (meta.hasSparsePendingOffset) {
          return false;
        }

        try {
          meta.sparsePendingOffset = static_cast<uint64_t>(std::stoull(value));
          meta.hasSparsePendingOffset = true;
          meta.hasSparse = true;
        } catch (...) {
          return false;
        }
      } else if (!global && key == "GNU.sparse.numbytes") {
        if (!meta.hasSparsePendingOffset) {
          return false;
        }

        uint64_t length = 0;
        try {
          length = static_cast<uint64_t>(std::stoull(value));
        } catch (...) {
          return false;
        }

        SparseRegion region;
        region.offset = meta.sparsePendingOffset;
        region.length = length;
        meta.sparse.push_back(region);
        meta.hasSparsePendingOffset = false;
        meta.sparsePendingOffset = 0;
        meta.hasSparse = true;
      } else if (!global && key == "size") {
        try {
          meta.size = static_cast<uint64_t>(std::stoull(value));
          meta.hasSize = true;
        } catch (...) {
          return false;
        }
      } else if (key == "mtime") {
        try {
          // mtime may be a floating point; we keep the integral seconds.
          double t = std::stod(value);
          if (t < 0) t = 0;
          meta.mtime = static_cast<uint64_t>(t);
          meta.hasMtime = true;
        } catch (...) {
          return false;
        }
      } else if (key == "uid") {
        try {
          meta.uid = static_cast<uint32_t>(std::stoul(value));
          meta.hasUid = true;
        } catch (...) {
          return false;
        }
      } else if (key == "gid") {
        try {
          meta.gid = static_cast<uint32_t>(std::stoul(value));
          meta.hasGid = true;
        } catch (...) {
          return false;
        }
      } else if (key == "uname") {
        meta.uname = value;
        meta.hasUname = true;
      } else if (key == "gname") {
        meta.gname = value;
        meta.hasGname = true;
      }

      i = recordEnd;
    }

    if (!global && meta.hasSparsePendingOffset) {
      return false;
    }

    if (global) {
      this->hasGlobalPax = this->globalPax.hasMtime || this->globalPax.hasUid ||
        this->globalPax.hasGid || this->globalPax.hasUname || this->globalPax.hasGname;
    } else {
      this->hasPendingPax = this->pendingPax.hasPath || this->pendingPax.hasLinkpath || this->pendingPax.hasSize ||
        this->pendingPax.hasMtime || this->pendingPax.hasUid || this->pendingPax.hasGid ||
        this->pendingPax.hasUname || this->pendingPax.hasGname || this->pendingPax.hasSparse;
    }
    return true;
  }

  bool ArchiveReader::parsePaxSparseEntry (Entry& entry, PaxMeta& meta) {
    if (!this->source) {
      return false;
    }

    if (!meta.hasSparseRealSize) {
      return false;
    }

    const uint64_t realSize = meta.sparseRealSize;
    Vector<SparseRegion> regions;

    auto acceptRegion = [&](uint64_t regionOffset, uint64_t regionLength) {
      if (regionLength == 0) {
        return true;
      }

      if (regionOffset > realSize) {
        return false;
      }

      if (regionLength > realSize - regionOffset) {
        return false;
      }

      if (!regions.empty()) {
        const auto& last = regions.back();
        const auto lastEnd = last.offset + last.length;
        if (regionOffset < lastEnd) {
          return false;
        }
      }

      SparseRegion region;
      region.offset = regionOffset;
      region.length = regionLength;
      regions.push_back(region);
      return true;
    };

    // GNU sparse format 1.0 stores the sparse map in the file payload.
    const bool isSparse10 = meta.hasSparseMajor && meta.hasSparseMinor &&
      meta.sparseMajor == 1 &&
      meta.sparseMinor == 0;

    if (isSparse10) {
      constexpr uint64_t kMaxSparseMapBytes = 8ull * 1024ull * 1024ull;
      constexpr uint64_t kMaxSparseEntries = 1024ull * 1024ull;

      const uint64_t scanLimit = std::min(entry.storedSize, kMaxSparseMapBytes);
      if (scanLimit == 0) {
        return false;
      }

      Vector<uint64_t> numbers;
      numbers.reserve(64);
      String current;

      uint64_t required = 0;
      uint64_t mapEnd = 0;

      uint64_t consumed = 0;
      size_t chunkSize = 4096;

      while (consumed < scanLimit && (required == 0 || numbers.size() < required)) {
        const auto remaining = scanLimit - consumed;
        const size_t toRead = static_cast<size_t>(std::min<uint64_t>(remaining, chunkSize));
        Vector<unsigned char> chunk(toRead);
        size_t bytesRead = 0;

        if (!this->source->read(entry.dataOffset + consumed, chunk.data(), toRead, bytesRead) || bytesRead != toRead) {
          return false;
        }

        for (size_t i = 0; i < bytesRead; ++i) {
          const auto ch = static_cast<unsigned char>(chunk[i]);
          consumed++;

          if (ch == '\0') {
            continue;
          }

          if (ch == '\r') {
            continue;
          }

          if (ch == '\n') {
            if (current.empty()) {
              continue;
            }

            uint64_t value = 0;
            try {
              value = static_cast<uint64_t>(std::stoull(current));
            } catch (...) {
              return false;
            }

            numbers.push_back(value);
            current.clear();

            if (required == 0) {
              const auto pairs = value;
              if (pairs == 0 || pairs > kMaxSparseEntries) {
                return false;
              }
              required = 1 + pairs * 2;
            }

            if (required != 0 && numbers.size() == required) {
              mapEnd = consumed;
              break;
            }

            continue;
          }

          if (ch < '0' || ch > '9') {
            return false;
          }

          if (current.size() >= 32) {
            return false;
          }

          current.push_back(static_cast<char>(ch));
        }
      }

      if (required == 0 || numbers.size() != required || mapEnd == 0) {
        return false;
      }

      const uint64_t mapBlocks = ((mapEnd + kBlockSize - 1) / kBlockSize) * kBlockSize;
      if (mapBlocks > entry.storedSize) {
        return false;
      }

      const uint64_t pairs = numbers[0];
      bool sawTerminator = false;
      uint64_t dataBytes = 0;

      for (size_t i = 0; i < pairs; ++i) {
        const uint64_t regionOffset = numbers[1 + i * 2];
        const uint64_t regionLength = numbers[1 + i * 2 + 1];

        if (!acceptRegion(regionOffset, regionLength)) {
          return false;
        }

        if (regionLength == 0) {
          sawTerminator = true;
          break;
        }

        dataBytes += regionLength;
      }

      if (!sawTerminator && realSize > 0) {
        return false;
      }

      if (dataBytes != entry.storedSize - mapBlocks) {
        return false;
      }

      const uint64_t dataStart = entry.dataOffset + mapBlocks;
      uint64_t cursor = dataStart;

      for (auto& region : regions) {
        region.dataOffset = cursor;
        cursor += region.length;
      }

      entry.size = realSize;
      entry.dataOffset = dataStart;
      entry.sparse = std::move(regions);
      entry.sparseFile = true;
      return true;
    }

    // GNU sparse format 0.0 and 0.1 store the sparse map in the PAX header.
    if (meta.sparse.empty()) {
      return false;
    }

    bool sawTerminator = false;
    uint64_t dataBytes = 0;

    for (const auto& region : meta.sparse) {
      if (!acceptRegion(region.offset, region.length)) {
        return false;
      }

      if (region.length == 0) {
        sawTerminator = true;
        break;
      }

      dataBytes += region.length;
    }

    if (!sawTerminator && realSize > 0) {
      return false;
    }

    if (dataBytes != entry.storedSize) {
      return false;
    }

    uint64_t cursor = entry.dataOffset;
    for (auto& region : regions) {
      region.dataOffset = cursor;
      cursor += region.length;
    }

    entry.size = realSize;
    entry.sparse = std::move(regions);
    entry.sparseFile = true;
    return true;
  }

  FileSink::FileSink (const String& path)
    : path(path),
      handle(nullptr),
      currentOffset(0) {
    const auto resolved = filesystem::Resource::resolve(path);
    std::filesystem::path target;

    if (!resolved.empty()) {
      target = resolved;
    } else {
      // For writable archives we allow creating new files at `path` even when
      // `Resource::resolve` cannot find an existing resource. In that case we
      // treat `path` as a native filesystem path and ensure its parent
      // directory exists before opening.
      target = std::filesystem::path(path);
    }

  #if !defined(_WIN32)
    auto parent = target.parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
    }

    FILE* file = std::fopen(target.string().c_str(), "wb");
    this->handle = file;

    if (!file) {
      return;
    }
  #else
    auto parent = target.parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
    }

    HANDLE file = CreateFileA(
      target.string().c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
      this->handle = nullptr;
      return;
    }

    this->handle = file;
  #endif
  }

  FileSink::~FileSink () {
  #if !defined(_WIN32)
    if (this->handle != nullptr) {
      auto* file = static_cast<FILE*>(this->handle);
      std::fflush(file);
      std::fclose(file);
      this->handle = nullptr;
    }
  #else
    if (this->handle != nullptr && this->handle != INVALID_HANDLE_VALUE) {
      FlushFileBuffers(static_cast<HANDLE>(this->handle));
      CloseHandle(static_cast<HANDLE>(this->handle));
      this->handle = nullptr;
    }
  #endif
  }

  bool FileSink::ok () const {
    return this->handle != nullptr;
  }

  uint64_t FileSink::offset () const {
    return this->currentOffset;
  }

  bool FileSink::write (const void* data, size_t size) {
    if (!this->ok() || data == nullptr || size == 0) {
      return false;
    }

  #if !defined(_WIN32)
    auto* file = static_cast<FILE*>(this->handle);
    const auto written = std::fwrite(data, 1, size, file);
    if (written != size) {
      return false;
    }
  #else
    DWORD written = 0;
    if (!WriteFile(
      static_cast<HANDLE>(this->handle),
      data,
      static_cast<DWORD>(size),
      &written,
      nullptr
    )) {
      return false;
    }
    if (written != size) {
      return false;
    }
  #endif

    this->currentOffset += size;
    return true;
  }

  bool FileSink::flush () {
    if (!this->ok()) {
      return false;
    }

  #if !defined(_WIN32)
    auto* file = static_cast<FILE*>(this->handle);
    return std::fflush(file) == 0;
  #else
    return FlushFileBuffers(static_cast<HANDLE>(this->handle)) != 0;
  #endif
  }

  bool MemorySink::write (const void* data, size_t size) {
    if (data == nullptr || size == 0) {
      return false;
    }

    const auto* bytes = static_cast<const unsigned char*>(data);
    const auto oldSize = this->data.size();
    this->data.resize(oldSize + size);
    std::memcpy(this->data.data() + oldSize, bytes, size);
    return true;
  }

  bool MemorySink::flush () {
    return true;
  }

  uint64_t MemorySink::offset () const {
    return static_cast<uint64_t>(this->data.size());
  }

  ArchiveWriter::ArchiveWriter (std::unique_ptr<Sink>&& sink)
    : sink(std::move(sink)),
      entryOpen(false),
      declaredSize(0),
      writtenForEntry(0),
      finished(false) {
  }

  ArchiveWriter::~ArchiveWriter () {
    this->finish();
  }

  void ArchiveWriter::setGlobalMeta (
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
  ) {
    this->globalMeta.uid = uid;
    this->globalMeta.gid = gid;
    this->globalMeta.mtime = mtime;
    this->globalMeta.uname = uname;
    this->globalMeta.gname = gname;
    this->globalMeta.hasUid = hasUid;
    this->globalMeta.hasGid = hasGid;
    this->globalMeta.hasMtime = hasMtime;
    this->globalMeta.hasUname = hasUname;
    this->globalMeta.hasGname = hasGname;
    this->hasGlobalMeta = hasUid || hasGid || hasMtime || hasUname || hasGname;
  }

  bool ArchiveWriter::writeHeader (
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
  ) {
    if (!this->sink) {
      return false;
    }

    Header header{};
    std::memset(&header, 0, sizeof(Header));

    String name = path;
    String prefix;

    if (name.size() > sizeof(header.name)) {
      auto pos = name.rfind('/');
      if (pos != String::npos && pos < name.size() - 1) {
        prefix = name.substr(0, pos);
        name = name.substr(pos + 1);
      }
    }

    if (name.size() > sizeof(header.name)) {
      return false;
    }

    if (prefix.size() > sizeof(header.prefix)) {
      return false;
    }

    std::memcpy(header.name, name.c_str(), name.size());

    auto writeOctal = [](char* dst, size_t width, uint64_t value) {
      char buf[32] = {0};
      std::snprintf(
        buf,
        sizeof(buf),
        "%0*llo",
        static_cast<int>(width - 1),
        static_cast<unsigned long long>(value)
      );
      std::memcpy(dst, buf, width);
    };

    auto maxOctalValue = [](size_t fieldWidth) -> uint64_t {
      if (fieldWidth <= 1) {
        return 0;
      }

      const size_t digits = fieldWidth - 1;
      const size_t bits = digits * 3;
      if (bits >= 64) {
        return std::numeric_limits<uint64_t>::max();
      }

      if (bits == 0) {
        return 0;
      }

      return (uint64_t(1) << bits) - 1;
    };

    auto writeNumberField = [&](char* dst, size_t width, uint64_t value) -> bool {
      const uint64_t maxOctal = maxOctalValue(width);
      if (value <= maxOctal) {
        writeOctal(dst, width, value);
        return true;
      }

      unsigned char buf[32] = {0};
      if (width > sizeof(buf)) {
        return false;
      }

      uint64_t v = value;
      for (size_t i = 0; i < width; ++i) {
        const size_t idx = width - 1 - i;
        buf[idx] = static_cast<unsigned char>(v & 0xffu);
        v >>= 8u;
      }

      if (v != 0) {
        return false;
      }

      buf[0] |= 0x80u;
      if (buf[0] & 0x40u) {
        return false;
      }

      std::memcpy(dst, buf, width);
      return true;
    };

    // mode always uses octal representation
    writeOctal(header.mode, sizeof(header.mode), mode);
    if (!writeNumberField(header.mtime, sizeof(header.mtime), mtime)) {
      return false;
    }

    if (!writeNumberField(header.uid, sizeof(header.uid), uid) ||
        !writeNumberField(header.gid, sizeof(header.gid), gid)) {
      return false;
    }

    if (!linkpath.empty()) {
      if (linkpath.size() > sizeof(header.linkname)) {
        return false;
      }
      std::memcpy(header.linkname, linkpath.c_str(), linkpath.size());
    }

    if (!writeNumberField(header.devmajor, sizeof(header.devmajor), devmajor) ||
        !writeNumberField(header.devminor, sizeof(header.devminor), devminor)) {
      return false;
    }

    if (!uname.empty()) {
      const auto len = std::min(uname.size(), sizeof(header.uname));
      std::memcpy(header.uname, uname.c_str(), len);
    }

    if (!gname.empty()) {
      const auto len = std::min(gname.size(), sizeof(header.gname));
      std::memcpy(header.gname, gname.c_str(), len);
    }

    // size may require base-256 encoding for very large entries
    constexpr uint64_t kMaxOctalSize = (uint64_t(1) << 33) - 1; // 11 octal digits
    if (size <= kMaxOctalSize) {
      writeOctal(header.size, sizeof(header.size), size);
    } else {
      unsigned char buf[sizeof(header.size)] = {0};
      uint64_t v = size;

      // big-endian representation; highest bits in buf[0]
      for (size_t i = sizeof(buf); i-- > 0;) {
        buf[i] = static_cast<unsigned char>(v & 0xffu);
        v >>= 8;
      }

      // set base-256 flag bit; parseNumber will mask it off
      buf[0] |= 0x80u;
      std::memcpy(header.size, buf, sizeof(header.size));
    }

    header.typeflag = type;
    std::memcpy(header.magic, "ustar", 5);
    std::memcpy(header.version, "00", 2);

    if (!prefix.empty()) {
      std::memcpy(header.prefix, prefix.c_str(), prefix.size());
    }

    std::memset(header.chksum, ' ', sizeof(header.chksum));
    const auto* raw = reinterpret_cast<const unsigned char*>(&header);
    unsigned long long checksum = 0;
    for (size_t i = 0; i < sizeof(Header); ++i) {
      checksum += raw[i];
    }
    std::snprintf(header.chksum, sizeof(header.chksum), "%06llo", checksum);
    header.chksum[6] = '\0';
    header.chksum[7] = ' ';

    return this->sink->write(&header, sizeof(Header));
  }

  bool ArchiveWriter::writeZeroBlock () {
    if (!this->sink) {
      return false;
    }

    unsigned char block[kBlockSize] = {0};
    return this->sink->write(block, sizeof(block));
  }

  bool ArchiveWriter::beginEntry (
    const String& path,
    uint64_t size,
    uint32_t mode,
    uint64_t mtime,
    char type,
    const String& linkpath,
    uint32_t devmajor,
    uint32_t devminor,
    uint64_t sparseSize,
    const Vector<SparseRegion>* sparse
  ) {
    EntryMeta meta {};
    meta.mtime = mtime;
    meta.hasMtime = true;

    return this->beginEntry(
      path,
      size,
      mode,
      type,
      linkpath,
      devmajor,
      devminor,
      sparseSize,
      sparse,
      std::move(meta)
    );
  }

  bool ArchiveWriter::beginEntry (
    const String& path,
    uint64_t size,
    uint32_t mode,
    char type,
    const String& linkpath,
    uint32_t devmajor,
    uint32_t devminor,
    uint64_t sparseSize,
    const Vector<SparseRegion>* sparse,
    EntryMeta meta
  ) {
    if (!this->sink || this->entryOpen) {
      return false;
    }
    if (this->finished) {
      return false;
    }

    const uint32_t uid = meta.hasUid
      ? meta.uid
      : (this->globalMeta.hasUid ? this->globalMeta.uid : 0);

    const uint32_t gid = meta.hasGid
      ? meta.gid
      : (this->globalMeta.hasGid ? this->globalMeta.gid : 0);

    const String uname = meta.hasUname
      ? meta.uname
      : (this->globalMeta.hasUname ? this->globalMeta.uname : String());

    const String gname = meta.hasGname
      ? meta.gname
      : (this->globalMeta.hasGname ? this->globalMeta.gname : String());

    const uint64_t mtime = [&]() -> uint64_t {
      if (meta.hasMtime) {
        return meta.mtime;
      }
      if (this->globalMeta.hasMtime) {
        return this->globalMeta.mtime;
      }
      const auto now = std::time(nullptr);
      return now > 0 ? static_cast<uint64_t>(now) : 0;
    }();

    const bool needsSparsePax = sparse != nullptr;

    if (needsSparsePax) {
      const bool isRegularFile = type == '0' || type == '\0' || type == '7';
      if (!isRegularFile) {
        return false;
      }

      if (!linkpath.empty()) {
        return false;
      }
    }

    // Emit a global PAX header once per archive if global metadata was
    // configured and we haven't written it yet.
    if (this->hasGlobalMeta && !this->globalPaxWritten) {
      String payload;

      auto appendRecord = [&payload](const String& key, const String& value) {
        String body = key + "=" + value + "\n";
        String lenStr = std::to_string(body.size() + 1); // initial guess
        auto len = body.size() + 1 + lenStr.size();
        String finalLenStr = std::to_string(len);
        if (finalLenStr.size() != lenStr.size()) {
          len = body.size() + 1 + finalLenStr.size();
          lenStr = std::to_string(len);
        } else {
          lenStr = finalLenStr;
        }
        payload += lenStr;
        payload += " ";
        payload += body;
      };

      if (this->globalMeta.hasUid) {
        appendRecord("uid", std::to_string(static_cast<unsigned long long>(this->globalMeta.uid)));
      }
      if (this->globalMeta.hasGid) {
        appendRecord("gid", std::to_string(static_cast<unsigned long long>(this->globalMeta.gid)));
      }
      if (this->globalMeta.hasUname) {
        appendRecord("uname", this->globalMeta.uname);
      }
      if (this->globalMeta.hasGname) {
        appendRecord("gname", this->globalMeta.gname);
      }
      if (this->globalMeta.hasMtime) {
        appendRecord("mtime", std::to_string(static_cast<unsigned long long>(this->globalMeta.mtime)));
      }

      if (!payload.empty()) {
        const uint64_t paxSize = static_cast<uint64_t>(payload.size());
        constexpr uint64_t kMaxPaxHeaderSize = 1024 * 1024;
        if (paxSize > kMaxPaxHeaderSize) {
          return false;
        }
        const String globalName = "PaxHeaders.global";

        if (!this->writeHeader(
          globalName,
          String(),
          0,
          0,
          paxSize,
          0,
          this->globalMeta.hasMtime ? this->globalMeta.mtime : 0,
          'g',
          0,
          0,
          String(),
          String()
        )) {
          return false;
        }

        if (!this->sink->write(payload.data(), payload.size())) {
          return false;
        }

        const auto paxPadding = (paxSize % kBlockSize)
          ? (kBlockSize - (paxSize % kBlockSize))
          : 0;

        if (paxPadding > 0) {
          unsigned char block[kBlockSize] = {0};
          if (!this->sink->write(block, static_cast<size_t>(paxPadding))) {
            return false;
          }
        }
      }

      this->globalPaxWritten = true;
    }

    // Determine if we need a per-file PAX header for extended metadata.
    String headerPath = path;
    String name = headerPath;
    String prefix;

    // The ustar header name/prefix field sizes.
    constexpr size_t kNameFieldSize = 100;
    constexpr size_t kPrefixFieldSize = 155;
    constexpr size_t kLinkNameFieldSize = 100;
    constexpr size_t kUnameFieldSize = sizeof(Header{}.uname);
    constexpr size_t kGnameFieldSize = sizeof(Header{}.gname);

    if (name.size() > kNameFieldSize) {
      auto pos = name.rfind('/');
      if (pos != String::npos && pos < name.size() - 1) {
        prefix = name.substr(0, pos);
        name = name.substr(pos + 1);
      }
    }

    const bool needsPaxPath =
      name.size() > kNameFieldSize || prefix.size() > kPrefixFieldSize;

    const bool needsPaxLinkpath =
      !linkpath.empty() && linkpath.size() > kLinkNameFieldSize;

    const bool needsMetaPax =
      (meta.hasUid && this->globalMeta.hasUid && meta.uid != this->globalMeta.uid) ||
      (meta.hasGid && this->globalMeta.hasGid && meta.gid != this->globalMeta.gid) ||
      (meta.hasUname &&
        (meta.uname.size() > kUnameFieldSize ||
          (this->globalMeta.hasUname && meta.uname != this->globalMeta.uname))) ||
      (meta.hasGname &&
        (meta.gname.size() > kGnameFieldSize ||
          (this->globalMeta.hasGname && meta.gname != this->globalMeta.gname))) ||
      (meta.hasMtime && this->globalMeta.hasMtime && meta.mtime != this->globalMeta.mtime);

    const bool needsPax = needsPaxPath || needsPaxLinkpath || needsSparsePax || needsMetaPax;

    if (needsPax) {
      // Build PAX payload with extended metadata.
      String payload;

      auto appendRecord = [&payload](const String& key, const String& value) {
        String body = key + "=" + value + "\n";
        String lenStr = std::to_string(body.size() + 1); // initial guess
        auto len = body.size() + 1 + lenStr.size();
        String finalLenStr = std::to_string(len);
        if (finalLenStr.size() != lenStr.size()) {
          len = body.size() + 1 + finalLenStr.size();
          lenStr = std::to_string(len);
        } else {
          lenStr = finalLenStr;
        }
        payload += lenStr;
        payload += " ";
        payload += body;
      };

      appendRecord("path", path);
      if (!linkpath.empty()) {
        appendRecord("linkpath", linkpath);
      }

      if (meta.hasUid && this->globalMeta.hasUid && meta.uid != this->globalMeta.uid) {
        appendRecord("uid", std::to_string(static_cast<unsigned long long>(meta.uid)));
      }
      if (meta.hasGid && this->globalMeta.hasGid && meta.gid != this->globalMeta.gid) {
        appendRecord("gid", std::to_string(static_cast<unsigned long long>(meta.gid)));
      }
      if (meta.hasUname &&
          (meta.uname.size() > kUnameFieldSize ||
            (this->globalMeta.hasUname && meta.uname != this->globalMeta.uname))) {
        appendRecord("uname", meta.uname);
      }
      if (meta.hasGname &&
          (meta.gname.size() > kGnameFieldSize ||
            (this->globalMeta.hasGname && meta.gname != this->globalMeta.gname))) {
        appendRecord("gname", meta.gname);
      }

      if (needsSparsePax) {
        const auto blocks = sparse ? sparse->size() + 1 : 1;

        appendRecord("GNU.sparse.size", std::to_string(static_cast<unsigned long long>(sparseSize)));
        appendRecord("GNU.sparse.numblocks", std::to_string(static_cast<unsigned long long>(blocks)));

        if (sparse) {
          for (const auto& region : *sparse) {
            appendRecord("GNU.sparse.offset", std::to_string(static_cast<unsigned long long>(region.offset)));
            appendRecord("GNU.sparse.numbytes", std::to_string(static_cast<unsigned long long>(region.length)));
          }
        }

        // Terminator record for GNU sparse formats 0.0/0.1.
        appendRecord("GNU.sparse.offset", std::to_string(static_cast<unsigned long long>(sparseSize)));
        appendRecord("GNU.sparse.numbytes", "0");
      } else {
        // Optional: include size for compatibility.
        appendRecord("size", std::to_string(static_cast<unsigned long long>(size)));
      }

      // Optional: include mtime for compatibility.
      appendRecord("mtime", std::to_string(static_cast<unsigned long long>(mtime)));

      const uint64_t paxSize = static_cast<uint64_t>(payload.size());
      constexpr uint64_t kMaxPaxHeaderSize = 1024 * 1024;
      if (paxSize > kMaxPaxHeaderSize) {
        return false;
      }

      const String paxHeaderPath =
        "PaxHeaders." +
        std::to_string(static_cast<unsigned long long>(this->sink->offset()));

      if (!this->writeHeader(
        paxHeaderPath,
        String(),
        0,
        0,
        paxSize,
        mode,
        mtime,
        'x',
        uid,
        gid,
        uname,
        gname
      )) {
        return false;
      }

      if (!this->sink->write(payload.data(), payload.size())) {
        return false;
      }

      const auto paxPadding = (paxSize % kBlockSize)
        ? (kBlockSize - (paxSize % kBlockSize))
        : 0;

      if (paxPadding > 0) {
        unsigned char block[kBlockSize] = {0};
        if (!this->sink->write(block, static_cast<size_t>(paxPadding))) {
          return false;
        }
      }
    }

    String headerLinkpath = linkpath;
    if (needsPaxLinkpath && headerLinkpath.size() > kLinkNameFieldSize) {
      headerLinkpath = headerLinkpath.substr(headerLinkpath.size() - kLinkNameFieldSize);
    }

    // For the actual file header, fall back to a truncated name if necessary.
    if (needsPaxPath) {
      headerPath = name;
      if (headerPath.size() > kNameFieldSize) {
        headerPath = headerPath.substr(headerPath.size() - kNameFieldSize);
      }
    }

    const uint32_t headerDevmajor = (type == '3' || type == '4') ? devmajor : 0;
    const uint32_t headerDevminor = (type == '3' || type == '4') ? devminor : 0;

    if (!this->writeHeader(
      headerPath,
      headerLinkpath,
      headerDevmajor,
      headerDevminor,
      size,
      mode,
      mtime,
      type,
      uid,
      gid,
      uname,
      gname
    )) {
      return false;
    }

    this->entryOpen = true;
    this->declaredSize = size;
    this->writtenForEntry = 0;
    return true;
  }

  bool ArchiveWriter::writeData (const unsigned char* data, size_t size) {
    if (!this->sink || !this->entryOpen || data == nullptr || size == 0) {
      return false;
    }

    const auto remaining = this->declaredSize - this->writtenForEntry;
    if (size > remaining) {
      size = static_cast<size_t>(remaining);
    }

    if (size == 0) {
      return true;
    }

    if (!this->sink->write(data, size)) {
      return false;
    }

    this->writtenForEntry += size;
    return true;
  }

  bool ArchiveWriter::endEntry () {
    if (!this->sink || !this->entryOpen) {
      return false;
    }

    const auto padding = (this->declaredSize % kBlockSize)
      ? (kBlockSize - (this->declaredSize % kBlockSize))
      : 0;

    if (padding > 0) {
      unsigned char block[kBlockSize] = {0};
      if (!this->sink->write(block, static_cast<size_t>(padding))) {
        return false;
      }
    }

    this->entryOpen = false;
    this->declaredSize = 0;
    this->writtenForEntry = 0;
    return true;
  }

  bool ArchiveWriter::finish () {
    if (!this->sink) {
      return false;
    }

    if (this->finished) {
      return true;
    }

    if (this->entryOpen) {
      if (!this->endEntry()) {
        return false;
      }
    }

    if (!this->writeZeroBlock()) {
      return false;
    }

    if (!this->writeZeroBlock()) {
      return false;
    }

    if (!this->sink->flush()) {
      return false;
    }

    this->finished = true;
    return true;
  }

  MemorySource::MemorySource (
    SharedPointer<unsigned char[]> data,
    uint64_t size
  ) : data(std::move(data)),
      length(size) {
  }

  bool MemorySource::read (
    uint64_t offset,
    void* buffer,
    size_t size,
    size_t& bytesRead
  ) {
    bytesRead = 0;

    if (!this->data || buffer == nullptr) {
      return false;
    }

    if (size == 0) {
      return true;
    }

    if (offset >= this->length) {
      return true;
    }

    const auto remaining = this->length - offset;
    if (size > remaining) {
      size = static_cast<size_t>(remaining);
    }

    if (size == 0) {
      return true;
    }

    std::memcpy(
      buffer,
      this->data.get() + offset,
      size
    );

    bytesRead = size;
    return true;
  }

  uint64_t MemorySource::size () const {
    return this->length;
  }
}
