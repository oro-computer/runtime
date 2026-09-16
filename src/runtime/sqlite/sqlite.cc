#include "../sqlite.hh"
#include <cstdlib>

namespace oro::runtime::sqlite {
  namespace {
    String crsqliteExtensionSuffix () {
    #if ORO_RUNTIME_PLATFORM_WINDOWS
      return ".dll";
    #elif ORO_RUNTIME_PLATFORM_MACOS || ORO_RUNTIME_PLATFORM_IOS
      return ".dylib";
    #else
      return ".so";
    #endif
    }

    String crsqliteExtensionPath () {
    #if !ORO_RUNTIME_PLATFORM_DESKTOP && !ORO_RUNTIME_PLATFORM_ANDROID && !ORO_RUNTIME_PLATFORM_IOS
      return {};
    #endif

      const char* home = std::getenv("ORO_HOME");
      if (home == nullptr || home[0] == '\0') {
        return {};
      }

      String base(home);
      String subdir;

    #if ORO_RUNTIME_PLATFORM_DESKTOP
      subdir = ::oro::runtime::platform.arch;
      subdir += "-desktop";
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      String abiSegment;
    #  if defined(__aarch64__) || defined(_M_ARM64)
      abiSegment = "arm64-v8a";
    #  elif defined(__x86_64__) || defined(_M_X64)
      abiSegment = "x86_64";
    #  else
      abiSegment = "unknown";
    #  endif
      subdir = abiSegment;
      subdir += "-android";
    #elif ORO_RUNTIME_PLATFORM_IOS
      #if ORO_RUNTIME_PLATFORM_IOS_SIMULATOR
        #if defined(__x86_64__) || defined(_M_X64)
          subdir = "x86_64-iPhoneSimulator";
        #elif defined(__aarch64__) || defined(_M_ARM64)
          subdir = "arm64-iPhoneSimulator";
        #else
          subdir = ::oro::runtime::platform.arch;
          subdir += "-iPhoneSimulator";
        #endif
      #else
        subdir = "arm64-iPhoneOS";
      #endif
    #endif

      String ext = crsqliteExtensionSuffix();

      String path = base;
      path += "/lib/";
      path += subdir;
      path += "/extensions/crsqlite";
      path += ext;
      return path;
    }

    bool loadCrsqliteExtension (sqlite3* handle, int& outResult, bool& loaded) {
      loaded = false;
    #if !ORO_RUNTIME_PLATFORM_DESKTOP && !ORO_RUNTIME_PLATFORM_ANDROID && !ORO_RUNTIME_PLATFORM_IOS
      outResult = SQLITE_OK;
      return true;
    #else
      const String path = crsqliteExtensionPath();
      if (path.empty()) {
        // Packaged apps can use SQLite without a configured extension directory.
        outResult = SQLITE_OK;
        return true;
      }

      char* errorMessagePtr = nullptr;
      const int rc = sqlite3_load_extension(handle, path.c_str(), nullptr, &errorMessagePtr);
      if (errorMessagePtr != nullptr) {
        sqlite3_free(errorMessagePtr);
      }

      outResult = rc;
      loaded = rc == SQLITE_OK;
      return rc == SQLITE_OK;
    #endif
    }

    int execBridge (void* data, int columns, char** values, char** names) {
      auto* fn = static_cast<ExecCallback*>(data);
      if (fn == nullptr || !(*fn)) {
        return 0;
      }

      return (*fn)(columns, values, names);
    }
  }

  Database::Database (const String& path, int flags) {
    this->open(path, flags);
  }

  Database::Database (Database&& other) noexcept {
    this->database = other.database;
    this->crsqliteLoaded = other.crsqliteLoaded;
    this->lastResultCode = other.lastResultCode;
    other.database = nullptr;
    other.crsqliteLoaded = false;
    other.lastResultCode = SQLITE_OK;
  }

  Database& Database::operator = (Database&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (!this->close()) {
      return *this;
    }
    this->database = other.database;
    this->crsqliteLoaded = other.crsqliteLoaded;
    this->lastResultCode = other.lastResultCode;
    other.database = nullptr;
    other.crsqliteLoaded = false;
    other.lastResultCode = SQLITE_OK;
    return *this;
  }

  Database::~Database () {
    this->close();
  }

  bool Database::open (const String& path, int flags) {
    if (!this->close()) {
      return false;
    }

    sqlite3* handle = nullptr;
    const int result = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    this->lastResultCode = result;

    if (result != SQLITE_OK) {
      if (handle != nullptr) {
        sqlite3_close(handle);
      }
      return false;
    }

    int configResult = sqlite3_db_config(
      handle,
      SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION,
      1,
      nullptr
    );
    if (configResult != SQLITE_OK) {
      sqlite3_close(handle);
      this->lastResultCode = configResult;
      return false;
    }

    int extensionResult = SQLITE_OK;
    if (!loadCrsqliteExtension(handle, extensionResult, this->crsqliteLoaded)) {
      sqlite3_close(handle);
      this->lastResultCode = extensionResult;
      return false;
    }

    this->database = handle;
    this->lastResultCode = SQLITE_OK;
    return true;
  }

  bool Database::close () {
    if (this->database == nullptr) {
      this->lastResultCode = SQLITE_OK;
      return true;
    }

    if (this->crsqliteLoaded) {
      // CR-SQLite caches statements that otherwise keep close_v2 from releasing the connection.
      const int result = sqlite3_exec(this->database, "SELECT crsql_finalize()", nullptr, nullptr, nullptr);
      this->lastResultCode = result;
      if (result != SQLITE_OK) {
        return false;
      }
      this->crsqliteLoaded = false;
    }

    const int result = sqlite3_close_v2(this->database);
    this->lastResultCode = result;

    if (result != SQLITE_OK) {
      return false;
    }

    this->database = nullptr;
    return true;
  }

