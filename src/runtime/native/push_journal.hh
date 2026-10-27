#ifndef ORO_RUNTIME_NATIVE_PUSH_JOURNAL_H
#define ORO_RUNTIME_NATIVE_PUSH_JOURNAL_H

#include "records.hh"

namespace oro::runtime::native {
  struct PushJournalSnapshot {
    std::string epoch;
    std::string reason;
    RecordBytes ciphertext;
  };
  struct PushJournalInspection {
    std::string revision;
    std::string reason;
    bool readable = false;
    bool recoverable = false;
  };
  struct PushJournalConsent {
    std::string expectedRevision;
    std::string recoveryId;
    bool installationScopeConfirmed = false;
    bool archiveAndStopLocalDeliveryConfirmed = false;
  };
  struct PushJournalReceipt {
    std::string recoveryId;
    std::string epoch;
    bool changed = false;
  };

  /** Private installation selector and immutable ciphertext archives; independent of the lost encryption key. */
  class PushJournalLedger {
   public:
    static constexpr size_t maximumArchiveBytes = 4 * 1024 * 1024;
    static constexpr size_t maximumArchives = 16;
    /** The database must have a dedicated directory. Existing directory + missing selector fails closed. */
    PushJournalLedger (const std::filesystem::path&, const std::string& application);
    std::string epoch () const;
    PushJournalInspection inspect (const PushJournalSnapshot&) const;
    /** Caller holds its transport lock and has joined/quarantined old work before allowing a switch. */
    PushJournalReceipt quarantine (const PushJournalSnapshot&, const PushJournalConsent&, bool idle);

   private:
    std::filesystem::path path;
    std::string application;
  };
}  // namespace oro::runtime::native
#endif
