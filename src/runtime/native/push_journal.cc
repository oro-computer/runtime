#include "push_journal.hh"

#include <sodium.h>
extern "C" {
#if __has_include("sqlite/sqlite3.h")
#include "sqlite/sqlite3.h"
#else
#include <sqlite3.h>
#endif
}
#include <algorithm>
#include <span>

namespace oro::runtime::native {
  namespace {
    bool hexadecimal (const std::string& value, size_t size) {
      return value.size () == size && value.find_first_not_of ("0123456789abcdef") == std::string::npos;
    }
    std::string nonce () {
      unsigned char bytes[16];
      randombytes_buf (bytes, sizeof (bytes));
      char text[33];
      sodium_bin2hex (text, sizeof (text), bytes, sizeof (bytes));
      return text;
    }
    void checked (int result) {
      if (result == SQLITE_OK || result == SQLITE_ROW || result == SQLITE_DONE)
        return;
      if ((result & 255) == SQLITE_BUSY || (result & 255) == SQLITE_LOCKED)
        throw StateError ("EBUSY");
      if ((result & 255) == SQLITE_FULL)
        throw StateError ("ENOSPC");
      if ((result & 255) == SQLITE_CORRUPT || (result & 255) == SQLITE_NOTADB)
        throw StateError ("EBADMSG");
      throw StateError ("EIO");
    }
    struct Database {
      sqlite3* value = nullptr;
      explicit Database (const std::filesystem::path& path) {
        const auto filename = path.u8string ();
        const auto result =
            sqlite3_open_v2 (reinterpret_cast<const char*> (filename.c_str ()), &value,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if (result != SQLITE_OK) {
          sqlite3_close_v2 (value);
          value = nullptr;
          checked (result);
        }
        try {
          checked (sqlite3_busy_timeout (value, 2000));
          exec ("PRAGMA trusted_schema=OFF");
          exec ("PRAGMA journal_mode=DELETE");
          exec ("PRAGMA synchronous=FULL");
        } catch (...) {
          sqlite3_close_v2 (value);
          throw;
        }
      }
      ~Database () {
        sqlite3_close_v2 (value);
      }
      void exec (const char* sql) {
        checked (sqlite3_exec (value, sql, nullptr, nullptr, nullptr));
      }
    };
    struct Statement {
      sqlite3_stmt* value = nullptr;
      Statement (Database& database, const char* sql) {
        const auto result = sqlite3_prepare_v2 (database.value, sql, -1, &value, nullptr);
        if (result != SQLITE_OK) {
          sqlite3_finalize (value);
          checked (result);
        }
      }
      ~Statement () {
        sqlite3_finalize (value);
      }
      void text (int slot, const std::string& text) {
        checked (sqlite3_bind_text (value, slot, text.c_str (), text.size (), SQLITE_TRANSIENT));
      }
      void bytes (int slot, const RecordBytes& bytes) {
        if (bytes.empty ()) {
          checked (sqlite3_bind_zeroblob (value, slot, 0));
        } else {
          checked (sqlite3_bind_blob (value, slot, bytes.data (), bytes.size (), SQLITE_TRANSIENT));
        }
      }
      int step () {
        const auto result = sqlite3_step (value);
        checked (result);
        return result;
      }
      std::string string (int slot) {
        const auto bytes = sqlite3_column_text (value, slot);
        const auto size = sqlite3_column_bytes (value, slot);
        if (!bytes || size > 256)
          throw StateError ("EBADMSG");
        return {reinterpret_cast<const char*> (bytes), static_cast<size_t> (size)};
      }
    };
    struct Transaction {
      Database& database;
      bool active = true;
      explicit Transaction (Database& database) : database (database) {
        database.exec ("BEGIN IMMEDIATE");
      }
      ~Transaction () {
        if (active)
          sqlite3_exec (database.value, "ROLLBACK", nullptr, nullptr, nullptr);
      }
      void commit () {
        if (sqlite3_exec (database.value, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
          throw StateError ("EUNKNOWN");
        active = false;
      }
    };
    struct Meta {
      std::string installation;
      std::string epoch;
    };
    Meta metadata (Database& database, const std::string& application) {
      Statement row (database, "SELECT application,installation,epoch FROM push_journal_meta WHERE id=1");
      if (row.step () != SQLITE_ROW || row.string (0) != application)
        throw StateError ("EBADMSG");
      Meta result{row.string (1), row.string (2)};
      if (!hexadecimal (result.installation, 32) || (!result.epoch.empty () && !hexadecimal (result.epoch, 32)))
        throw StateError ("EBADMSG");
      return result;
    }
    void validate (const PushJournalSnapshot& snapshot) {
      if ((!snapshot.epoch.empty () && !hexadecimal (snapshot.epoch, 32)) || snapshot.reason.size () > 32)
        throw StateError ("EINVAL");
      if (snapshot.ciphertext.size () > PushJournalLedger::maximumArchiveBytes)
        throw StateError ("ENOSPC");
    }
    std::string revision (const Meta& meta, const PushJournalSnapshot& snapshot) {
      validate (snapshot);
      crypto_generichash_state hash;
      crypto_generichash_init (&hash, nullptr, 0, 32);
      const auto field = [&] (std::span<const unsigned char> value) {
        unsigned char size[8];
        const auto length = static_cast<uint64_t> (value.size ());
        for (int n = 0; n < 8; n++)
          size[n] = static_cast<unsigned char> (length >> (8 * n));
        crypto_generichash_update (&hash, size, sizeof (size));
        crypto_generichash_update (&hash, value.data (), value.size ());
      };
      for (const auto& value :
           {std::string ("oro-push-journal-recovery-v1"), meta.installation, snapshot.epoch, snapshot.reason})
        field ({reinterpret_cast<const unsigned char*> (value.data ()), value.size ()});
      field (snapshot.ciphertext);
      unsigned char digest[32];
      crypto_generichash_final (&hash, digest, sizeof (digest));
      char output[65];
      sodium_bin2hex (output, sizeof (output), digest, sizeof (digest));
      return output;
    }
    bool recoverable (const std::string& reason) {
      return reason == "missing-key" || reason == "invalidated-key" || reason == "ciphertext-corrupt" ||
             reason == "journal-format" || reason == "uncertain-write";
    }
  }  // namespace

