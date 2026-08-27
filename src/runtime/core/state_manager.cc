#include "state_manager.hh"

#include "../filesystem.hh"
#include "../debug.hh"
#include "../env.hh"
#include "../url.hh"
#include "../crypto.hh"
#include "../string.hh"

#include <chrono>
#include <limits>
#include <system_error>

namespace oro::runtime::core {
  namespace {
    constexpr const char* kCreateTable =
      "CREATE TABLE IF NOT EXISTS state_entries ("
      "  key TEXT PRIMARY KEY,"
      "  value BLOB,"
      "  updated_at INTEGER NOT NULL"
      ");";

    constexpr const char* kCreateMetaTable =
      "CREATE TABLE IF NOT EXISTS state_meta ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ");";
  }

  StateManager::StateManager (context::RuntimeContext& context)
    : context(context)
  {}

  StateManager::~StateManager () {
    this->shutdown();
  }

  bool StateManager::init () {
    Lock lock(this->mutex);
    if (this->ready.load()) {
      return true;
    }

    if (!this->initializeLocked()) {
      debug("StateManager: initializeLocked() failed during init()");
      return false;
    }

    return true;
  }

  void StateManager::shutdown () {
    Lock lock(this->mutex);
    for (auto& tuple : this->scopedDatabases) {
      if (tuple.second.database.isOpen()) {
        tuple.second.database.close();
      }
    }
    this->scopedDatabases.clear();
    this->ready = false;
    this->stateDirectory.clear();
  }

  bool StateManager::isReady () const {
    return this->ready.load();
  }

  bool StateManager::normalizeScope (
    const String& scope,
    String& normalized
  ) const {
    if (scope.empty()) {
      debug("StateManager: normalizeScope rejected empty scope");
      return false;
    }

    url::URL parsed(scope, true);

    if (parsed.origin.empty() || parsed.scheme.empty() || parsed.hostname.empty()) {
      debug(
        "StateManager: normalizeScope rejected scope '%s' (missing origin components)",
        scope.c_str()
      );
      return false;
    }

    if (!parsed.search.empty() || !parsed.hash.empty()) {
      debug(
        "StateManager: normalizeScope rejected scope '%s' (query or hash not allowed)",
        scope.c_str()
      );
      return false;
    }

    if (!parsed.pathname.empty() && parsed.pathname != "/") {
      debug(
        "StateManager: normalizeScope rejected scope '%s' (pathname must be '/' when present)",
        scope.c_str()
      );
      return false;
    }

    normalized = parsed.origin;
    return true;
  }

  StateManager::ScopedDatabase* StateManager::ensureDatabaseForScope (
    const String& normalizedScope
  ) {
    auto existing = this->scopedDatabases.find(normalizedScope);
    if (existing != this->scopedDatabases.end()) {
      if (!existing->second.database.isOpen()) {
        if (!existing->second.database.open(existing->second.path.string())) {
          debug(
            "StateManager: failed to reopen database for %s",
            normalizedScope.c_str()
          );
          return nullptr;
        }

        existing->second.database.exec("PRAGMA journal_mode=WAL;");
        existing->second.database.exec("PRAGMA synchronous=NORMAL;");
        existing->second.database.exec("PRAGMA temp_store=MEMORY;");
        existing->second.database.exec("PRAGMA foreign_keys=ON;");
      }

      return &existing->second;
    }

    const auto hash = oro::runtime::string::toLowerCase(
      oro::runtime::crypto::sha1(normalizedScope)
    );
    const Path dbPath = this->stateDirectory / (hash + ".db");

    ScopedDatabase entry;
    entry.path = dbPath;

    if (!entry.database.open(dbPath.string())) {
      debug(
        "StateManager: failed to open sqlite database %s for origin %s (code=%d, message=%s)",
        dbPath.string().c_str(),
        normalizedScope.c_str(),
        entry.database.lastErrorCode(),
        entry.database.lastErrorMessage().c_str()
      );
      return nullptr;
    }

    entry.database.exec("PRAGMA journal_mode=WAL;");
    entry.database.exec("PRAGMA synchronous=NORMAL;");
    entry.database.exec("PRAGMA temp_store=MEMORY;");
    entry.database.exec("PRAGMA foreign_keys=ON;");

    if (!this->runMigrationsForDatabase(entry.database, normalizedScope)) {
      entry.database.close();
      return nullptr;
    }

    entry.initialized = true;
    auto [iterator, inserted] = this->scopedDatabases.emplace(
      normalizedScope,
      std::move(entry)
    );

    if (!inserted) {
      return nullptr;
    }

    return &iterator->second;
  }

  bool StateManager::upsertValue (
    ScopedDatabase* scopedDatabase,
    const String& key,
    const bytes::Buffer& value,
    uint64_t timestamp
  ) {
    if (scopedDatabase == nullptr || !scopedDatabase->database.isOpen()) {
      return false;
    }

    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
      debug(
        "StateManager: value too large to store (%zu bytes) for key %s",
        value.size(),
        key.c_str()
      );
      return false;
    }

    sqlite::Statement statement;
    const String sql =
      "INSERT INTO state_entries(key, value, updated_at) "
      "VALUES(?, ?, ?) "
      "ON CONFLICT(key) DO UPDATE SET "
      "value=excluded.value, updated_at=excluded.updated_at;";

    if (!statement.prepare(scopedDatabase->database, sql)) {
      debug(
        "StateManager: failed to prepare upsert statement (code=%d, message=%s)",
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    if (!statement.bindText(1, key)) {
      debug(
        "StateManager: failed to bind key parameter for upsert (key=%s, database=%s, code=%d, message=%s)",
        key.c_str(),
        scopedDatabase->path.string().c_str(),
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    const auto size = static_cast<int>(value.size());
    const void* blob = size > 0 ? static_cast<const void*>(value.data()) : nullptr;

    if (!statement.bindBlob(2, blob, size)) {
      debug(
        "StateManager: failed to bind blob parameter for upsert (key=%s, database=%s, bytes=%d, code=%d, message=%s)",
        key.c_str(),
        scopedDatabase->path.string().c_str(),
        size,
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    if (!statement.bindInt64(3, static_cast<int64_t>(timestamp))) {
      debug(
        "StateManager: failed to bind timestamp parameter for upsert (key=%s, database=%s, timestamp=%llu, code=%d, message=%s)",
        key.c_str(),
        scopedDatabase->path.string().c_str(),
        static_cast<unsigned long long>(timestamp),
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    const auto stepResult = statement.step();
    if (stepResult != SQLITE_DONE) {
      debug(
        "StateManager: failed to execute upsert (key=%s, database=%s, sqlite_result=%d, code=%d, message=%s)",
        key.c_str(),
        scopedDatabase->path.string().c_str(),
        stepResult,
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    return true;
  }

  bool StateManager::initializeLocked () {
    Path stateDir;
    auto overrideDir = env::get("ORO_STATE_DIR");
    if (overrideDir.empty()) {
      overrideDir = env::get("ORO_RUNTIME_STATE_DIR");
    }

    if (!overrideDir.empty()) {
      stateDir = Path(overrideDir);
    } else {
      auto wellKnown = filesystem::Resource::getWellKnownPaths();
      Path baseDir = wellKnown.data;

      if (baseDir.empty()) {
        baseDir = wellKnown.config;
      }

      if (baseDir.empty()) {
        baseDir = filesystem::Resource::getResourcesPath();
      }

      if (baseDir.empty()) {
        baseDir = Path(".");
      }

      stateDir = baseDir / "state";
    }

    std::error_code ec;
    oro::fs::create_directories(stateDir, ec);
    if (ec) {
      debug(
        "StateManager: failed to create state directory %s (%s)",
        stateDir.string().c_str(),
        ec.message().c_str()
      );
      return false;
    }

    this->stateDirectory = stateDir;
    if (!overrideDir.empty()) {
      debug(
        "StateManager: using state directory %s (set via ORO_STATE_DIR or deprecated ORO_RUNTIME_STATE_DIR)",
        this->stateDirectory.string().c_str()
      );
    } else {
      debug(
        "StateManager: using state directory %s",
        this->stateDirectory.string().c_str()
      );
    }

    this->ready = true;
    return true;
  }

  bool StateManager::ensureInitializedLocked () {
    if (this->ready.load()) {
      return true;
    }

    return this->initializeLocked();
  }

  bool StateManager::runMigrationsForDatabase (
    sqlite::Database& db,
    const String& origin
  ) {
    if (!db.exec(kCreateTable)) {
      debug(
        "StateManager: failed to create state_entries table (code=%d, message=%s)",
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }

    if (!db.exec(kCreateMetaTable)) {
      debug(
        "StateManager: failed to create state_meta table (code=%d, message=%s)",
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }

    sqlite::Statement selectMeta(db, "SELECT value FROM state_meta WHERE key = 'origin' LIMIT 1;");
    if (selectMeta.handle() == nullptr) {
      debug(
        "StateManager: failed to prepare meta select (code=%d, message=%s)",
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }

    if (selectMeta.step() == SQLITE_ROW) {
      const auto storedOrigin = selectMeta.columnText(0);
      if (storedOrigin != origin) {
        debug(
          "StateManager: origin mismatch for database, expected %s but found %s",
          origin.c_str(),
          storedOrigin.c_str()
        );
        return false;
      }

      return true;
    }

    sqlite::Statement insertMeta;
    if (!insertMeta.prepare(db, "INSERT OR REPLACE INTO state_meta(key, value) VALUES('origin', ?);")) {
      debug(
        "StateManager: failed to prepare meta insert (code=%d, message=%s)",
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }

    if (!insertMeta.bindText(1, origin)) {
      debug(
        "StateManager: failed to bind origin metadata (origin=%s, code=%d, message=%s)",
        origin.c_str(),
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }

    const auto result = insertMeta.step();
    if (result != SQLITE_DONE) {
      debug(
        "StateManager: failed to persist origin metadata (origin=%s, sqlite_result=%d, code=%d, message=%s)",
        origin.c_str(),
        result,
        db.lastErrorCode(),
        db.lastErrorMessage().c_str()
      );
      return false;
    }
    return true;
  }

  uint64_t StateManager::nowMilliseconds () const {
    using namespace std::chrono;
    const auto now = time_point_cast<milliseconds>(system_clock::now());
    return static_cast<uint64_t>(now.time_since_epoch().count());
  }

  bool StateManager::put (
    const String& scope,
    const String& key,
    const bytes::Buffer& value
  ) {
    Lock lock(this->mutex);
    if (!this->ensureInitializedLocked()) {
      return false;
    }

    String normalizedScope;
    if (!this->normalizeScope(scope, normalizedScope)) {
      return false;
    }

    auto* scopedDatabase = this->ensureDatabaseForScope(normalizedScope);
    if (scopedDatabase == nullptr) {
      return false;
    }

    return this->upsertValue(
      scopedDatabase,
      key,
      value,
      this->nowMilliseconds()
    );
  }

  bool StateManager::putString (
    const String& scope,
    const String& key,
    const String& value
  ) {
    return this->put(scope, key, bytes::Buffer::from(value));
  }

  std::optional<StateManager::Entry> StateManager::get (
    const String& scope,
    const String& key
  ) {
    Lock lock(this->mutex);
    if (!this->ensureInitializedLocked()) {
      return std::nullopt;
    }

    String normalizedScope;
    if (!this->normalizeScope(scope, normalizedScope)) {
      return std::nullopt;
    }

    auto* scopedDatabase = this->ensureDatabaseForScope(normalizedScope);
    if (scopedDatabase == nullptr) {
      return std::nullopt;
    }

    sqlite::Statement statement(
      scopedDatabase->database,
      "SELECT value, updated_at FROM state_entries WHERE key = ? LIMIT 1;"
    );

    if (statement.handle() == nullptr) {
      debug(
        "StateManager: failed to prepare select statement (code=%d, message=%s)",
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return std::nullopt;
    }

    if (!statement.bindText(1, key)) {
      return std::nullopt;
    }

    const auto result = statement.step();
    if (result != SQLITE_ROW) {
      return std::nullopt;
    }

    const int size = statement.columnBytes(0);
    bytes::Buffer buffer(size);

    if (size > 0) {
      const auto blob = static_cast<const unsigned char*>(statement.columnBlob(0));
      if (blob != nullptr) {
        buffer.set(blob, 0, size);
      }
    }

    Entry entry;
    entry.value = std::move(buffer);
    entry.updatedAt = static_cast<uint64_t>(statement.columnInt64(1));

    return entry;
  }

  std::optional<String> StateManager::getString (
    const String& scope,
    const String& key
  ) {
    auto entry = this->get(scope, key);
    if (!entry.has_value()) {
      return std::nullopt;
    }

    return entry->value.str();
  }

  bool StateManager::remove (
    const String& scope,
    const String& key
  ) {
    Lock lock(this->mutex);
    if (!this->ensureInitializedLocked()) {
      return false;
    }

    String normalizedScope;
    if (!this->normalizeScope(scope, normalizedScope)) {
      return false;
    }

    auto* scopedDatabase = this->ensureDatabaseForScope(normalizedScope);
    if (scopedDatabase == nullptr) {
      return false;
    }

    sqlite::Statement statement;
    if (!statement.prepare(scopedDatabase->database, "DELETE FROM state_entries WHERE key = ?;")) {
      debug(
        "StateManager: failed to prepare delete statement (code=%d, message=%s)",
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    if (!statement.bindText(1, key)) {
      return false;
    }

    const auto result = statement.step();
    return result == SQLITE_DONE;
  }

  bool StateManager::clear (const String& scope) {
    Lock lock(this->mutex);
    if (!this->ensureInitializedLocked()) {
      return false;
    }

    String normalizedScope;
    if (!this->normalizeScope(scope, normalizedScope)) {
      return false;
    }

    auto* scopedDatabase = this->ensureDatabaseForScope(normalizedScope);
    if (scopedDatabase == nullptr) {
      return false;
    }

    sqlite::Statement statement;
    if (!statement.prepare(scopedDatabase->database, "DELETE FROM state_entries;")) {
      debug(
        "StateManager: failed to prepare clear statement (code=%d, message=%s)",
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return false;
    }

    const auto result = statement.step();
    return result == SQLITE_DONE;
  }

  Vector<String> StateManager::listScopes () {
    Vector<String> scopes;
    Lock lock(this->mutex);

    if (!this->ensureInitializedLocked()) {
      return scopes;
    }

    Set<String> unique;

    auto addScope = [&](const String& scope) {
      if (scope.empty()) {
        return;
      }
      if (unique.insert(scope).second) {
        scopes.push_back(scope);
      }
    };

    for (const auto& tuple : this->scopedDatabases) {
      addScope(tuple.first);
    }

    std::error_code ec;
    if (!this->stateDirectory.empty()) {
      for (const auto& entry : oro::fs::directory_iterator(this->stateDirectory, ec)) {
        if (ec) {
          break;
        }

        if (!entry.is_regular_file()) {
          continue;
        }

        const auto path = entry.path();
        if (path.extension() != ".db") {
          continue;
        }

        sqlite::Database db;
        if (!db.open(path.string(), SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX)) {
          continue;
        }

        sqlite::Statement meta(db, "SELECT value FROM state_meta WHERE key = 'origin' LIMIT 1;");
        if (meta.handle() != nullptr && meta.step() == SQLITE_ROW) {
          const auto origin = meta.columnText(0);
          String normalized;
          if (this->normalizeScope(origin, normalized)) {
            addScope(normalized);
          } else {
            addScope(origin);
          }
        }

        db.close();
      }
    }

    return scopes;
  }

  Vector<String> StateManager::listKeys (const String& scope) {
    Vector<String> keys;
    Lock lock(this->mutex);

    if (!this->ensureInitializedLocked()) {
      return keys;
    }

    String normalizedScope;
    if (!this->normalizeScope(scope, normalizedScope)) {
      return keys;
    }

    auto* scopedDatabase = this->ensureDatabaseForScope(normalizedScope);
    if (scopedDatabase == nullptr) {
      return keys;
    }

    sqlite::Statement statement(
      scopedDatabase->database,
      "SELECT key FROM state_entries ORDER BY key ASC;"
    );

    if (statement.handle() == nullptr) {
      debug(
        "StateManager: failed to prepare key enumeration statement (code=%d, message=%s)",
        scopedDatabase->database.lastErrorCode(),
        scopedDatabase->database.lastErrorMessage().c_str()
      );
      return keys;
    }

    while (statement.step() == SQLITE_ROW) {
      keys.push_back(statement.columnText(0));
    }

    return keys;
  }

  const Path& StateManager::databasePath () const {
    return this->stateDirectory;
  }
}
