#ifndef ORO_RUNTIME_CORE_SERVICES_SQLITE_H
#define ORO_RUNTIME_CORE_SERVICES_SQLITE_H

#include "../../ipc.hh"
#include "../../core.hh"
#include "../../sqlite.hh"
#include "../../filesystem.hh"

namespace oro::runtime::core::services {
  class SQLite : public core::Service {
    public:
      using ID = uint64_t;

      struct Handle {
        ID id = 0;
        sqlite::Database database;
        bool isMemory = false;
        UniquePointer<filesystem::Resource> resource = nullptr;
      };

      struct StatementHandle {
        ID id = 0;
        SharedPointer<Handle> database;
        sqlite::Statement statement;
        Vector<String> columnNames;
        Vector<String> columnDeclTypes;
        Vector<String> columnRuntimeTypes;
      };

      SQLite (const Options& options)
        : core::Service(options)
      {}

      bool stop () override;

      void open (
        const ipc::Message::Seq& seq,
        ID id,
        const String& path,
        int flags,
        const Callback callback
      );

      void close (
        const ipc::Message::Seq& seq,
        ID id,
        const Callback callback
      );

      void exec (
        const ipc::Message::Seq& seq,
        ID id,
        const String& sql,
        bool arrayMode,
        const String& paramsJSON,
        const Callback callback
      );

      void prepare (
        const ipc::Message::Seq& seq,
        ID databaseId,
        const String& sql,
        const Callback callback
      );

      void bind (
        const ipc::Message::Seq& seq,
        ID statementId,
        const String& paramsJSON,
        const Callback callback
      );

      void step (
        const ipc::Message::Seq& seq,
        ID statementId,
        size_t limit,
        bool arrayMode,
        const Callback callback
      );

      void reset (
        const ipc::Message::Seq& seq,
        ID statementId,
        const Callback callback
      );

      void finalize (
        const ipc::Message::Seq& seq,
        ID statementId,
        const Callback callback
      );

    private:
      Mutex mutex;
      Map<ID, SharedPointer<Handle>> handles;
      Map<ID, SharedPointer<StatementHandle>> statements;

      SharedPointer<Handle> getHandle (ID id);
      SharedPointer<StatementHandle> getStatement (ID id);
      ID generateUniqueId (const Map<ID, SharedPointer<StatementHandle>>& map) const;
      void finalizeStatementsForHandle (const SharedPointer<Handle>& handle);
  };
}
#endif