  bool Database::isOpen () const {
    return this->database != nullptr;
  }

  sqlite3* Database::handle () const {
    return this->database;
  }

  int Database::lastResult () const {
    return this->lastResultCode;
  }

  int Database::lastErrorCode () const {
    if (this->database == nullptr) {
      return this->lastResultCode != SQLITE_OK ? this->lastResultCode : SQLITE_MISUSE;
    }

    return sqlite3_errcode(this->database);
  }

  String Database::lastErrorMessage () const {
    if (this->database == nullptr) {
      return errorMessage(this->lastErrorCode());
    }

    return sqlite3_errmsg(this->database);
  }

  bool Database::exec (const String& sql, const ExecCallback& callback) const {
    if (this->database == nullptr) {
      this->lastResultCode = SQLITE_MISUSE;
      return false;
    }

    ExecCallback handler = callback;
    ExecCallback* handlerPtr = callback ? &handler : nullptr;
    char* errorMessagePtr = nullptr;

    const int result = sqlite3_exec(
      this->database,
      sql.c_str(),
      callback ? execBridge : nullptr,
      handlerPtr,
      &errorMessagePtr
    );

    if (errorMessagePtr != nullptr) {
      sqlite3_free(errorMessagePtr);
    }

    this->lastResultCode = result;
    return result == SQLITE_OK;
  }

  void Database::setLastResult (int result) {
    this->lastResultCode = result;
  }

  Statement::Statement (Database& db, const String& sql) {
    this->prepare(db, sql);
  }

  Statement::Statement (Statement&& other) noexcept {
    this->statement = other.statement;
    this->database = other.database;
    other.statement = nullptr;
    other.database = nullptr;
  }

  Statement& Statement::operator = (Statement&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    this->finalize();

    this->statement = other.statement;
    this->database = other.database;
    other.statement = nullptr;
    other.database = nullptr;
    return *this;
  }

  Statement::~Statement () {
    this->finalize();
  }

  bool Statement::prepare (Database& db, const String& sql) {
    this->finalize();

    if (!db.isOpen()) {
      return false;
    }

    sqlite3_stmt* handle = nullptr;
    const int result = sqlite3_prepare_v2(db.handle(), sql.c_str(), static_cast<int>(sql.size()), &handle, nullptr);

    if (result != SQLITE_OK) {
      return false;
    }

    this->statement = handle;
    this->database = db.handle();
    return true;
  }

  void Statement::finalize () {
    if (this->statement == nullptr) {
      this->database = nullptr;
      return;
    }

    sqlite3_finalize(this->statement);
    this->statement = nullptr;
    this->database = nullptr;
  }

  int Statement::step () {
    if (this->statement == nullptr) {
      return SQLITE_MISUSE;
    }

    return sqlite3_step(this->statement);
  }

  bool Statement::reset () {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_reset(this->statement) == SQLITE_OK;
  }

  int Statement::columnCount () const {
    if (this->statement == nullptr) {
      return 0;
    }

    return sqlite3_column_count(this->statement);
  }

  String Statement::columnText (int index) const {
    if (this->statement == nullptr) {
      return {};
    }

    const unsigned char* text = sqlite3_column_text(this->statement, index);
    if (text == nullptr) {
      return {};
    }

    return reinterpret_cast<const char*>(text);
  }

  const void* Statement::columnBlob (int index) const {
    if (this->statement == nullptr) {
      return nullptr;
    }

    return sqlite3_column_blob(this->statement, index);
  }

  int Statement::columnBytes (int index) const {
    if (this->statement == nullptr) {
      return 0;
    }

    return sqlite3_column_bytes(this->statement, index);
  }

  int Statement::columnInt (int index) const {
    if (this->statement == nullptr) {
      return 0;
    }

    return sqlite3_column_int(this->statement, index);
  }

  int64_t Statement::columnInt64 (int index) const {
    if (this->statement == nullptr) {
      return 0;
    }

    return sqlite3_column_int64(this->statement, index);
  }

  double Statement::columnDouble (int index) const {
    if (this->statement == nullptr) {
      return 0.0;
    }

    return sqlite3_column_double(this->statement, index);
  }

  sqlite3_stmt* Statement::handle () const {
    return this->statement;
  }

  bool Statement::bindNull (int index) {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_bind_null(this->statement, index) == SQLITE_OK;
  }

  bool Statement::bindInt (int index, int value) {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_bind_int(this->statement, index, value) == SQLITE_OK;
  }

  bool Statement::bindInt64 (int index, int64_t value) {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_bind_int64(this->statement, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
  }

  bool Statement::bindDouble (int index, double value) {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_bind_double(this->statement, index, value) == SQLITE_OK;
  }

  bool Statement::bindText (int index, const String& value) {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_bind_text(
      this->statement,
      index,
      value.c_str(),
      static_cast<int>(value.size()),
      SQLITE_TRANSIENT
    ) == SQLITE_OK;
  }

  bool Statement::bindBlob (int index, const void* data, int size) {
    if (this->statement == nullptr) {
      return false;
    }

    const void* blob = size > 0 ? data : nullptr;
    return sqlite3_bind_blob(
      this->statement,
      index,
      blob,
      size,
      SQLITE_TRANSIENT
    ) == SQLITE_OK;
  }

  bool Statement::clearBindings () {
    if (this->statement == nullptr) {
      return false;
    }

    return sqlite3_clear_bindings(this->statement) == SQLITE_OK;
  }

  String errorMessage (int code) {
    return sqlite3_errstr(code);
  }
}
