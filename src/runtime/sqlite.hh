#ifndef ORO_RUNTIME_SQLITE_H
#define ORO_RUNTIME_SQLITE_H

#include "platform.hh"

extern "C" {
#include "sqlite/sqlite3.h"
}

namespace oro::runtime::sqlite {
  constexpr int kDefaultOpenFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

  using ExecCallback = Function<int(int, char**, char**)>;

  class Database {
    public:
      Database () = default;
      Database (const String& path, int flags = kDefaultOpenFlags);
      Database (const Database&) = delete;
      Database& operator = (const Database&) = delete;
      Database (Database&&) noexcept;
      Database& operator = (Database&&) noexcept;
      ~Database ();

      bool open (const String& path, int flags = kDefaultOpenFlags);
      bool close ();

      bool isOpen () const;

      sqlite3* handle () const;

      int lastErrorCode () const;
      String lastErrorMessage () const;
      int lastResult () const;
      void setLastResult (int result);

      bool exec (const String& sql, const ExecCallback& callback = {}) const;

    private:
      sqlite3* database = nullptr;
      mutable int lastResultCode = SQLITE_OK;
  };

  class Statement {
    public:
      Statement () = default;
      Statement (Database& db, const String& sql);
      Statement (const Statement&) = delete;
      Statement& operator = (const Statement&) = delete;
      Statement (Statement&&) noexcept;
      Statement& operator = (Statement&&) noexcept;
      ~Statement ();

      bool prepare (Database& db, const String& sql);
      void finalize ();

      int step ();
      bool reset ();

      int columnCount () const;
      String columnText (int index) const;
      const void* columnBlob (int index) const;
      int columnBytes (int index) const;
      int columnInt (int index) const;
      int64_t columnInt64 (int index) const;
      double columnDouble (int index) const;
      sqlite3_stmt* handle () const;

      bool bindNull (int index);
      bool bindInt (int index, int value);
      bool bindInt64 (int index, int64_t value);
      bool bindDouble (int index, double value);
      bool bindText (int index, const String& value);
      bool bindBlob (int index, const void* data, int size);
      bool clearBindings ();

    private:
      sqlite3_stmt* statement = nullptr;
      sqlite3* database = nullptr;
  };

  String errorMessage (int code);
}

#endif
