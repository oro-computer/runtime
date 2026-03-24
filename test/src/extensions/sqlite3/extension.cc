#include <oro/extension.h>

extern "C" {
  #include <sqlite3.h>
};

#include <string>
#include <map>

using ID = std::string;
using DatabaseMap = std::map<ID, sqlite3*>;

DatabaseMap databases;

void onexec (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
) {
  auto id = oapi_ipc_message_get(message, "id");
  auto query = oapi_ipc_message_get(message, "query");
  auto result = oapi_ipc_result_create(context, message);

  if (query == nullptr || std::string(query).size() == 0) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_string_create(context, "Missing 'query' in parameters")
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  if (id == nullptr) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_string_create(context, "Missing 'id' in parameters")
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  if (!databases.contains(id)) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "type",
      oapi_json_string_create(context, "NotFoundError")
    );
    oapi_json_object_set(
      err,
      "message",
      oapi_json_string_create(context, "Database not found")
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  auto db = databases[id];

  // oapi_printf(context, "query: %s", query);
  sqlite3_stmt* statement;
  if (sqlite3_prepare_v3(db, query, -1, 0, &statement, nullptr)) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_string_create(context, sqlite3_errmsg(db))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  auto rows = oapi_json_array_create(context);
  int status = 0;
  int items = 0;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    const int columns = sqlite3_data_count(statement);
    auto row = oapi_json_object_create(context);
    if (columns > 0) {
      items++;
    }

    for (int i = 0; i < columns;  ++i) {
      const char* name = sqlite3_column_name(statement, i);
      switch (sqlite3_column_type(statement, i)) {
        case SQLITE_INTEGER: {
          const int value = sqlite3_column_int(statement, i);
          oapi_json_object_set(
            row,
            name,
            oapi_json_number_create(context, value)
          );
          break;
        }

        case SQLITE_FLOAT: {
          const double value = sqlite3_column_double(statement, i);
          oapi_json_object_set(
            row,
            name,
            oapi_json_any(oapi_json_number_create(context, value))
          );
          break;
        }

        case SQLITE_TEXT: {
          const unsigned char* value = sqlite3_column_text(statement, i);
          oapi_json_object_set(
            row,
            name,
            oapi_json_any(oapi_json_string_create(context, (const char*) value))
          );
          break;
        }

        case SQLITE_BLOB: {
          // not supported
          break;
        }

        case SQLITE_NULL: {
          oapi_json_object_set(row, name, nullptr);
          break;
        }
      }
    }

    oapi_json_array_push(rows, oapi_json_any(row));
  }

  sqlite3_finalize(statement);
  if (status != SQLITE_DONE) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, sqlite3_errmsg(db)))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  oapi_ipc_result_set_json_data(result, oapi_json_any(rows));
  oapi_ipc_reply(result);
}

void onopen (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
) {
  auto path = oapi_ipc_message_get(message, "path");
  auto result = oapi_ipc_result_create(context, message);

  if (path == nullptr) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, "'path' is required"))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    return;
  }

  sqlite3* db;
  auto id = std::to_string(oapi_rand64());

  oapi_printf(context, "Opening database '%s'", path);

  if (sqlite3_open(path, &db)) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, sqlite3_errmsg(db)))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    if (sqlite3_close(db)) {
      auto err = oapi_json_object_create(context);
      oapi_json_object_set(
        err,
        "message",
        oapi_json_any(oapi_json_string_create(context, sqlite3_errmsg(db)))
      );
      oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    }
  } else {
    auto data = oapi_json_object_create(context);
    oapi_json_object_set(
      data,
      "id",
      oapi_json_any(oapi_json_string_create(context, id.c_str()))
    );

    oapi_json_object_set(
      data,
      "path",
      oapi_json_any(oapi_json_string_create(context, sqlite3_db_filename(db, nullptr)))
    );

    oapi_ipc_result_set_json_data(result, oapi_json_any(data));
    databases[id] = db;
  }

  oapi_ipc_reply(result);
}

void onclose (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
) {
  auto id = oapi_ipc_message_get(message, "id");
  auto result = oapi_ipc_result_create(context, message);

  if (id == nullptr) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, "Missing 'id' in parameters"))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  if (!databases.contains(id)) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "type",
      oapi_json_any(oapi_json_string_create(context, "NotFoundError"))
    );
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, "Database not found"))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  auto db = databases[id];
  if (sqlite3_close(db)) {
    auto err = oapi_json_object_create(context);
    oapi_json_object_set(
      err,
      "message",
      oapi_json_any(oapi_json_string_create(context, sqlite3_errmsg(db)))
    );
    oapi_ipc_result_set_json_error(result, oapi_json_any(err));
    oapi_ipc_reply(result);
    return;
  }

  databases.erase(id);
  oapi_ipc_reply(result);
}

bool initialize (oapi_context_t* context, const void *data) {
  oapi_ipc_router_map(context, "sqlite3.open", onopen, data);
  oapi_ipc_router_map(context, "sqlite3.close", onclose, data);
  oapi_ipc_router_map(context, "sqlite3.exec", onexec, data);
  return true;
}

bool deinitialize (oapi_context_t* context, const void *data) {
  return true;
}

ORO_RUNTIME_REGISTER_EXTENSION(
  "sqlite3", // name
  initialize, // initializer
  deinitialize,
  "a simple sqlite3 binding", // description
);
