#include "src/runtime/native/push_journal.hh"
#include <sqlite3.h>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace oro::runtime::native;
template <class F>
void fails (const std::string& code, F work) {
  try {
    work ();
    assert (false);
  } catch (const StateError& error) {
    assert (error.code == code);
  }
}
int main (int argc, char** argv) {
  assert (argc == 2);
  const auto path = std::filesystem::path (argv[1]) / "installation" / "selector.sqlite";
  PushJournalLedger ledger (path, "public.application");
  PushJournalSnapshot damaged{"", "missing-key", {0, 1, 2, 255, 3}};
  const auto initial = ledger.inspect (damaged);
  assert (!initial.readable && initial.recoverable && initial.revision.size () == 64);
  PushJournalConsent consent{initial.revision, "public-recovery-1", false, false};
  fails ("EACCES", [&] { ledger.quarantine (damaged, consent, true); });
  consent.installationScopeConfirmed = true;
  fails ("EACCES", [&] { ledger.quarantine (damaged, consent, true); });
  consent.archiveAndStopLocalDeliveryConfirmed = true;
  fails ("EBUSY", [&] { ledger.quarantine (damaged, consent, false); });
  auto changed = damaged;
  changed.ciphertext.push_back (4);
  fails ("EAGAIN", [&] { ledger.quarantine (changed, consent, true); });
  const auto receipt = ledger.quarantine (damaged, consent, true);
  assert (receipt.changed && receipt.epoch.size () == 32 && ledger.epoch () == receipt.epoch);
  PushJournalSnapshot fresh{receipt.epoch, "", {0}};
  assert (ledger.inspect (fresh).readable && !ledger.inspect (fresh).recoverable);
  const auto retry = ledger.quarantine (fresh, consent, false);
  assert (!retry.changed && retry.epoch == receipt.epoch);
  auto other = consent;
  other.recoveryId = "public-recovery-2";
  fails ("EAGAIN", [&] { ledger.quarantine (damaged, other, true); });
  fails ("EINVAL", [&] { ledger.quarantine (fresh, {ledger.inspect (fresh).revision, "readable", true, true}, true); });
  PushJournalSnapshot denied{receipt.epoch, "provider-unavailable", {1}};
  assert (!ledger.inspect (denied).recoverable);
  fails ("EINVAL", [&] { ledger.quarantine (denied, {ledger.inspect (denied).revision, "denied", true, true}, true); });
  PushJournalLedger reopened (path, "public.application");
  assert (reopened.epoch () == receipt.epoch);
  fails ("EBADMSG", [&] { PushJournalLedger foreign (path, "other.application"); });
  PushJournalLedger installation (std::filesystem::path (argv[1]) / "other" / "selector.sqlite", "public.application");
  assert (installation.inspect (damaged).revision != initial.revision);
  fails ("EAGAIN", [&] { installation.quarantine (damaged, consent, true); });
  sqlite3* db = nullptr;
  assert (sqlite3_open (path.string ().c_str (), &db) == SQLITE_OK);
  sqlite3_stmt* row = nullptr;
  assert (sqlite3_prepare_v2 (db, "SELECT ciphertext FROM push_journal_archives WHERE id='public-recovery-1'", -1, &row,
                              nullptr) == SQLITE_OK);
  assert (sqlite3_step (row) == SQLITE_ROW);
  const auto value = static_cast<const unsigned char*> (sqlite3_column_blob (row, 0));
  assert (RecordBytes (value, value + sqlite3_column_bytes (row, 0)) == damaged.ciphertext);
  sqlite3_finalize (row);
  assert (sqlite3_exec (db,
                        "CREATE TRIGGER fail_switch BEFORE UPDATE ON push_journal_meta BEGIN SELECT "
                        "RAISE(ABORT,'public injected failure'); END",
                        nullptr, nullptr, nullptr) == SQLITE_OK);
  PushJournalSnapshot corrupt{receipt.epoch, "ciphertext-corrupt", {42}};
  const auto corruptRevision = ledger.inspect (corrupt).revision;
  fails ("EIO", [&] { ledger.quarantine (corrupt, {corruptRevision, "rollback", true, true}, true); });
  assert (ledger.epoch () == receipt.epoch);
  assert (sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM push_journal_archives", -1, &row, nullptr) == SQLITE_OK);
  assert (sqlite3_step (row) == SQLITE_ROW && sqlite3_column_int (row, 0) == 1);
  sqlite3_finalize (row);
  sqlite3_close (db);
  const auto absent = std::filesystem::path (argv[1]) / "missing-selector";
  std::filesystem::create_directories (absent);
  fails ("EBADMSG", [&] { PushJournalLedger missing (absent / "selector.sqlite", "public.application"); });
  auto large = damaged;
  large.ciphertext.resize (PushJournalLedger::maximumArchiveBytes + 1);
  fails ("ENOSPC", [&] { installation.inspect (large); });
  for (size_t index = 0; index < PushJournalLedger::maximumArchives; index++) {
    const PushJournalSnapshot current{installation.epoch (), "journal-format", {static_cast<unsigned char> (index)}};
    installation.quarantine (
        current, {installation.inspect (current).revision, "archive-" + std::to_string (index), true, true}, true);
  }
  const PushJournalSnapshot full{installation.epoch (), "missing-key", {42}};
  const auto fullEpoch = installation.epoch ();
  fails ("ENOSPC", [&] {
    installation.quarantine (full, {installation.inspect (full).revision, "archive-overflow", true, true}, true);
  });
  assert (installation.epoch () == fullEpoch);
  std::cout << "push-journal-consent-ciphertext-retention-stale-retry-rollback-and-installation-fences-passed\n";
}
