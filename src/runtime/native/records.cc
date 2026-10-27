#include "records.hh"

#include <sodium.h>
extern "C" {
#if __has_include("sqlite/sqlite3.h")
#include "sqlite/sqlite3.h"
#else
#include <sqlite3.h>
#endif
}

#include <algorithm>
#include <limits>
#include <span>

namespace oro::runtime::native {
  namespace {
    void initialized () {
      static const bool ready = sodium_init() >= 0;
      if (!ready) {
        throw StateError("EIO");
      }
    }

    void sqlCheck (int result) {
      if (result == SQLITE_OK || result == SQLITE_ROW || result == SQLITE_DONE) {
        return;
      }
      const auto code = result & 0xff;
      if (code == SQLITE_BUSY || code == SQLITE_LOCKED) {
        throw StateError("EBUSY");
      }
      if (code == SQLITE_FULL) {
        throw StateError("ENOSPC");
      }
      if (code == SQLITE_CORRUPT || code == SQLITE_NOTADB) {
        throw StateError("EBADMSG");
      }
      throw StateError("EIO");
    }

    class Database {
      public:
      sqlite3 *handle = nullptr;
      Database(const std::filesystem::path &path, bool create) {
        const auto name = path.u8string();
        const int flags =
            SQLITE_OPEN_FULLMUTEX | (create ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY);
        const int result = sqlite3_open_v2(reinterpret_cast<const char *>(name.c_str()), &this->handle, flags, nullptr);
        if (result != SQLITE_OK) {
          sqlite3_close_v2(this->handle);
          this->handle = nullptr;
          sqlCheck(result);
        }
        try {
          sqlCheck(sqlite3_busy_timeout(this->handle, 2000));
          this->exec("PRAGMA trusted_schema=OFF");
          if (create) {
            this->exec("PRAGMA journal_mode=DELETE");
            this->exec("PRAGMA synchronous=FULL");
            this->exec("CREATE TABLE IF NOT EXISTS native_scopes(scope TEXT PRIMARY KEY, revision INTEGER NOT NULL, "
                       "slot TEXT, commit_id TEXT NOT NULL DEFAULT '', envelope BLOB)");
            this->exec(
                "CREATE TABLE IF NOT EXISTS native_meta(id INTEGER PRIMARY KEY CHECK(id=1), identity TEXT NOT NULL)");
          }
        } catch (...) {
          sqlite3_close_v2(this->handle);
          this->handle = nullptr;
          throw;
        }
      }
      ~Database() {
        sqlite3_close_v2(this->handle);
      }
      Database(const Database &) = delete;
      Database &operator=(const Database &) = delete;
      void exec (const char *sql) {
        sqlCheck(sqlite3_exec(this->handle, sql, nullptr, nullptr, nullptr));
      }
    };

    class Transaction {
      public:
      Database &database;
      bool active = true;
      explicit Transaction(Database &database) : database(database) {
        database.exec("BEGIN IMMEDIATE");
      }
      ~Transaction() {
        if (this->active) {
          sqlite3_exec(this->database.handle, "ROLLBACK", nullptr, nullptr, nullptr);
        }
      }
      void commit () {
        if (sqlite3_exec(this->database.handle, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
          throw StateError("EUNKNOWN");
        }
        this->active = false;
      }
    };

