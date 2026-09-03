#include "sqlite.hh"

#include "../../json.hh"
#include "../../queued_response.hh"
#include "../../bytes.hh"
#include "../../crypto.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>

namespace oro::runtime::core::services {
  namespace {
    constexpr size_t kMaxResultRows = 50000;
    constexpr size_t kMaxResultBytes = 16 * 1024 * 1024;
    constexpr size_t kDefaultStepLimit = 128;

    String toLowerCopy (String value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return value;
    }

    bool isSQLiteFileUri (const String& path) {
      return path.rfind("file:", 0) == 0;
    }

    bool isSQLiteMemoryPath (const String& path) {
      if (path == ":memory:") {
        return true;
      }

      const auto lower = toLowerCopy(path);
      if (lower.rfind("file::memory:", 0) == 0) {
        return true;
      }

      return lower.rfind("file:", 0) == 0 && lower.find("mode=memory") != String::npos;
    }

    void trimLeadingWhitespace (const char*& cursor) {
      while (cursor != nullptr && *cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
      }
    }

    std::optional<String> decodeFileUriPath (const String& uri) {
      if (!isSQLiteFileUri(uri)) {
        return std::nullopt;
      }

      if (isSQLiteMemoryPath(uri)) {
        return std::nullopt;
      }

      String remainder = uri.substr(5);
      if (remainder.empty()) {
        return std::nullopt;
      }

      const auto queryPos = remainder.find('?');
      if (queryPos != String::npos) {
        remainder = remainder.substr(0, queryPos);
      }

      if (remainder.rfind("//", 0) == 0) {
        const auto slash = remainder.find('/', 2);
        if (slash == String::npos) {
          return std::nullopt;
        }
        remainder = remainder.substr(slash);
      }

      String decoded;
      decoded.reserve(remainder.size());
      for (size_t i = 0; i < remainder.size(); ++i) {
        const char ch = remainder[i];
        if (ch == '%' && i + 2 < remainder.size()) {
          const char hi = remainder[i + 1];
          const char lo = remainder[i + 2];
          if (std::isxdigit(static_cast<unsigned char>(hi)) &&
              std::isxdigit(static_cast<unsigned char>(lo))) {
            const int value = std::stoi(remainder.substr(i + 1, 2), nullptr, 16);
            decoded.push_back(static_cast<char>(value));
            i += 2;
            continue;
          }
        }
        decoded.push_back(ch);
      }

      if (decoded.empty()) {
        return std::nullopt;
      }

      if (decoded.rfind("//", 0) == 0) {
        const auto slash = decoded.find('/', 2);
        if (slash != String::npos) {
          decoded = decoded.substr(slash);
        }
      }

      if (decoded.size() >= 3 && decoded[0] == '/' && decoded[1] == '/' && decoded[2] == '/') {
        decoded.erase(0, 2);
      }

      if (decoded.size() >= 3 &&
          decoded[0] == '/' &&
          std::isalpha(static_cast<unsigned char>(decoded[1])) &&
          decoded[2] == ':') {
        decoded.erase(0, 1);
      }

      return decoded;
    }

    String runtimeTypeForCode (int code) {
      switch (code) {
        case SQLITE_INTEGER: return "integer";
        case SQLITE_FLOAT: return "float";
        case SQLITE_TEXT: return "text";
        case SQLITE_BLOB: return "blob";
        case SQLITE_NULL: return "null";
        default: return "";
      }
    }

    JSON::Array::Entries buildColumnsMeta (
      const Vector<String>& names,
      const Vector<String>& declTypes,
      const Vector<String>& runtimeTypes
    ) {
      JSON::Array::Entries meta;
      const size_t count = names.size();
      meta.reserve(count);

      for (size_t i = 0; i < count; ++i) {
        JSON::Object::Entries column;
        column["name"] = names[i];

        const auto decl = i < declTypes.size() ? declTypes[i] : String();
        column["declType"] = decl;

        const auto runtime = i < runtimeTypes.size() ? runtimeTypes[i] : String("null");
        column["type"] = runtime.empty() ? String("null") : runtime;

        meta.push_back(column);
      }

      return meta;
    }

    struct ExtractedValue {
      JSON::Any value;
      String runtimeType;
      size_t bytes = 0;
    };

