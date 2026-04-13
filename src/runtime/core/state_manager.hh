#ifndef ORO_RUNTIME_CORE_STATE_MANAGER_H
#define ORO_RUNTIME_CORE_STATE_MANAGER_H

#include "../platform.hh"
#include "../context.hh"
#include "../sqlite.hh"
#include "../bytes.hh"

#include <optional>

namespace oro::runtime::core {
  using types::Atomic;
  using types::Map;
  using types::Mutex;
  using types::Path;
  using types::String;
  using types::Vector;

  class StateManager {
    public:
      struct Entry {
        bytes::Buffer value;
        uint64_t updatedAt = 0;
      };

      struct ScopedDatabase {
        sqlite::Database database;
        Path path;
        bool initialized = false;
      };

      explicit StateManager(context::RuntimeContext& context);
      StateManager () = delete;
      StateManager (const StateManager&) = delete;
      StateManager& operator = (const StateManager&) = delete;
      StateManager (StateManager&&) = delete;
      StateManager& operator = (StateManager&&) = delete;
      ~StateManager ();

      bool init ();
      void shutdown ();

      bool isReady () const;

      // Scope values must be serialized origins (scheme://host[:port]).
      bool put (const String& scope, const String& key, const bytes::Buffer& value);
      bool putString (const String& scope, const String& key, const String& value);

      std::optional<Entry> get (const String& scope, const String& key);
      std::optional<String> getString (const String& scope, const String& key);

      bool remove (const String& scope, const String& key);
      bool clear (const String& scope);

      Vector<String> listScopes ();
      Vector<String> listKeys (const String& scope);

      const Path& databasePath () const;

    private:
      bool normalizeScope (const String& scope, String& normalized) const;
      ScopedDatabase* ensureDatabaseForScope (const String& normalizedScope);
      bool upsertValue (
        ScopedDatabase* scopedDatabase,
        const String& key,
        const bytes::Buffer& value,
        uint64_t timestamp
      );
      bool initializeLocked ();
      bool ensureInitializedLocked ();
      bool runMigrationsForDatabase (sqlite::Database& db, const String& origin);
      uint64_t nowMilliseconds () const;

      Mutex mutex;
      context::RuntimeContext& context;
      Path stateDirectory;
      Map<String, ScopedDatabase> scopedDatabases;
      Atomic<bool> ready = false;
  };
}

#endif
