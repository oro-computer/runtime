#include "tar.hh"
#include "../../crypto.hh"
#include "../../http.hh"

#include <cmath>
#include <filesystem>
#include <limits>

namespace oro::runtime::core::services {
  using oro::runtime::tar::Entry;

  SharedPointer<Tar::Descriptor> Tar::getDescriptor (ID id) const {
    Lock lock(this->mutex);
    auto it = this->descriptors.find(id);
    if (it != this->descriptors.end()) {
      return it->second;
    }
    return nullptr;
  }

  void Tar::removeDescriptor (ID id) {
    Lock lock(this->mutex);
    this->descriptors.erase(id);
  }

  bool Tar::stop () {
    Lock lock(this->mutex);
    this->descriptors.clear();
    return true;
  }

  JSON::Object::Entries Tar::makeError (
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

  JSON::Object::Entries Tar::describeEntry (const Entry& entry) const {
    JSON::Object::Entries json;
    json.emplace("path", entry.path);
    if (!entry.linkpath.empty()) {
      json.emplace("linkpath", entry.linkpath);
    }
    json.emplace("size", static_cast<int64_t>(entry.size));
    json.emplace("mode", static_cast<int64_t>(entry.mode));
    json.emplace("mtime", static_cast<int64_t>(entry.mtime));
    json.emplace("uid", static_cast<int64_t>(entry.uid));
    json.emplace("gid", static_cast<int64_t>(entry.gid));
    if (entry.type == '3' || entry.type == '4') {
      json.emplace("devmajor", static_cast<int64_t>(entry.devmajor));
      json.emplace("devminor", static_cast<int64_t>(entry.devminor));
    }
    if (!entry.uname.empty()) {
      json.emplace("uname", entry.uname);
    }
    if (!entry.gname.empty()) {
      json.emplace("gname", entry.gname);
    }
    String type(1, entry.type);
    json.emplace("type", type);
    json.emplace("isFile", entry.isFile());
    json.emplace("isDirectory", entry.isDirectory());
    if (entry.isSparse()) {
      JSON::Array sparse;
      for (const auto& region : entry.sparse) {
        sparse.push(JSON::Object::Entries {
          {"offset", static_cast<int64_t>(region.offset)},
          {"length", static_cast<int64_t>(region.length)}
        });
      }
      json.emplace("sparse", sparse);
    }
    return json;
  }

  void Tar::open (
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
  ) {
    this->queue.push([=, this]() {
      const auto resolved = filesystem::Resource::resolve(path);
      std::filesystem::path target;
      if (!resolved.empty()) {
        target = resolved;
      } else {
        target = std::filesystem::path(path);
      }

      if (!writable) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(target, ec);
        if (ec) {
          auto json = this->makeError(
            "tar.open",
            "EACCES",
            "Failed to access archive path"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }

        if (!exists) {
          auto json = this->makeError(
            "tar.open",
            "ENOENT",
            "Archive path does not exist"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }
      }

      const auto id = crypto::rand64();
      auto desc = std::make_shared<Descriptor>(
        id,
        resolved.empty() ? path : resolved.string(),
        writable,
        mmapEnabled
      );

      if (writable) {
        auto sink = std::make_unique<tar::FileSink>(desc->path);
        if (!sink->ok()) {
          auto json = this->makeError(
            "tar.open",
            "EACCES",
            "Failed to open archive for writing"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }

        desc->writer = std::make_unique<tar::ArchiveWriter>(std::move(sink));
        if (hasUid || hasGid || hasUname || hasGname || hasMtime) {
          desc->writer->setGlobalMeta(
            uid,
            hasUid,
            gid,
            hasGid,
            mtime,
            hasMtime,
            uname,
            hasUname,
            gname,
            hasGname
          );
        }
      } else {
        auto source = std::make_unique<tar::FileSource>(desc->path, mmapEnabled);
        if (!source->ok()) {
          auto json = this->makeError(
            "tar.open",
            "EACCES",
            "Failed to open archive for reading"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }

        desc->reader = std::make_unique<tar::ArchiveReader>(std::move(source));
        if (!desc->reader->buildIndex()) {
          auto json = this->makeError(
            "tar.open",
            "EINVAL",
            "Failed to parse tar archive"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }
      }

      {
        Lock lock(this->mutex);
        this->descriptors.insert_or_assign(id, desc);
      }

      this->loop.dispatch([=, this]() {
        JSON::Object::Entries data;
        data.emplace("id", std::to_string(desc->id));
        data.emplace("path", desc->path);
        data.emplace("writable", desc->writable);
        data.emplace("mmap", desc->mmapEnabled);

        if (desc->reader) {
          const auto stats = desc->reader->stats();
          data.emplace("size", static_cast<int64_t>(stats.size));
          data.emplace("entryCount", static_cast<int64_t>(stats.entryCount));
        }

        JSON::Object::Entries json {
          {"source", "tar.open"},
          {"data", data}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void Tar::createInMemory (
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
  ) {
    this->queue.push([=, this]() {
      const auto id = crypto::rand64();
      auto desc = std::make_shared<Descriptor>();
      desc->id = id;
      desc->writable = true;
      desc->mmapEnabled = false;
      desc->inMemory = true;

      auto sink = std::make_unique<tar::MemorySink>();
      desc->writer = std::make_unique<tar::ArchiveWriter>(std::move(sink));
      if (hasUid || hasGid || hasUname || hasGname || hasMtime) {
        desc->writer->setGlobalMeta(
          uid,
          hasUid,
          gid,
          hasGid,
          mtime,
          hasMtime,
          uname,
          hasUname,
          gname,
          hasGname
        );
      }

      {
        Lock lock(this->mutex);
        this->descriptors.insert_or_assign(id, desc);
      }

      this->loop.dispatch([=, this]() {
        JSON::Object::Entries data;
        data.emplace("id", std::to_string(desc->id));
        data.emplace("writable", true);
        data.emplace("mmap", false);

        JSON::Object::Entries json {
          {"source", "tar.createBuffer"},
          {"data", data}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void Tar::close (
    const ipc::Message::Seq& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr) {
      auto json = this->makeError(
        "tar.close",
        "ENOTOPEN",
        "No archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->removeDescriptor(id);

    JSON::Object::Entries data {
      {"id", std::to_string(id)}
    };

    JSON::Object::Entries json {
      {"source", "tar.close"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::openFromBuffer (
    const ipc::Message::Seq& seq,
    SharedPointer<unsigned char[]> buffer,
    size_t size,
    const Callback callback
  ) {
    if (buffer == nullptr || size == 0) {
      auto json = this->makeError(
        "tar.openBuffer",
        "EINVAL",
        "Expecting non-empty tar buffer"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      const auto id = crypto::rand64();
      auto desc = std::make_shared<Descriptor>();
      desc->id = id;
      desc->writable = false;
      desc->mmapEnabled = false;
      desc->inMemory = true;
      desc->buffer = buffer;
      desc->bufferSize = static_cast<uint64_t>(size);

      auto source = std::make_unique<tar::MemorySource>(
        buffer,
        static_cast<uint64_t>(size)
      );

      desc->reader = std::make_unique<tar::ArchiveReader>(std::move(source));
      if (!desc->reader->buildIndex()) {
        auto json = this->makeError(
          "tar.openBuffer",
          "EINVAL",
          "Failed to parse tar archive from buffer"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      {
        Lock lock(this->mutex);
        this->descriptors.insert_or_assign(id, desc);
      }

      this->loop.dispatch([=, this]() {
        const auto stats = desc->reader->stats();

        JSON::Object::Entries data;
        data.emplace("id", std::to_string(desc->id));
        data.emplace("writable", false);
        data.emplace("mmap", false);
        data.emplace("size", static_cast<int64_t>(stats.size));
        data.emplace("entryCount", static_cast<int64_t>(stats.entryCount));

        JSON::Object::Entries json {
          {"source", "tar.openBuffer"},
          {"data", data}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void Tar::listEntries (
    const ipc::Message::Seq& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->reader) {
      auto json = this->makeError(
        "tar.list",
        "ENOTOPEN",
        "No readable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    const auto& entries = desc->reader->entries();
    JSON::Array list;
    for (const auto& entry : entries) {
      list.push(this->describeEntry(entry));
    }

    const auto stats = desc->reader->stats();

    JSON::Object::Entries data;
    data.emplace("id", std::to_string(id));
    data.emplace("entries", list);
    data.emplace("size", static_cast<int64_t>(stats.size));
    data.emplace("entryCount", static_cast<int64_t>(stats.entryCount));

    JSON::Object::Entries json {
      {"source", "tar.list"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::statEntry (
    const ipc::Message::Seq& seq,
    ID id,
    const String& path,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->reader) {
      auto json = this->makeError(
        "tar.stat",
        "ENOTOPEN",
        "No readable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    const auto* entry = desc->reader->find(path);
    if (entry == nullptr) {
      auto json = this->makeError(
        "tar.stat",
        "ENOENT",
        "Entry not found in archive"
      );
      return callback(seq, json, QueuedResponse{});
    }

    JSON::Object::Entries data = this->describeEntry(*entry);
    data.emplace("id", std::to_string(id));

    JSON::Object::Entries json {
      {"source", "tar.stat"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::readEntry (
    const ipc::Message::Seq& seq,
    ID id,
    const String& path,
    uint64_t offset,
    uint32_t size,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->reader) {
      auto json = this->makeError(
        "tar.read",
        "ENOTOPEN",
        "No readable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    const auto* entry = desc->reader->find(path);
    if (entry == nullptr) {
      auto json = this->makeError(
        "tar.read",
        "ENOENT",
        "Entry not found in archive"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (entry->isDirectory()) {
      auto json = this->makeError(
        "tar.read",
        "EISDIR",
        "Cannot read body of directory entry"
      );
      return callback(seq, json, QueuedResponse{});
    }

    this->queue.push([=, this]() {
      Vector<uint8_t> buffer;
      if (!desc->reader->read(*entry, offset, size, buffer)) {
        auto json = this->makeError(
          "tar.read",
          "EIO",
          "Failed to read from archive entry"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const auto length = buffer.size();

      this->loop.dispatch([=, this, buffer = std::move(buffer)]() mutable {
        http::Headers headers;
        headers.set("content-type", "application/octet-stream");
        headers.set("content-length", static_cast<uint64_t>(length));

        auto shared = std::make_shared<unsigned char[]>(length);
        if (length > 0) {
          std::memcpy(shared.get(), buffer.data(), length);
        }

        QueuedResponse queuedResponse;
        queuedResponse.id = crypto::rand64();
        queuedResponse.body = shared;
        queuedResponse.length = length;
        queuedResponse.headers = headers.str();

        JSON::Object json;
        callback(seq, json, queuedResponse);
      });
    });
  }

  void Tar::beginWriteEntry (
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
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->writer) {
      auto json = this->makeError(
        "tar.write.begin",
        "ENOTOPEN",
        "No writable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (desc->finalized) {
      auto json = this->makeError(
        "tar.write.begin",
        "EALREADY",
        "Archive has already been finalized"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (desc->entryOpen) {
      auto json = this->makeError(
        "tar.write.begin",
        "EBUSY",
        "Another entry is currently being written"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if ((type == '2' || type == '1') && linkpath.empty()) {
      auto json = this->makeError(
        "tar.write.begin",
        "EINVAL",
        "linkpath must be provided for link entries"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if ((type == '2' || type == '1') && size != 0) {
      auto json = this->makeError(
        "tar.write.begin",
        "EINVAL",
        "Link entries must have size 0"
      );
      return callback(seq, json, QueuedResponse{});
    }

    Vector<tar::SparseRegion> sparseRegions;
    const bool wantsSparse = !sparse.empty();

    if (wantsSparse) {
      constexpr uint64_t kMaxSafeInteger = 9007199254740991ull; // 2^53 - 1
      constexpr size_t kMaxSparseRegions = 16384;

      const bool isRegularFile = type == '0' || type == '\0' || type == '7';
      if (!isRegularFile) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse entries are only supported for regular file types"
        );
        return callback(seq, json, QueuedResponse{});
      }

      if (!linkpath.empty()) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse entries must not include linkpath metadata"
        );
        return callback(seq, json, QueuedResponse{});
      }

      if (sparseSize > kMaxSafeInteger || size > kMaxSafeInteger) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse entry sizes must not exceed 9007199254740991"
        );
        return callback(seq, json, QueuedResponse{});
      }

      JSON::Any parsed;
      try {
        parsed = JSON::parse(sparse);
      } catch (const JSON::Error&) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse map JSON could not be parsed"
        );
        return callback(seq, json, QueuedResponse{});
      }

      if (!parsed.isArray()) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse map must be a JSON array"
        );
        return callback(seq, json, QueuedResponse{});
      }

      const auto& regions = parsed.as<JSON::Array>();

      if (regions.size() > kMaxSparseRegions) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse region map exceeds maximum supported region count"
        );
        return callback(seq, json, QueuedResponse{});
      }

      uint64_t storedSize = 0;
      uint64_t lastEnd = 0;

      for (size_t i = 0; i < regions.size(); ++i) {
        const auto& regionAny = regions.get(static_cast<unsigned int>(i));
        if (!regionAny.isObject()) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse map entries must be objects"
          );
          return callback(seq, json, QueuedResponse{});
        }

        const auto& obj = regionAny.as<JSON::Object>();
        const auto& offsetAny = obj.get("offset");
        const auto& lengthAny = obj.get("length");

        if (!offsetAny.isNumber() || !lengthAny.isNumber()) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse map entries must include numeric offset and length fields"
          );
          return callback(seq, json, QueuedResponse{});
        }

        const double offsetValue = offsetAny.as<JSON::Number>().value();
        const double lengthValue = lengthAny.as<JSON::Number>().value();

        if (!std::isfinite(offsetValue) || offsetValue < 0 || std::floor(offsetValue) != offsetValue ||
            offsetValue > static_cast<double>(kMaxSafeInteger)) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse region offset must be a non-negative integer"
          );
          return callback(seq, json, QueuedResponse{});
        }

        if (!std::isfinite(lengthValue) || lengthValue <= 0 || std::floor(lengthValue) != lengthValue ||
            lengthValue > static_cast<double>(kMaxSafeInteger)) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse region length must be a positive integer"
          );
          return callback(seq, json, QueuedResponse{});
        }

        const uint64_t offset = static_cast<uint64_t>(offsetValue);
        const uint64_t length = static_cast<uint64_t>(lengthValue);

        if (offset < lastEnd) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse regions must be sorted and non-overlapping"
          );
          return callback(seq, json, QueuedResponse{});
        }

        if (offset > sparseSize || length > sparseSize - offset) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse region exceeds declared sparseSize bounds"
          );
          return callback(seq, json, QueuedResponse{});
        }

        if (storedSize > std::numeric_limits<uint64_t>::max() - length) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse region lengths overflow stored size accounting"
          );
          return callback(seq, json, QueuedResponse{});
        }

        storedSize += length;
        if (storedSize > kMaxSafeInteger) {
          auto json = this->makeError(
            "tar.write.begin",
            "EINVAL",
            "Sparse entry stored size exceeds maximum supported safe integer"
          );
          return callback(seq, json, QueuedResponse{});
        }

        tar::SparseRegion region;
        region.offset = offset;
        region.length = length;
        region.dataOffset = 0;
        sparseRegions.push_back(region);

        lastEnd = offset + length;
      }

      if (sparseSize < lastEnd) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "sparseSize must be at least the end of the final sparse region"
        );
        return callback(seq, json, QueuedResponse{});
      }

      if (storedSize != size) {
        auto json = this->makeError(
          "tar.write.begin",
          "EINVAL",
          "Sparse region lengths must sum to the declared entry size"
        );
        return callback(seq, json, QueuedResponse{});
      }
    }

    tar::ArchiveWriter::EntryMeta meta {};
    meta.uid = uid;
    meta.gid = gid;
    meta.uname = uname;
    meta.gname = gname;
    meta.mtime = mtime;
    meta.hasUid = hasUid;
    meta.hasGid = hasGid;
    meta.hasUname = hasUname;
    meta.hasGname = hasGname;
    meta.hasMtime = hasMtime;

    const auto ok = desc->writer->beginEntry(
      path,
      size,
      mode,
      type,
      linkpath,
      devmajor,
      devminor,
      sparseSize,
      wantsSparse ? &sparseRegions : nullptr,
      std::move(meta)
    );
    if (!ok) {
      auto json = this->makeError(
        "tar.write.begin",
        "EIO",
        "Failed to begin tar entry"
      );
      return callback(seq, json, QueuedResponse{});
    }

    desc->entryOpen = true;
    desc->entryRemaining = size;

    JSON::Object::Entries data {
      {"id", std::to_string(id)},
      {"path", path},
      {"size", static_cast<int64_t>(size)}
    };

    JSON::Object::Entries json {
      {"source", "tar.write.begin"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::writeEntryData (
    const ipc::Message::Seq& seq,
    ID id,
    SharedPointer<unsigned char[]> buffer,
    size_t size,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->writer) {
      auto json = this->makeError(
        "tar.write.data",
        "ENOTOPEN",
        "No writable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (desc->finalized) {
      auto json = this->makeError(
        "tar.write.data",
        "EALREADY",
        "Archive has already been finalized"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (!desc->entryOpen) {
      auto json = this->makeError(
        "tar.write.data",
        "EINVAL",
        "No active entry to write to"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (size > desc->entryRemaining) {
      size = static_cast<size_t>(desc->entryRemaining);
    }

    bool ok = true;
    if (size > 0 && buffer != nullptr) {
      ok = desc->writer->writeData(buffer.get(), size);
    }

    if (!ok) {
      auto json = this->makeError(
        "tar.write.data",
        "EIO",
        "Failed to write tar entry data"
      );
      return callback(seq, json, QueuedResponse{});
    }

    desc->entryRemaining -= size;
    bool entryClosed = false;

    if (desc->entryRemaining == 0) {
      entryClosed = desc->writer->endEntry();
      desc->entryOpen = false;
    }

    if (!entryClosed && desc->entryRemaining == 0) {
      auto json = this->makeError(
        "tar.write.data",
        "EIO",
        "Failed to finalize tar entry"
      );
      return callback(seq, json, QueuedResponse{});
    }

    JSON::Object::Entries data {
      {"id", std::to_string(id)},
      {"written", static_cast<int64_t>(size)},
      {"remaining", static_cast<int64_t>(desc->entryRemaining)}
    };

    JSON::Object::Entries json {
      {"source", "tar.write.data"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::finalize (
    const ipc::Message::Seq& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->writer) {
      auto json = this->makeError(
        "tar.finalize",
        "ENOTOPEN",
        "No writable archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (desc->entryOpen) {
      auto json = this->makeError(
        "tar.finalize",
        "EBUSY",
        "An entry is still being written"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (!desc->writer->finish()) {
      auto json = this->makeError(
        "tar.finalize",
        "EIO",
        "Failed to finalize tar archive"
      );
      return callback(seq, json, QueuedResponse{});
    }

    // For in-memory writable archives, capture the finalized buffer and
    // build an index for post-finalize reads.
    if (desc->inMemory && desc->writable) {
      auto* sink = dynamic_cast<tar::MemorySink*>(desc->writer->sinkPtr());
      if (!sink) {
        auto json = this->makeError(
          "tar.finalize",
          "EIO",
          "Failed to access in-memory tar buffer"
        );
        return callback(seq, json, QueuedResponse{});
      }

      const auto& vec = sink->buffer();
      const uint64_t len = static_cast<uint64_t>(vec.size());
      if (len > 0) {
        auto shared = std::make_shared<unsigned char[]>(len);
        std::memcpy(shared.get(), vec.data(), static_cast<size_t>(len));
        desc->buffer = shared;
        desc->bufferSize = len;
      } else {
        desc->buffer.reset();
        desc->bufferSize = 0;
      }

      if (desc->buffer && desc->bufferSize > 0) {
        auto source = std::make_unique<tar::MemorySource>(desc->buffer, desc->bufferSize);
        auto reader = std::make_unique<tar::ArchiveReader>(std::move(source));
        if (!reader->buildIndex()) {
          auto json = this->makeError(
            "tar.finalize",
            "EINVAL",
            "Failed to parse finalized in-memory tar archive"
          );
          return callback(seq, json, QueuedResponse{});
        }
        desc->reader = std::move(reader);
      }
    }

    // Always release the writer after finalizing so file handles are closed
    // (important on Windows, where FileSink is opened without sharing).
    desc->writer.reset();

    // For file-backed writable archives, build a reader index so list/stat/read
    // can be used without reopening the archive.
    if (!desc->inMemory) {
      auto source = std::make_unique<tar::FileSource>(desc->path, desc->mmapEnabled);
      if (!source->ok()) {
        auto json = this->makeError(
          "tar.finalize",
          "EACCES",
          "Failed to open finalized archive for reading"
        );
        return callback(seq, json, QueuedResponse{});
      }

      auto reader = std::make_unique<tar::ArchiveReader>(std::move(source));
      if (!reader->buildIndex()) {
        auto json = this->makeError(
          "tar.finalize",
          "EINVAL",
          "Failed to parse finalized tar archive"
        );
        return callback(seq, json, QueuedResponse{});
      }

      desc->reader = std::move(reader);
    }

    desc->entryOpen = false;
    desc->entryRemaining = 0;
    desc->finalized = true;

    JSON::Object::Entries data {
      {"id", std::to_string(id)}
    };

    JSON::Object::Entries json {
      {"source", "tar.finalize"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse{});
  }

  void Tar::getBuffer (
    const ipc::Message::Seq& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = this->getDescriptor(id);
    if (desc == nullptr || !desc->inMemory) {
      auto json = this->makeError(
        "tar.buffer",
        "ENOTOPEN",
        "No in-memory archive descriptor found with that id"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (desc->writable && !desc->finalized) {
      auto json = this->makeError(
        "tar.buffer",
        "EBUSY",
        "Writable in-memory archive has not been finalized"
      );
      return callback(seq, json, QueuedResponse{});
    }

    if (!desc->buffer) {
      auto json = this->makeError(
        "tar.buffer",
        "EINVAL",
        "In-memory tar buffer is empty"
      );
      return callback(seq, json, QueuedResponse{});
    }

    uint64_t len = desc->bufferSize;
    if (len == 0 && desc->reader) {
      len = desc->reader->stats().size;
      desc->bufferSize = len;
    }

    auto shared = desc->buffer;

    http::Headers headers;
    headers.set("content-type", "application/octet-stream");
    headers.set("content-length", len);

    QueuedResponse queuedResponse;
    queuedResponse.id = crypto::rand64();
    queuedResponse.body = shared;
    queuedResponse.length = len;
    queuedResponse.headers = headers.str();

    JSON::Object json;
    callback(seq, json, queuedResponse);
  }
}