    class Statement {
      public:
      sqlite3_stmt *handle = nullptr;
      Statement(Database &database, const char *sql) {
        const auto result = sqlite3_prepare_v2(database.handle, sql, -1, &this->handle, nullptr);
        if (result != SQLITE_OK) {
          sqlite3_finalize(this->handle);
          this->handle = nullptr;
          sqlCheck(result);
        }
      }
      ~Statement() {
        sqlite3_finalize(this->handle);
      }
      void text (int index, const std::string &value) {
        sqlCheck(
            sqlite3_bind_text(this->handle, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
      }
      std::string string (int index) const {
        const auto text = sqlite3_column_text(this->handle, index);
        return text ? std::string(reinterpret_cast<const char *>(text), sqlite3_column_bytes(this->handle, index)) : "";
      }
      int step () {
        const auto result = sqlite3_step(this->handle);
        sqlCheck(result);
        return result;
      }
    };

    void u32 (RecordBytes &result, uint32_t value) {
      for (int shift = 24; shift >= 0; shift -= 8) {
        result.push_back(static_cast<unsigned char>(value >> shift));
      }
    }
    void field (RecordBytes &result, std::span<const unsigned char> value) {
      u32(result, static_cast<uint32_t>(value.size()));
      result.insert(result.end(), value.begin(), value.end());
    }
    void field (RecordBytes &result, const std::string &value) {
      field(result, std::span(reinterpret_cast<const unsigned char *>(value.data()), value.size()));
    }
    class Reader {
      public:
      std::span<const unsigned char> bytes;
      explicit Reader(std::span<const unsigned char> bytes) : bytes(bytes) {}
      uint32_t number () {
        if (this->bytes.size() < 4) {
          throw StateError("EBADMSG");
        }
        uint32_t result = 0;
        for (size_t i = 0; i < 4; ++i) {
          result = (result << 8) | this->bytes[i];
        }
        this->bytes = this->bytes.subspan(4);
        return result;
      }
      RecordBytes field () {
        const auto size = this->number();
        if (size > this->bytes.size()) {
          throw StateError("EBADMSG");
        }
        RecordBytes result(this->bytes.begin(), this->bytes.begin() + size);
        this->bytes = this->bytes.subspan(size);
        return result;
      }
    };

    bool identifier (const std::string &value, size_t maximum = 128) {
      return !value.empty() && value.size() <= maximum && std::all_of(value.begin(), value.end(), [] (unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == ':' || c == '-';
      });
    }
    void userKey (const std::string &key) {
      if (!identifier(key)) {
        throw StateError("EINVAL");
      }
    }
    void validate (const RecordValues &values) {
      size_t count = 0;
      size_t total = 0;
      if (values.size() > RecordStore::maximumRecords + 48) {
        throw StateError("ENOSPC");
      }
      for (const auto &[key, value] : values) {
        if (key.empty() || key.size() > 160 || value.size() > RecordStore::maximumValueBytes) {
          throw StateError("ENOSPC");
        }
        if (key.front() != '@') {
          userKey(key);
          count++;
        }
        total += key.size() + value.size();
        if (total > RecordStore::maximumBytes) {
          throw StateError("ENOSPC");
        }
      }
      if (count > RecordStore::maximumRecords) {
        throw StateError("ENOSPC");
      }
    }
    RecordBytes encode (const RecordValues &values) {
      validate(values);
      RecordBytes result;
      u32(result, 2);
      u32(result, static_cast<uint32_t>(values.size()));
      for (const auto &[key, value] : values) {
        field(result, key);
        field(result, value);
      }
      return result;
    }
    RecordValues decode (const RecordBytes &bytes) {
      Reader reader(bytes);
      if (reader.number() != 2) {
        throw StateError("EBADMSG");
      }
      const auto count = reader.number();
      if (count > RecordStore::maximumRecords + 48) {
        throw StateError("EBADMSG");
      }
      RecordValues result;
      for (size_t i = 0; i < count; ++i) {
        const auto keyBytes = reader.field();
        const std::string key(keyBytes.begin(), keyBytes.end());
        if (!result.emplace(key, reader.field()).second) {
          throw StateError("EBADMSG");
        }
      }
      if (!reader.bytes.empty()) {
        throw StateError("EBADMSG");
      }
      try {
        validate(result);
      } catch (const StateError &) {
        throw StateError("EBADMSG");
      }
      return result;
    }
    void wipe (RecordValues &values) {
      for (auto &[key, value] : values) {
        sodium_memzero(value.data(), value.size());
      }
    }
    struct ValuesWiper {
      RecordValues &values;
      ~ValuesWiper() {
        wipe(this->values);
      }
    };
    struct KeyWiper {
      std::optional<RecordKey> &key;
      ~KeyWiper() {
        if (this->key) {
          sodium_memzero(this->key->data(), this->key->size());
        }
      }
    };
    struct BytesWiper {
      RecordBytes &bytes;
      ~BytesWiper() {
        sodium_memzero(this->bytes.data(), this->bytes.size());
      }
    };
    std::string hex (std::span<const unsigned char> bytes) {
      std::string result(bytes.size() * 2 + 1, 0);
      sodium_bin2hex(result.data(), result.size(), bytes.data(), bytes.size());
      result.pop_back();
      return result;
    }
    std::string randomIdentity () {
      initialized();
      std::array<unsigned char, 16> bytes;
      randombytes_buf(bytes.data(), bytes.size());
      return hex(bytes);
    }
    const std::string &processIdentity () {
      static const std::string identity = randomIdentity();
      return identity;
    }
    struct Row {
      bool exists = false;
      int64_t revision = 0;
      char slot = 0;
      std::string commitId;
      RecordBytes envelope;
    };
    Row row (Database &database, const std::string &scope) {
      Statement query(database, "SELECT revision,slot,commit_id,envelope FROM native_scopes WHERE scope=?");
      query.text(1, scope);
      if (query.step() == SQLITE_DONE) {
        return {};
      }
      Row result;
      result.exists = true;
      result.revision = sqlite3_column_int64(query.handle, 0);
      const auto slot = query.string(1);
      result.slot = slot.size() == 1 ? slot.front() : 0;
      result.commitId = query.string(2);
      const auto size = sqlite3_column_bytes(query.handle, 3);
      const auto bytes = static_cast<const unsigned char *>(sqlite3_column_blob(query.handle, 3));
      if (size < 0 || size > static_cast<int>(RecordStore::maximumBytes + 8192)) {
        throw StateError("EBADMSG");
      }
      if (bytes && size) {
        result.envelope.assign(bytes, bytes + size);
      }
      if (result.revision < 0 || (result.revision > 0 && (result.slot != 'a' && result.slot != 'b'))) {
        throw StateError("EBADMSG");
      }
      if (result.revision == 0 && (!slot.empty() || !result.envelope.empty() || !result.commitId.empty())) {
        throw StateError("EBADMSG");
      }
      if (result.commitId.size() > 128) {
        throw StateError("EBADMSG");
      }
      return result;
    }
    RecordBytes associated (const std::string &scope, const Row &row) {
      RecordBytes result;
      field(result, std::string("oro.native.records.v2"));
      field(result, scope);
      field(result, std::to_string(row.revision));
      field(result, std::string(1, row.slot));
      field(result, row.commitId);
      return result;
    }
    RecordValues decrypt (const std::string &scope, const Row &row, RecordKeyStore &keys) {
      if (!row.revision) {
        return {};
      }
      if (row.envelope.size() < 40) {
        throw StateError("EBADMSG");
      }
      auto key = keys.get(scope, row.slot);
      KeyWiper wipeKey{key};
      if (!key) {
        throw StateError("EKEYREVOKED");
      }
      const auto aad = associated(scope, row);
      RecordBytes plaintext(row.envelope.size());
      BytesWiper wipePlaintext{plaintext};
      unsigned long long size = 0;
      if (crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &size, nullptr, row.envelope.data() + 24,
                                                     row.envelope.size() - 24, aad.data(), aad.size(),
                                                     row.envelope.data(), key->data()) != 0) {
        throw StateError("EBADMSG");
      }
      plaintext.resize(size);
      return decode(plaintext);
    }
    RecordSnapshot publicSnapshot (RecordSnapshot value) {
      for (auto i = value.records.begin(); i != value.records.end();) {
        if (i->first.starts_with('@')) {
          sodium_memzero(i->second.data(), i->second.size());
          i = value.records.erase(i);
        } else {
          ++i;
        }
      }
      return value;
    }
    void changes (RecordValues &values, const RecordChanges &changes) {
      for (const auto &[key, value] : changes) {
        userKey(key);
        if (const auto previous = values.find(key); previous != values.end()) {
          sodium_memzero(previous->second.data(), previous->second.size());
        }
        if (value && value->size() > RecordStore::maximumValueBytes) {
          throw StateError("ENOSPC");
        }
        if (value) {
          values[key] = *value;
        } else {
          values.erase(key);
        }
      }
      validate(values);
    }
  } // namespace