    ExtractedValue extractValue (sqlite3_stmt* statement, int columnIndex) {
      ExtractedValue extracted;
      const int code = sqlite3_column_type(statement, columnIndex);
      extracted.runtimeType = runtimeTypeForCode(code);

      switch (code) {
        case SQLITE_NULL: {
          extracted.value = JSON::null;
          break;
        }
        case SQLITE_INTEGER: {
          const sqlite3_int64 raw = sqlite3_column_int64(statement, columnIndex);
          const String text = std::to_string(raw);
          extracted.bytes = text.size();
          extracted.value = JSON::Object::Entries {
            {"type", String("integer")},
            {"value", text}
          };
          break;
        }
        case SQLITE_FLOAT: {
          const double raw = sqlite3_column_double(statement, columnIndex);
          const String text = std::to_string(raw);
          extracted.bytes = text.size();
          extracted.value = raw;
          break;
        }
        case SQLITE_TEXT: {
          const unsigned char* ptr = sqlite3_column_text(statement, columnIndex);
          const int size = sqlite3_column_bytes(statement, columnIndex);
          extracted.bytes = size > 0 ? static_cast<size_t>(size) : 0;
          if (ptr != nullptr && size > 0) {
            extracted.value = String(reinterpret_cast<const char*>(ptr), size);
          } else {
            extracted.value = String();
          }
          break;
        }
        case SQLITE_BLOB: {
          const void* blobPtr = sqlite3_column_blob(statement, columnIndex);
          const int size = sqlite3_column_bytes(statement, columnIndex);
          extracted.bytes = size > 0 ? static_cast<size_t>(size) : 0;
          String encoded;
          if (blobPtr != nullptr && size > 0) {
            encoded = bytes::base64::encode(String(static_cast<const char*>(blobPtr), size));
          }
          JSON::Object::Entries wrapper {
            {"encoding", String("base64")},
            {"data", encoded}
          };
          extracted.value = wrapper;
          break;
        }
        default: {
          extracted.value = JSON::null;
          break;
        }
      }

      return extracted;
    }

    bool bindValue (sqlite::Statement& statement, int index, const JSON::Any& param) {
      if (param.isNull()) {
        return statement.bindNull(index);
      }

      if (param.isBoolean()) {
        const bool value = param.as<JSON::Boolean>().value();
        return statement.bindInt(index, value ? 1 : 0);
      }

      if (param.isNumber()) {
        const double raw = param.as<JSON::Number>().value();
        const double rounded = std::floor(raw);
        if (std::fabs(raw - rounded) < std::numeric_limits<double>::epsilon()) {
          const double absRounded = std::fabs(rounded);
          if (absRounded <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
            return statement.bindInt64(index, static_cast<int64_t>(rounded));
          }
        }
        return statement.bindDouble(index, raw);
      }

      if (param.isString()) {
        return statement.bindText(index, param.as<JSON::String>().value());
      }

      if (param.isObject()) {
        const auto object = param.as<JSON::Object>();
        const auto type = object.get("type");
        if (type.isString()) {
          const auto typeString = type.as<JSON::String>().value();
          const auto valueAny = object.get("value");

          if (typeString == "integer") {
            if (valueAny.isString()) {
              const auto value = valueAny.as<JSON::String>().value();
              try {
                const sqlite3_int64 parsed = static_cast<sqlite3_int64>(std::stoll(value));
                return statement.bindInt64(index, parsed);
              } catch (const std::exception&) {
                return statement.bindText(index, value);
              }
            }

            if (valueAny.isNumber()) {
              return statement.bindInt64(index, static_cast<int64_t>(valueAny.as<JSON::Number>().value()));
            }
          } else if (typeString == "float" && valueAny.isNumber()) {
            return statement.bindDouble(index, valueAny.as<JSON::Number>().value());
          } else if (typeString == "text" && valueAny.isString()) {
            return statement.bindText(index, valueAny.as<JSON::String>().value());
          }
        }

        const auto encoding = object.get("encoding");
        if (encoding.isString() && encoding.as<JSON::String>().value() == "base64") {
          const auto dataAny = object.get("data");
          const String encoded = dataAny.isString()
            ? dataAny.as<JSON::String>().value()
            : String();
          const String decoded = bytes::base64::decode(encoded);
          return statement.bindBlob(index, decoded.data(), static_cast<int>(decoded.size()));
        }
      }

      return statement.bindText(index, param.str());
    }