  PushJournalLedger::PushJournalLedger (const std::filesystem::path& path, const std::string& application)
      : path (path), application (application) {
    if (sodium_init () < 0)
      throw StateError ("EIO");
    if (!path.is_absolute () || path.parent_path ().empty () || application.empty () || application.size () > 200)
      throw StateError ("EINVAL");
    if (std::filesystem::exists (path.parent_path ()) && !std::filesystem::exists (path))
      throw StateError ("EBADMSG");
    if (!std::filesystem::exists (path.parent_path ())) {
      std::filesystem::create_directories (path.parent_path ());
      std::filesystem::permissions (path.parent_path (), std::filesystem::perms::owner_all);
    }
    Database database (path);
    std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    Transaction transaction (database);
    database.exec (
        "CREATE TABLE IF NOT EXISTS push_journal_meta(id INTEGER PRIMARY KEY CHECK(id=1),application TEXT NOT "
        "NULL,installation TEXT NOT NULL,epoch TEXT NOT NULL)");
    database.exec (
        "CREATE TABLE IF NOT EXISTS push_journal_archives(id TEXT PRIMARY KEY,revision TEXT NOT NULL,previous_epoch "
        "TEXT NOT NULL,next_epoch TEXT NOT NULL,reason TEXT NOT NULL,ciphertext BLOB NOT NULL)");
    Statement insert (database,
                      "INSERT OR IGNORE INTO push_journal_meta(id,application,installation,epoch) VALUES(1,?,?,'')");
    insert.text (1, application);
    insert.text (2, nonce ());
    insert.step ();
    metadata (database, application);
    transaction.commit ();
  }
  std::string PushJournalLedger::epoch () const {
    Database database (path);
    return metadata (database, application).epoch;
  }
  PushJournalInspection PushJournalLedger::inspect (const PushJournalSnapshot& snapshot) const {
    Database database (path);
    const auto meta = metadata (database, application);
    if (meta.epoch != snapshot.epoch)
      throw StateError ("EAGAIN");
    return {revision (meta, snapshot), snapshot.reason, snapshot.reason.empty (), recoverable (snapshot.reason)};
  }
  PushJournalReceipt PushJournalLedger::quarantine (const PushJournalSnapshot& snapshot,
                                                    const PushJournalConsent& consent, bool idle) {
    if (!consent.installationScopeConfirmed || !consent.archiveAndStopLocalDeliveryConfirmed)
      throw StateError ("EACCES");
    if (!hexadecimal (consent.expectedRevision, 64) || consent.recoveryId.empty () ||
        consent.recoveryId.size () > 128 ||
        consent.recoveryId.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos)
      throw StateError ("EINVAL");
    Database database (path);
    Transaction transaction (database);
    const auto meta = metadata (database, application);
    {
      Statement previous (database, "SELECT revision,next_epoch FROM push_journal_archives WHERE id=?");
      previous.text (1, consent.recoveryId);
      if (previous.step () == SQLITE_ROW) {
        if (previous.string (0) != consent.expectedRevision || previous.string (1) != meta.epoch)
          throw StateError ("EAGAIN");
        return {consent.recoveryId, meta.epoch, false};
      }
    }
    if (meta.epoch != snapshot.epoch || revision (meta, snapshot) != consent.expectedRevision)
      throw StateError ("EAGAIN");
    if (!recoverable (snapshot.reason))
      throw StateError ("EINVAL");
    if (!idle)
      throw StateError ("EBUSY");
    Statement count (database, "SELECT COUNT(*) FROM push_journal_archives");
    count.step ();
    if (sqlite3_column_int64 (count.value, 0) >= maximumArchives)
      throw StateError ("ENOSPC");
    const auto next = nonce ();
    Statement save (database,
                    "INSERT INTO push_journal_archives(id,revision,previous_epoch,next_epoch,reason,ciphertext) "
                    "VALUES(?,?,?,?,?,?)");
    save.text (1, consent.recoveryId);
    save.text (2, consent.expectedRevision);
    save.text (3, meta.epoch);
    save.text (4, next);
    save.text (5, snapshot.reason);
    save.bytes (6, snapshot.ciphertext);
    save.step ();
    Statement select (database, "UPDATE push_journal_meta SET epoch=? WHERE id=1");
    select.text (1, next);
    select.step ();
    transaction.commit ();
    return {consent.recoveryId, next, true};
  }
}  // namespace oro::runtime::native