  RecordStore::RecordStore(const std::filesystem::path &database, std::shared_ptr<RecordKeyStore> keys,
                           const std::string &name, const RecordIdentity &identity)
      : database(database), keys(std::move(keys)) {
    initialized();
    if (!this->keys || database.empty() || name.empty() || name.size() > 64 || name.front() < 'a' ||
        name.front() > 'z' || name.starts_with("oro.") || !std::all_of(name.begin(), name.end(), [] (unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        })) {
      throw StateError("EINVAL");
    }
    const auto directory = database.parent_path();
    if (!directory.empty() && !std::filesystem::exists(directory)) {
      std::filesystem::create_directories(directory);
      std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    }
    Database connection(database, true);
    Transaction initialization(connection);
    std::string databaseIdentity;
    {
      Statement identity(connection, "SELECT identity FROM native_meta WHERE id=1");
      if (identity.step() == SQLITE_ROW) {
        databaseIdentity = identity.string(0);
      }
    }
    if (databaseIdentity.empty()) {
      Statement count(connection, "SELECT COUNT(*) FROM native_scopes");
      count.step();
      if (sqlite3_column_int64(count.handle, 0) != 0) {
        throw StateError("EBADMSG");
      }
      databaseIdentity = randomIdentity();
      Statement insert(connection, "INSERT INTO native_meta(id,identity) VALUES(1,?)");
      insert.text(1, databaseIdentity);
      insert.step();
    }
    if (databaseIdentity.size() != 32 ||
        !std::all_of(databaseIdentity.begin(), databaseIdentity.end(),
                     [] (unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
      throw StateError("EBADMSG");
    }
    initialization.commit();
    RecordBytes binding;
    field(binding, databaseIdentity);
    field(binding, std::string("oro.native.records.v2"));
    field(binding, name);
    for (const auto &value : {identity.authority, identity.account, identity.identity}) {
      if (value.empty() || value.size() > 256 ||
          std::any_of(value.begin(), value.end(), [] (unsigned char c) { return c < 32 || c == 127; })) {
        throw StateError("EINVAL");
      }
      field(binding, value);
    }
    std::array<unsigned char, 32> digest;
    crypto_generichash(digest.data(), digest.size(), binding.data(), binding.size(), nullptr, 0);
    this->scopeId = hex(digest);
  }

  RecordSnapshot RecordStore::inspect() const {
    if (!std::filesystem::exists(this->database)) {
      return {};
    }
    Database database(this->database, false);
    const auto current = row(database, this->scopeId);
    return {current.revision, current.commitId, decrypt(this->scopeId, current, *this->keys)};
  }

  RecordSnapshot RecordStore::read() const {
    return publicSnapshot(this->inspect());
  }

  RecordBytes RecordStore::opaqueArchive () const {
    Database database(this->database, false);
    Statement identity(database, "SELECT identity FROM native_meta WHERE id=1");
    if (identity.step() != SQLITE_ROW) throw StateError("EBADMSG");
    RecordBytes archive;
    u32(archive, 1);
    field(archive, identity.string(0));
    field(archive, this->scopeId);
    Statement query(database, "SELECT revision,slot,commit_id,envelope FROM native_scopes WHERE scope=?");
    query.text(1, this->scopeId);
    if (query.step() == SQLITE_ROW) {
      field(archive, std::to_string(sqlite3_column_int64(query.handle, 0)));
      for (int index : {1, 2, 3}) {
        const auto length = sqlite3_column_bytes(query.handle, index);
        const auto data = static_cast<const unsigned char*>(sqlite3_column_blob(query.handle, index));
        if (length < 0 || length > 524288) throw StateError("ENOSPC");
        field(archive, std::span<const unsigned char>(data, static_cast<size_t>(length)));
      }
    }
    return archive;
  }

  RecordStatus RecordStore::status() const {
    if (!std::filesystem::exists(this->database)) {
      return {};
    }
    Database database(this->database, false);
    const auto current = row(database, this->scopeId);
    auto key = current.revision ? this->keys->get(this->scopeId, current.slot) : std::optional<RecordKey>();
    KeyWiper wipeKey{key};
    return {current.revision, key.has_value(), current.revision > 0 && !key};
  }

  RecordSnapshot RecordStore::mutate(int64_t expectedRevision, const std::function<bool(RecordValues &)> &action,
                                     const std::string &commitId, bool reset) {
    if (expectedRevision < 0 || commitId.size() > 128) {
      throw StateError("EINVAL");
    }
    const auto directory = this->database.parent_path();
    if (!directory.empty() && !std::filesystem::exists(directory)) {
      std::filesystem::create_directories(directory);
      std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    }
    Database database(this->database, true);
    {
      // Persist a bounded reservation before creating an OS key. An interrupted
      // initial write can retain one key, but cannot allocate unbounded aliases.
      Transaction reserve(database);
      if (!row(database, this->scopeId).exists) {
        Statement count(database, "SELECT COUNT(*) FROM native_scopes");
        count.step();
        if (sqlite3_column_int64(count.handle, 0) >= static_cast<int64_t>(maximumScopes)) {
          throw StateError("ENOSPC");
        }
        Statement insert(database, "INSERT INTO native_scopes(scope,revision) VALUES(?,0)");
        insert.text(1, this->scopeId);
        insert.step();
      }
      reserve.commit();
    }
    Transaction transaction(database);
    auto current = row(database, this->scopeId);
    if (current.revision != expectedRevision) {
      throw StateError("ESTALE");
    }
    if (current.revision == std::numeric_limits<int64_t>::max()) {
      throw StateError("ENOSPC");
    }
    auto values = reset ? RecordValues() : decrypt(this->scopeId, current, *this->keys);
    ValuesWiper wipeValues{values};
    if (!action(values)) {
      return publicSnapshot({current.revision, current.commitId, values});
    }
    auto encoded = encode(values);
    BytesWiper wipeEncoded{encoded};
    Row updated;
    updated.revision = current.revision + 1;
    updated.commitId = commitId;
    updated.slot = reset && current.slot == 'a' ? 'b' : reset || !current.slot ? 'a' : current.slot;
    std::optional<RecordKey> key;
    KeyWiper wipeKey{key};
    if (!reset) {
      key = this->keys->get(this->scopeId, updated.slot);
    }
    if (!key) {
      if (current.revision && !reset) {
        throw StateError("EKEYREVOKED");
      }
      // The selected slot is never the current ciphertext key during reset.
      this->keys->remove(this->scopeId, updated.slot);
      key.emplace();
      randombytes_buf(key->data(), key->size());
      this->keys->put(this->scopeId, updated.slot, *key);
    }
    const auto aad = associated(this->scopeId, updated);
    updated.envelope.resize(24 + encoded.size() + 16);
    randombytes_buf(updated.envelope.data(), 24);
    unsigned long long length = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(updated.envelope.data() + 24, &length, encoded.data(),
                                                   encoded.size(), aad.data(), aad.size(), nullptr,
                                                   updated.envelope.data(), key->data()) != 0) {
      throw StateError("EIO");
    }
    updated.envelope.resize(24 + length);
    Statement update(database,
                     "UPDATE native_scopes SET revision=?,slot=?,commit_id=?,envelope=? WHERE scope=? AND revision=?");
    sqlCheck(sqlite3_bind_int64(update.handle, 1, updated.revision));
    update.text(2, std::string(1, updated.slot));
    update.text(3, updated.commitId);
    sqlCheck(sqlite3_bind_blob(update.handle, 4, updated.envelope.data(), static_cast<int>(updated.envelope.size()),
                               SQLITE_TRANSIENT));
    update.text(5, this->scopeId);
    sqlCheck(sqlite3_bind_int64(update.handle, 6, expectedRevision));
    update.step();
    if (sqlite3_changes(database.handle) != 1) {
      throw StateError("ESTALE");
    }
    transaction.commit();
    return publicSnapshot({updated.revision, updated.commitId, values});
  }

  RecordSnapshot RecordStore::apply(int64_t revision, const RecordChanges &update, const std::string &commitId) {
    return this->mutate(
        revision,
        [&update] (RecordValues &values) {
          changes(values, update);
          return true;
        },
        commitId);
  }

  RecordSnapshot RecordStore::reset(int64_t revision) {
    return this->mutate(
        revision,
        [] (RecordValues &values) {
          wipe(values);
          values.clear();
          return true;
        },
        "", true);
  }

  void RecordStore::collectRetiredKey() {
    if (!std::filesystem::exists(this->database)) {
      return;
    }
    Database database(this->database, true);
    Transaction transaction(database);
    const auto current = row(database, this->scopeId);
    for (const auto slot : {'a', 'b'}) {
      if (current.slot != slot) {
        this->keys->remove(this->scopeId, slot);
      }
    }
    transaction.commit();
  }

  RecordSnapshot RecordStore::migrate(const std::string &id, const std::string &digest, int64_t revision,
                                      const RecordValues &imported) {
    if (!identifier(id) || digest.size() != 64 || !std::all_of(digest.begin(), digest.end(), [] (unsigned char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
      throw StateError("EINVAL");
    }
    validate(imported);
    for (const auto &[key, value] : imported) {
      userKey(key);
    }
    return this->mutate(revision, [&] (RecordValues &values) {
      const auto marker = "@migration:" + id;
      const RecordBytes source(digest.begin(), digest.end());
      if (const auto previous = values.find(marker); previous != values.end()) {
        if (previous->second != source) {
          throw StateError("EEXIST");
        }
        return false;
      }
      size_t count = 0;
      for (const auto &[key, value] : values) {
        if (key.starts_with("@migration:")) {
          count++;
        }
      }
      if (count >= 32) {
        throw StateError("ENOSPC");
      }
      for (const auto &[key, value] : imported) {
        if (values.contains(key)) {
          throw StateError("EEXIST");
        }
        values[key] = value;
      }
      values[marker] = source;
      return true;
    });
  }

  namespace {
    const std::string prefix = "@coordination:";
    RecordBytes bytes (const std::string &value) {
      return {value.begin(), value.end()};
    }
    std::string string (const RecordValues &values, const std::string &key) {
      const auto found = values.find(key);
      if (found == values.end()) {
        throw StateError("EBADMSG");
      }
      return {found->second.begin(), found->second.end()};
    }
    RecordValues coordination (const RecordValues &values, const std::string &slot) {
      const auto found = values.find(prefix + slot);
      if (found == values.end()) {
        throw StateError("ENOENT");
      }
      return decode(found->second);
    }
    CoordinationState state (const RecordStore &store, const std::string &slot, int64_t revision,
                             const RecordValues &record) {
      CoordinationState result;
      result.ticket = {store.scope(), slot, string(record, "generation"), revision};
      result.phase = string(record, "phase");
      result.operationRef = string(record, "operation");
      result.ownerProcessMatches = string(record, "owner") == processIdentity();
      if (!identifier(result.ticket.generation) || !identifier(result.operationRef) ||
          (result.phase != "prepared" && result.phase != "executing" && result.phase != "unknown" &&
           result.phase != "committed" && result.phase != "aborted")) {
        throw StateError("EBADMSG");
      }
      if (const auto receipt = record.find("receipt"); receipt != record.end()) {
        result.receipt = receipt->second;
      }
      return result;
    }
    void validTicket (const RecordStore &store, const CoordinationTicket &ticket) {
      if (ticket.scope != store.scope() || !identifier(ticket.slot) || !identifier(ticket.generation)) {
        throw StateError("ESTALE");
      }
    }
  } // namespace

  std::optional<CoordinationState> RecordCoordinator::get(const std::string &slot) const {
    if (!identifier(slot)) {
      throw StateError("EINVAL");
    }
    auto snapshot = this->store.inspect();
    ValuesWiper wipeValues{snapshot.records};
    if (!snapshot.records.contains(prefix + slot)) {
      return std::nullopt;
    }
    return state(this->store, slot, snapshot.revision, coordination(snapshot.records, slot));
  }

  CoordinationState RecordCoordinator::prepare(const std::string &slot, const std::string &operation,
                                               int64_t revision) {
    if (!identifier(slot) || !identifier(operation)) {
      throw StateError("EINVAL");
    }
    RecordValues record{{"generation", bytes(randomIdentity())},
                        {"phase", bytes("prepared")},
                        {"operation", bytes(operation)},
                        {"owner", bytes(processIdentity())}};
    auto snapshot = this->store.mutate(revision, [&] (RecordValues &values) {
      if (values.contains(prefix + slot)) {
        throw StateError("EEXIST");
      }
      size_t count = 0;
      for (const auto &[key, value] : values) {
        if (key.starts_with(prefix)) {
          count++;
        }
      }
      if (count >= 16) {
        throw StateError("ENOSPC");
      }
      values[prefix + slot] = encode(record);
      return true;
    });
    return state(this->store, slot, snapshot.revision, record);
  }

  CoordinationState RecordCoordinator::transition(const CoordinationTicket &ticket,
                                                  const std::vector<std::string> &allowed, const std::string &phase,
                                                  const RecordChanges &update, const RecordBytes &receipt) {
    validTicket(this->store, ticket);
    if (receipt.size() > RecordStore::maximumValueBytes) {
      throw StateError("ENOSPC");
    }
    RecordValues record;
    auto snapshot = this->store.mutate(ticket.revision, [&] (RecordValues &values) {
      record = coordination(values, ticket.slot);
      if (string(record, "generation") != ticket.generation) {
        throw StateError("ESTALE");
      }
      const auto current = string(record, "phase");
      if (std::find(allowed.begin(), allowed.end(), current) == allowed.end()) {
        throw StateError("EINVAL");
      }
      changes(values, update);
      record["phase"] = bytes(phase);
      record["owner"] = bytes(processIdentity());
      if (!receipt.empty()) {
        record["receipt"] = receipt;
      }
      values[prefix + ticket.slot] = encode(record);
      return true;
    });
    return state(this->store, ticket.slot, snapshot.revision, record);
  }

  CoordinationState RecordCoordinator::begin(const CoordinationTicket &ticket) {
    return this->transition(ticket, {"prepared"}, "executing");
  }
  CoordinationState RecordCoordinator::unknown(const CoordinationTicket &ticket) {
    return this->transition(ticket, {"executing", "unknown"}, "unknown");
  }
  CoordinationState RecordCoordinator::abort(const CoordinationTicket &ticket) {
    return this->transition(ticket, {"prepared"}, "aborted");
  }
  CoordinationState RecordCoordinator::commit(const CoordinationTicket &ticket, const RecordChanges &values,
                                              const RecordBytes &receipt) {
    return this->transition(ticket, {"executing"}, "committed", values, receipt);
  }
  CoordinationState RecordCoordinator::resolve(const CoordinationTicket &ticket, bool applied,
                                               const RecordChanges &values, const RecordBytes &receipt) {
    if (!applied && (!values.empty() || !receipt.empty())) {
      throw StateError("EINVAL");
    }
    return this->transition(ticket, {"unknown"}, applied ? "committed" : "aborted", values, receipt);
  }
  CoordinationState RecordCoordinator::recover(const CoordinationTicket &ticket) {
    return this->unknown(ticket);
  }
  RecordSnapshot RecordCoordinator::release(const CoordinationTicket &ticket) {
    validTicket(this->store, ticket);
    return this->store.mutate(ticket.revision, [&] (RecordValues &values) {
      const auto record = coordination(values, ticket.slot);
      if (string(record, "generation") != ticket.generation) {
        throw StateError("ESTALE");
      }
      const auto phase = string(record, "phase");
      if (phase != "committed" && phase != "aborted") {
        throw StateError("EBUSY");
      }
      values.erase(prefix + ticket.slot);
      return true;
    });
  }
} // namespace oro::runtime::native
