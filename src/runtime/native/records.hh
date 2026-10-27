#ifndef ORO_RUNTIME_NATIVE_RECORDS_H
#define ORO_RUNTIME_NATIVE_RECORDS_H

#include <oro/platform.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace oro::runtime::native {
  using RecordBytes = std::vector<unsigned char>;
  using RecordValues = std::map<std::string, RecordBytes>;
  using RecordChanges = std::map<std::string, std::optional<RecordBytes>>;
  using RecordKey = std::array<unsigned char, 32>;

  struct StateError : public std::runtime_error {
    std::string code;
    explicit StateError(const std::string &code) : std::runtime_error(code), code(code) {}
  };

  /** Trusted native-only OS key provider. An unavailable/locked provider must throw, never create a file fallback. */
  class RecordKeyStore {
    public:
    virtual ~RecordKeyStore() = default;
    virtual std::optional<RecordKey> get (const std::string &scope, char slot) = 0;
    virtual void put (const std::string &scope, char slot, const RecordKey &) = 0;
    virtual void remove (const std::string &scope, char slot) = 0;
  };

  struct RecordIdentity {
    std::string authority;
    std::string account;
    std::string identity;
  };

  struct RecordSnapshot {
    int64_t revision = 0;
    std::string commitId;
    RecordValues records;
  };

  struct RecordStatus {
    int64_t revision = 0;
    bool keyPresent = false;
    bool requiresRecovery = false;
  };

  /** Blocking native-only SQLite CAS. The key store and its failures are part of the contract. */
  class RecordStore {
    public:
    static constexpr size_t maximumScopes = 256;
    static constexpr size_t maximumRecords = 128;
    static constexpr size_t maximumValueBytes = 65536;
    static constexpr size_t maximumBytes = 262144;

    RecordStore(const std::filesystem::path &database, std::shared_ptr<RecordKeyStore>, const std::string &name,
                const RecordIdentity &);
    RecordSnapshot read () const;
    RecordStatus status () const;
    RecordSnapshot apply (int64_t expectedRevision, const RecordChanges &, const std::string &commitId = "");
    RecordSnapshot migrate (const std::string &id, const std::string &sourceSha256, int64_t expectedRevision,
                            const RecordValues &);
    RecordSnapshot reset (int64_t expectedRevision);
    void collectRetiredKey ();
    const std::string &scope () const {
      return this->scopeId;
    }

    private:
    friend class RecordCoordinator;
    friend class DesktopPush;
    RecordBytes opaqueArchive () const;
    std::filesystem::path database;
    std::shared_ptr<RecordKeyStore> keys;
    std::string scopeId;
    RecordSnapshot inspect () const;
    RecordSnapshot mutate (int64_t expectedRevision, const std::function<bool(RecordValues &)> &,
                           const std::string &commitId = "", bool reset = false);
  };

  struct CoordinationTicket {
    std::string scope;
    std::string slot;
    std::string generation;
    int64_t revision = 0;
  };

  struct CoordinationState {
    CoordinationTicket ticket;
    std::string operationRef;
    std::string phase;
    bool ownerProcessMatches = false;
    RecordBytes receipt;
  };

  /** Local transaction receipts never prove or replay an external operation. */
  class RecordCoordinator {
    public:
    explicit RecordCoordinator(RecordStore store) : store(std::move(store)) {}
    std::optional<CoordinationState> get (const std::string &slot) const;
    CoordinationState prepare (const std::string &slot, const std::string &operationRef, int64_t revision);
    CoordinationState begin (const CoordinationTicket &);
    CoordinationState unknown (const CoordinationTicket &);
    CoordinationState abort (const CoordinationTicket &);
    CoordinationState commit (const CoordinationTicket &, const RecordChanges &, const RecordBytes &receipt = {});
    CoordinationState resolve (const CoordinationTicket &, bool applied, const RecordChanges & = {},
                               const RecordBytes &receipt = {});
    CoordinationState recover (const CoordinationTicket &);
    RecordSnapshot release (const CoordinationTicket &);

    private:
    RecordStore store;
    CoordinationState transition (const CoordinationTicket &, const std::vector<std::string> &phases,
                                  const std::string &phase, const RecordChanges & = {},
                                  const RecordBytes &receipt = {});
  };
} // namespace oro::runtime::native
#endif
