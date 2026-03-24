#include "tests.hh"

#include "src/runtime/core/state_manager.hh"
#include "src/runtime/env.hh"
#include "src/runtime/filesystem.hh"
#include "src/runtime/crypto.hh"
#include "src/runtime/string.hh"

#include <algorithm>

namespace oro::Tests {
  void state_manager (Harness& t) {
    t.test("StateManager persists scoped origin values", [](auto t) {
      using namespace oro::runtime;

      const auto tempDir = oro::fs::temp_directory_path() /
        ("socket-state-manager-" + std::to_string(crypto::rand64()));

      oro::fs::remove_all(tempDir);
      env::set("ORO_STATE_DIR", tempDir.string());

      context::RuntimeContext context;
      core::StateManager manager(context);

      t.assert(manager.init(), "initializes sqlite-backed state store");
      const String scope = "https://example.test";

      t.assert(manager.putString(scope, "token", "value-1"), "stores first value");

      const auto hash = oro::runtime::string::toLowerCase(
        oro::runtime::crypto::sha1(scope)
      );
      const auto dbPath = tempDir / (hash + ".db");
      t.assert(oro::fs::exists(dbPath), "creates sqlite database per origin");

      auto value = manager.getString(scope, "token");
      t.assert(value.has_value(), "retrieves stored value");
      t.equals(value.value(), "value-1", "value matches stored data");

      t.assert(manager.putString(scope, "token", "value-2"), "updates existing value");
      auto updated = manager.getString(scope, "token");
      t.equals(updated.value(), "value-2", "update persists");

      t.assert(manager.putString(scope, "refresh", "value-3"), "stores second key");
      const auto keys = manager.listKeys(scope);
      t.equals(keys.size(), static_cast<size_t>(2), "lists scoped keys");
      t.assert(std::find(keys.begin(), keys.end(), "token") != keys.end(), "contains token key");
      t.assert(std::find(keys.begin(), keys.end(), "refresh") != keys.end(), "contains refresh key");

      const auto scopes = manager.listScopes();
      t.assert(std::find(scopes.begin(), scopes.end(), scope) != scopes.end(), "origin scope is recorded");

      t.assert(manager.remove(scope, "refresh"), "removes scoped key");
      t.assert(!manager.getString(scope, "refresh").has_value(), "removed key missing");

      t.assert(manager.clear(scope), "clears scoped keys");
      t.assert(!manager.getString(scope, "token").has_value(), "cleared key missing");

      manager.shutdown();

      env::set("ORO_STATE_DIR", "");
      oro::fs::remove_all(tempDir);
    });

    t.test("StateManager rejects non-origin scopes", [](auto t) {
      using namespace oro::runtime;

      const auto tempDir = oro::fs::temp_directory_path() /
        ("socket-state-manager-invalid-" + std::to_string(crypto::rand64()));

      oro::fs::remove_all(tempDir);
      env::set("ORO_STATE_DIR", tempDir.string());

      context::RuntimeContext context;
      core::StateManager manager(context);

      t.assert(manager.init(), "initializes sqlite-backed state store");

      t.assert(!manager.putString("not-an-origin", "token", "value"), "rejects plain string scope");
      t.assert(!manager.getString("not-an-origin", "token").has_value(), "no value stored for invalid scope");

      manager.shutdown();

      env::set("ORO_STATE_DIR", "");
      oro::fs::remove_all(tempDir);
    });

  }
}