    bool bindValue (sqlite3_stmt* stmt, int index, const JSON::Any& param) {
      if (param.isNull()) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
      }

      if (param.isBoolean()) {
        const bool value = param.as<JSON::Boolean>().value();
        return sqlite3_bind_int(stmt, index, value ? 1 : 0) == SQLITE_OK;
      }

      if (param.isNumber()) {
        const double raw = param.as<JSON::Number>().value();
        const double rounded = std::floor(raw);
        if (std::fabs(raw - rounded) < std::numeric_limits<double>::epsilon()) {
          const double absRounded = std::fabs(rounded);
          if (absRounded <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
            return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(static_cast<int64_t>(rounded))) == SQLITE_OK;
          }
        }
        return sqlite3_bind_double(stmt, index, raw) == SQLITE_OK;
      }

      if (param.isString()) {
        const auto value = param.as<JSON::String>().value();
        return sqlite3_bind_text(
          stmt,
          index,
          value.c_str(),
          static_cast<int>(value.size()),
          SQLITE_TRANSIENT
        ) == SQLITE_OK;
      }

      if (param.isObject()) {
        const auto object = param.as<JSON::Object>();
        const auto type = object.get("type");
        if (type.isString()) {
          const auto typeString = type.as<JSON::String>().value();
          const auto valueAny = object.get("value");

          if (typeString == "integer") {
            if (valueAny.isString()) {
              const auto value = valueAny.as<JSON::String>().value();
              try {
                const sqlite3_int64 parsed = static_cast<sqlite3_int64>(std::stoll(value));
                return sqlite3_bind_int64(stmt, index, parsed) == SQLITE_OK;
              } catch (const std::exception&) {
                return sqlite3_bind_text(
                  stmt,
                  index,
                  value.c_str(),
                  static_cast<int>(value.size()),
                  SQLITE_TRANSIENT
                ) == SQLITE_OK;
              }
            }

            if (valueAny.isNumber()) {
              const auto value = static_cast<sqlite3_int64>(valueAny.as<JSON::Number>().value());
              return sqlite3_bind_int64(stmt, index, value) == SQLITE_OK;
            }
          } else if (typeString == "float" && valueAny.isNumber()) {
            const auto value = valueAny.as<JSON::Number>().value();
            return sqlite3_bind_double(stmt, index, value) == SQLITE_OK;
          } else if (typeString == "text" && valueAny.isString()) {
            const auto value = valueAny.as<JSON::String>().value();
            return sqlite3_bind_text(
              stmt,
              index,
              value.c_str(),
              static_cast<int>(value.size()),
              SQLITE_TRANSIENT
            ) == SQLITE_OK;
          }
        }

        const auto encoding = object.get("encoding");
        if (encoding.isString() && encoding.as<JSON::String>().value() == "base64") {
          const auto dataAny = object.get("data");
          const String encoded = dataAny.isString()
            ? dataAny.as<JSON::String>().value()
            : String();
          const String decoded = bytes::base64::decode(encoded);
          return sqlite3_bind_blob(
            stmt,
            index,
            decoded.data(),
            static_cast<int>(decoded.size()),
            SQLITE_TRANSIENT
          ) == SQLITE_OK;
        }
      }

      const auto value = param.str();
      return sqlite3_bind_text(
        stmt,
        index,
        value.c_str(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
      ) == SQLITE_OK;
    }
  }

  SharedPointer<SQLite::Handle> SQLite::getHandle (ID id) {
    Lock lock(this->mutex);
    auto it = this->handles.find(id);
    if (it == this->handles.end()) {
      return nullptr;
    }
    return it->second;
  }

  SharedPointer<SQLite::StatementHandle> SQLite::getStatement (ID id) {
    Lock lock(this->mutex);
    auto it = this->statements.find(id);
    if (it == this->statements.end()) {
      return nullptr;
    }
    return it->second;
  }

  SQLite::ID SQLite::generateUniqueId (const Map<ID, SharedPointer<StatementHandle>>& map) const {
    ID id = 0;
    do {
      id = crypto::rand64();
    } while (id == 0 || map.contains(id));
    return id;
  }

  void SQLite::finalizeStatementsForHandle (const SharedPointer<Handle>& handle) {
    Vector<ID> toErase;
    for (const auto& entry : this->statements) {
      if (entry.second->database == handle) {
        toErase.push_back(entry.first);
      }
    }

    for (const auto& id : toErase) {
      auto it = this->statements.find(id);
      if (it == this->statements.end()) {
        continue;
      }

      it->second->statement.finalize();
      this->statements.erase(it);
    }
  }

  bool SQLite::stop () {
    Lock lock(this->mutex);

    for (auto it = this->statements.begin(); it != this->statements.end(); ) {
      it->second->statement.finalize();
      it = this->statements.erase(it);
    }

    for (const auto& entry : this->handles) {
      auto handle = entry.second;
      handle->database.close();
      if (!handle->isMemory && handle->resource) {
        handle->resource->stopAccessing();
      }
    }

    this->handles.clear();
    return true;
  }

  void SQLite::open (
    const ipc::Message::Seq& seq,
    ID id,
    const String& path,
    int flags,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      {
        Lock lock(this->mutex);
        if (this->handles.contains(id)) {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.open"},
            {"err", JSON::Object::Entries {
              {"type", "InvalidStateError"},
              {"message", "A database with that id already exists"},
              {"id", std::to_string(id)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }
      }

      auto handle = std::make_shared<Handle>();
      handle->id = id;

      const bool isMemory = isSQLiteMemoryPath(path);
      handle->isMemory = isMemory;

      const bool looksLikeUri = isSQLiteFileUri(path);
      int openFlags = flags;
      bool treatAsUri = (openFlags & SQLITE_OPEN_URI) != 0 || looksLikeUri;

      if (treatAsUri) {
        openFlags |= SQLITE_OPEN_URI;
      }

      filesystem::Resource::Options resourceOptions{};
      resourceOptions.allowCreate = (openFlags & SQLITE_OPEN_CREATE) != 0;

      String sqlitePath = path;
      String reportedPath = path;

      if (!isMemory) {
        std::unique_ptr<filesystem::Resource> resource;

        if (treatAsUri) {
          if (auto decoded = decodeFileUriPath(path)) {
            resource = std::make_unique<filesystem::Resource>(*decoded, resourceOptions);
          } else {
            const auto json = JSON::Object::Entries {
              {"source", "sqlite.open"},
              {"err", JSON::Object::Entries {
                {"type", "SecurityError"},
                {"message", "Unsupported SQLite URI path"},
                {"code", "EPERM"},
                {"id", std::to_string(id)}
              }}
            };

            callback(seq, json, QueuedResponse{});
            return;
          }
        } else {
          resource = std::make_unique<filesystem::Resource>(path, resourceOptions);
        }

        if (resource) {
          if (!resourceOptions.allowCreate && !resource->exists()) {
            const auto json = JSON::Object::Entries {
              {"source", "sqlite.open"},
              {"err", JSON::Object::Entries {
                {"type", "NotFoundError"},
                {"message", "Database path does not exist"},
                {"code", "ENOENT"},
                {"id", std::to_string(id)}
              }}
            };

            callback(seq, json, QueuedResponse{});
            return;
          }

          if (!resource->startAccessing()) {
            const auto json = JSON::Object::Entries {
              {"source", "sqlite.open"},
              {"err", JSON::Object::Entries {
                {"type", "SecurityError"},
                {"message", "Filesystem sandbox denied access"},
                {"code", "EPERM"},
                {"id", std::to_string(id)}
              }}
            };

            callback(seq, json, QueuedResponse{});
            return;
          }

          if (!treatAsUri) {
            sqlitePath = resource->path.string();
            reportedPath = sqlitePath;
          }

          handle->resource = std::move(resource);
        }
      }

      if (!handle->database.open(sqlitePath, openFlags)) {
        const int code = handle->database.lastResult();
        const auto message = handle->database.lastErrorMessage();

        if (!isMemory && handle->resource) {
          handle->resource->stopAccessing();
        }

        const auto json = JSON::Object::Entries {
          {"source", "sqlite.open"},
          {"err", JSON::Object::Entries {
            {"type", "SQLiteError"},
            {"message", message},
            {"code", std::to_string(code)},
            {"id", std::to_string(id)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      {
        Lock lock(this->mutex);
        this->handles[id] = handle;
      }

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.open"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"path", reportedPath},
          {"flags", openFlags}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::close (
    const ipc::Message::Seq& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<Handle> handle;

      {
        Lock lock(this->mutex);
        auto it = this->handles.find(id);

        if (it == this->handles.end()) {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.close"},
            {"err", JSON::Object::Entries {
              {"type", "NotFoundError"},
              {"message", "No database found for the provided id"},
              {"id", std::to_string(id)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }

        handle = it->second;
        this->finalizeStatementsForHandle(handle);
        this->handles.erase(it);
      }

      if (!handle->database.close()) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.close"},
          {"err", JSON::Object::Entries {
            {"type", "SQLiteError"},
            {"message", handle->database.lastErrorMessage()},
            {"code", std::to_string(handle->database.lastResult())},
            {"id", std::to_string(id)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      if (!handle->isMemory && handle->resource) {
        handle->resource->stopAccessing();
      }

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.close"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::exec (
    const ipc::Message::Seq& seq,
    ID id,
    const String& sql,
    bool arrayMode,
    const String& paramsJSON,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<Handle> handle = this->getHandle(id);

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.exec"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No database found for the provided id"},
            {"id", std::to_string(id)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      sqlite::Database& database = handle->database;
      sqlite3* connection = database.handle();

      if (connection == nullptr) {
        database.setLastResult(SQLITE_MISUSE);
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.exec"},
          {"err", JSON::Object::Entries {
            {"type", "InvalidStateError"},
            {"message", "Database connection is closed"},
            {"id", std::to_string(id)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      const sqlite3_int64 totalChangesBefore = sqlite3_total_changes64(connection);

      JSON::Array::Entries rows;
      JSON::Array::Entries columnsJson;
      JSON::Array::Entries columnsMeta;
      Vector<String> columnNames;
      Vector<String> columnDeclTypes;
      Vector<String> columnRuntimeTypes;

      size_t totalRows = 0;
      size_t totalBytes = 0;

      JSON::Array::Entries params;
      if (paramsJSON.size() > 0) {
        JSON::Any parsed;
        try {
          parsed = JSON::parse(paramsJSON);
        } catch (const std::exception& e) {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.exec"},
            {"err", JSON::Object::Entries {
              {"type", "SyntaxError"},
              {"message", e.what()},
              {"id", std::to_string(id)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }

        if (!parsed.isArray()) {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.exec"},
            {"err", JSON::Object::Entries {
              {"type", "TypeError"},
              {"message", "params must be a JSON array"},
              {"id", std::to_string(id)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }

        params = parsed.as<JSON::Array>().value();
      }

      const char* cursor = sql.c_str();
      trimLeadingWhitespace(cursor);

      while (cursor != nullptr && *cursor != '\0') {
        sqlite3_stmt* statement = nullptr;
        const char* tail = nullptr;

        const int prepare = sqlite3_prepare_v2(connection, cursor, -1, &statement, &tail);
        database.setLastResult(prepare);

        if (prepare != SQLITE_OK) {
          if (statement != nullptr) {
            sqlite3_finalize(statement);
          }

          const auto json = JSON::Object::Entries {
            {"source", "sqlite.exec"},
            {"err", JSON::Object::Entries {
              {"type", "SQLiteError"},
              {"message", database.lastErrorMessage()},
              {"code", std::to_string(database.lastResult())},
              {"id", std::to_string(id)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }

        if (statement == nullptr) {
          cursor = tail;
          trimLeadingWhitespace(cursor);
          continue;
        }

        if (!params.empty()) {
          const int expected = sqlite3_bind_parameter_count(statement);
          if (expected > 0) {
            // When a statement declares placeholders, enforce an upper bound.
            if (params.size() > static_cast<size_t>(expected)) {
              sqlite3_finalize(statement);

              const auto json = JSON::Object::Entries {
                {"source", "sqlite.exec"},
                {"err", JSON::Object::Entries {
                  {"type", "RangeError"},
                  {"message", "Too many parameters supplied"},
                  {"id", std::to_string(id)}
                }}
              };

              callback(seq, json, QueuedResponse{});
              return;
            }

            const size_t limit = static_cast<size_t>(expected);
            for (size_t i = 0; i < params.size() && i < limit; ++i) {
              if (!bindValue(statement, static_cast<int>(i + 1), params[i])) {
                const int code = sqlite3_errcode(connection);
                database.setLastResult(code);

                const auto json = JSON::Object::Entries {
                  {"source", "sqlite.exec"},
                  {"err", JSON::Object::Entries {
                    {"type", "SQLiteError"},
                    {"message", database.lastErrorMessage()},
                    {"code", std::to_string(database.lastResult())},
                    {"id", std::to_string(id)}
                  }}
                };

                sqlite3_finalize(statement);
                callback(seq, json, QueuedResponse{});
                return;
              }
            }
          }
        }

        const int columnCount = sqlite3_column_count(statement);
        if (columnCount > 0) {
          columnNames.clear();
          columnDeclTypes.clear();
          columnRuntimeTypes.clear();
          columnsJson.clear();
          rows.clear();
          totalRows = 0;
          totalBytes = 0;

          columnNames.reserve(columnCount);
          columnDeclTypes.reserve(columnCount);
          columnRuntimeTypes.reserve(columnCount);
          columnsJson.reserve(columnCount);

          for (int i = 0; i < columnCount; ++i) {
            const char* name = sqlite3_column_name(statement, i);
            const char* decl = sqlite3_column_decltype(statement, i);

            const String columnName = name ? String(name) : String("");
            const String declType = decl ? String(decl) : String();

            columnNames.push_back(columnName);
            columnDeclTypes.push_back(declType);
            columnRuntimeTypes.push_back(String("null"));
            columnsJson.push_back(columnName);
          }
        }

        while (true) {
          const int step = sqlite3_step(statement);
          database.setLastResult(step);

          if (step == SQLITE_ROW) {
            if (columnNames.empty()) {
              continue;
            }

            Vector<ExtractedValue> extractedRow;
            extractedRow.reserve(columnNames.size());
            for (size_t i = 0; i < columnNames.size(); ++i) {
              extractedRow.push_back(extractValue(statement, static_cast<int>(i)));
            }

            if (arrayMode) {
              JSON::Array::Entries row;
              row.reserve(columnNames.size());

              for (const auto& column : extractedRow) {
                row.push_back(column.value);
              }

              rows.push_back(row);
            } else {
              JSON::Object::Entries row;

              for (size_t i = 0; i < columnNames.size(); ++i) {
                row.insert_or_assign(columnNames[i], extractedRow[i].value);
              }

              rows.push_back(row);
            }

            for (size_t i = 0; i < extractedRow.size(); ++i) {
              if (!extractedRow[i].runtimeType.empty()) {
                columnRuntimeTypes[i] = extractedRow[i].runtimeType;
              }
              totalBytes += extractedRow[i].bytes;
            }

            totalRows += 1;

            if (totalRows > kMaxResultRows || totalBytes > kMaxResultBytes) {
              sqlite3_finalize(statement);
              database.setLastResult(SQLITE_TOOBIG);

              const auto json = JSON::Object::Entries {
                {"source", "sqlite.exec"},
                {"err", JSON::Object::Entries {
                  {"type", "RangeError"},
                  {"message", "SQLite exec result exceeds configured limits"},
                  {"code", std::to_string(SQLITE_TOOBIG)},
                  {"id", std::to_string(id)}
                }}
              };

              callback(seq, json, QueuedResponse{});
              return;
            }
          } else if (step == SQLITE_DONE) {
            break;
          } else {
            const auto json = JSON::Object::Entries {
              {"source", "sqlite.exec"},
              {"err", JSON::Object::Entries {
                {"type", "SQLiteError"},
                {"message", database.lastErrorMessage()},
                {"code", std::to_string(database.lastResult())},
                {"id", std::to_string(id)}
              }}
            };

            sqlite3_finalize(statement);
            callback(seq, json, QueuedResponse{});
            return;
          }
        }

        sqlite3_finalize(statement);
        cursor = tail;
        trimLeadingWhitespace(cursor);
      }

      database.setLastResult(SQLITE_OK);

      const sqlite3_int64 totalChangesAfter = sqlite3_total_changes64(connection);
      const sqlite3_int64 changes = totalChangesAfter == totalChangesBefore
        ? 0
        : sqlite3_changes64(connection);
      const sqlite3_int64 lastInsertRowId = sqlite3_last_insert_rowid(connection);
      const auto meta = buildColumnsMeta(columnNames, columnDeclTypes, columnRuntimeTypes);

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.exec"},
        {"data", JSON::Object::Entries {
          {"rows", rows},
          {"columns", columnsJson},
          {"columnsMeta", meta},
          {"changes", changes},
          {"lastInsertRowid", std::to_string(lastInsertRowId)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::prepare (
    const ipc::Message::Seq& seq,
    ID databaseId,
    const String& sql,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<Handle> handle = this->getHandle(databaseId);

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.prepare"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No database found for the provided id"},
            {"id", std::to_string(databaseId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      auto statementHandle = std::make_shared<StatementHandle>();
      statementHandle->database = handle;

      if (!statementHandle->statement.prepare(handle->database, sql)) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.prepare"},
          {"err", JSON::Object::Entries {
            {"type", "SQLiteError"},
            {"message", handle->database.lastErrorMessage()},
            {"code", std::to_string(handle->database.lastResult())},
            {"databaseId", std::to_string(databaseId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      sqlite3_stmt* stmt = statementHandle->statement.handle();
      const int columnCount = stmt ? sqlite3_column_count(stmt) : 0;

      statementHandle->columnNames.reserve(columnCount);
      statementHandle->columnDeclTypes.reserve(columnCount);
      statementHandle->columnRuntimeTypes.reserve(columnCount);

      JSON::Array::Entries columnsJson;
      columnsJson.reserve(columnCount);

      for (int i = 0; i < columnCount; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        const char* decl = sqlite3_column_decltype(stmt, i);

        const String columnName = name ? String(name) : String("");
        const String declType = decl ? String(decl) : String();

        statementHandle->columnNames.push_back(columnName);
        statementHandle->columnDeclTypes.push_back(declType);
        statementHandle->columnRuntimeTypes.push_back(String("null"));

        columnsJson.push_back(columnName);
      }

      ID statementId;
      {
        Lock lock(this->mutex);
        statementId = this->generateUniqueId(this->statements);
        statementHandle->id = statementId;
        this->statements[statementId] = statementHandle;
      }

      const auto meta = buildColumnsMeta(
        statementHandle->columnNames,
        statementHandle->columnDeclTypes,
        statementHandle->columnRuntimeTypes
      );

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.prepare"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(statementId)},
          {"databaseId", std::to_string(databaseId)},
          {"columns", columnsJson},
          {"columnsMeta", meta}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::bind (
    const ipc::Message::Seq& seq,
    ID statementId,
    const String& paramsJSON,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<StatementHandle> handle = this->getStatement(statementId);

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.bind"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No statement found for the provided id"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      JSON::Any parsed;
      try {
        parsed = JSON::parse(paramsJSON);
      } catch (const std::exception& e) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.bind"},
          {"err", JSON::Object::Entries {
            {"type", "SyntaxError"},
            {"message", e.what()},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      if (!parsed.isArray()) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.bind"},
          {"err", JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", "params must be a JSON array"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      auto params = parsed.as<JSON::Array>().value();

      auto& statement = handle->statement;
      statement.reset();
      statement.clearBindings();

      sqlite3_stmt* stmt = statement.handle();
      const int expected = stmt ? sqlite3_bind_parameter_count(stmt) : 0;

      if (params.size() > static_cast<size_t>(expected)) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.bind"},
          {"err", JSON::Object::Entries {
            {"type", "RangeError"},
            {"message", "Too many parameters supplied"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      for (size_t i = 0; i < params.size(); ++i) {
        if (!bindValue(statement, static_cast<int>(i + 1), params[i])) {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.statement.bind"},
            {"err", JSON::Object::Entries {
              {"type", "SQLiteError"},
              {"message", handle->database->database.lastErrorMessage()},
              {"code", std::to_string(handle->database->database.lastResult())},
              {"id", std::to_string(statementId)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }
      }

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.statement.bind"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(statementId)},
          {"bound", static_cast<int>(params.size())}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::step (
    const ipc::Message::Seq& seq,
    ID statementId,
    size_t limit,
    bool arrayMode,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<StatementHandle> handle = this->getStatement(statementId);

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.step"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No statement found for the provided id"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      size_t stepLimit = limit;
      if (stepLimit == 0) {
        stepLimit = kDefaultStepLimit;
      }

      sqlite::Statement& statement = handle->statement;
      sqlite3_stmt* stmt = statement.handle();
      sqlite3* connection = handle->database->database.handle();

      if (stmt == nullptr || connection == nullptr) {
        handle->database->database.setLastResult(SQLITE_MISUSE);
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.step"},
          {"err", JSON::Object::Entries {
            {"type", "InvalidStateError"},
            {"message", "Statement is not prepared"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      const sqlite3_int64 totalChangesBefore = sqlite3_total_changes64(connection);

      JSON::Array::Entries rows;
      rows.reserve(stepLimit);

      bool done = false;
      size_t produced = 0;

      while (!done) {
        const int step = sqlite3_step(stmt);
        handle->database->database.setLastResult(step);

        if (step == SQLITE_ROW) {
          const size_t columnCount = handle->columnNames.size();
          Vector<ExtractedValue> extractedRow;
          extractedRow.reserve(columnCount);

          for (size_t i = 0; i < columnCount; ++i) {
            extractedRow.push_back(extractValue(stmt, static_cast<int>(i)));
          }

          if (arrayMode) {
            JSON::Array::Entries row;
            row.reserve(columnCount);
            for (const auto& column : extractedRow) {
              row.push_back(column.value);
            }
            rows.push_back(row);
          } else {
            JSON::Object::Entries row;
            for (size_t i = 0; i < columnCount; ++i) {
              row.insert_or_assign(handle->columnNames[i], extractedRow[i].value);
            }
            rows.push_back(row);
          }

          for (size_t i = 0; i < extractedRow.size(); ++i) {
            if (!extractedRow[i].runtimeType.empty()) {
              handle->columnRuntimeTypes[i] = extractedRow[i].runtimeType;
            }
          }

          produced += 1;

          if (produced >= stepLimit && !handle->columnNames.empty()) {
            break;
          }
        } else if (step == SQLITE_DONE) {
          done = true;
        } else {
          const auto json = JSON::Object::Entries {
            {"source", "sqlite.statement.step"},
            {"err", JSON::Object::Entries {
              {"type", "SQLiteError"},
              {"message", handle->database->database.lastErrorMessage()},
              {"code", std::to_string(handle->database->database.lastResult())},
              {"id", std::to_string(statementId)}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }
      }

      const auto meta = buildColumnsMeta(
        handle->columnNames,
        handle->columnDeclTypes,
        handle->columnRuntimeTypes
      );

      const sqlite3_int64 totalChangesAfter = sqlite3_total_changes64(connection);
      const sqlite3_int64 changes = totalChangesAfter == totalChangesBefore
        ? 0
        : sqlite3_changes64(connection);
      const sqlite3_int64 lastInsertRowId = sqlite3_last_insert_rowid(connection);

      JSON::Array::Entries columnsJson;
      columnsJson.reserve(handle->columnNames.size());
      for (const auto& name : handle->columnNames) {
        columnsJson.push_back(name);
      }

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.statement.step"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(statementId)},
          {"rows", rows},
          {"columns", columnsJson},
          {"columnsMeta", meta},
          {"done", done},
          {"changes", changes},
          {"lastInsertRowid", std::to_string(lastInsertRowId)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::reset (
    const ipc::Message::Seq& seq,
    ID statementId,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<StatementHandle> handle = this->getStatement(statementId);

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.reset"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No statement found for the provided id"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      handle->statement.reset();
      handle->statement.clearBindings();
      std::fill(handle->columnRuntimeTypes.begin(), handle->columnRuntimeTypes.end(), String("null"));

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.statement.reset"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(statementId)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void SQLite::finalize (
    const ipc::Message::Seq& seq,
    ID statementId,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      SharedPointer<StatementHandle> handle;

      {
        Lock lock(this->mutex);
        auto it = this->statements.find(statementId);
        if (it == this->statements.end()) {
          handle = nullptr;
        } else {
          handle = it->second;
          this->statements.erase(it);
        }
      }

      if (handle == nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "sqlite.statement.finalize"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "No statement found for the provided id"},
            {"id", std::to_string(statementId)}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      handle->statement.finalize();

      const auto json = JSON::Object::Entries {
        {"source", "sqlite.statement.finalize"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(statementId)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }
}
