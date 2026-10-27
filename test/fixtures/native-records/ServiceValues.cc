#include "src/runtime/native/service_values.hh"

#include <cassert>
#include <iostream>
#include <limits>

using namespace oro::runtime;
using namespace oro::runtime::native;

int main () {
  const RecordBytes binary = {0, 1, 127, 128, 255};
  assert (service::bytes (service::bytes (binary)) == binary);
  assert (service::bytes (RecordBytes{}).empty ());
  assert (service::bytes (std::string{}).empty ());
  for (const auto& malformed : {"AA=", "AA+", "AB", "_", " A"}) {
    bool denied = false;
    try {
      service::bytes (std::string (malformed));
    } catch (const StateError& error) {
      denied = error.code == "EBADMSG";
    }
    assert (denied);
  }
  const auto maximum = std::numeric_limits<int64_t>::max ();
  for (const auto revision : {int64_t (0), int64_t (9007199254740993), maximum}) {
    JSON::Object value =
        JSON::Object::Entries{{"revision", std::to_string (revision)},
                              {"commitId", "public-commit"},
                              {"records", JSON::Object (JSON::Object::Entries{{"public", service::bytes (binary)}})}};
    const auto decoded = service::snapshot (value);
    assert (decoded.revision == revision && decoded.commitId == "public-commit" &&
            decoded.records.at ("public") == binary);
  }
  for (const auto& revision : {"-1", "01", "9223372036854775808", "1.5", "1e3", ""}) {
    JSON::Object value =
        JSON::Object::Entries{{"revision", revision}, {"commitId", "public"}, {"records", JSON::Object{}}};
    bool denied = false;
    try {
      service::snapshot (value);
    } catch (const StateError& error) {
      denied = error.code == "EBADMSG";
    }
    assert (denied);
  }
  const CoordinationTicket ticket{"public-scope", "public-slot", "public-generation", maximum};
  JSON::Object value = JSON::Object::Entries{{"ticket", service::ticket (ticket)},
                                             {"operationRef", "public-read"},
                                             {"phase", "unknown"},
                                             {"ownerProcessMatches", false},
                                             {"receipt", service::bytes (binary)}};
  const auto state = service::coordination (value);
  assert (state.ticket.revision == maximum && state.ticket.generation == ticket.generation);
  assert (state.phase == "unknown" && !state.ownerProcessMatches && state.receipt == binary);
  const auto changes = service::changes (RecordChanges{{"remove", std::nullopt}, {"empty", RecordBytes{}}});
  assert (changes.get ("remove").isNull () && changes.get ("empty").as<JSON::String> ().value ().empty ());
  std::cout << "native-service-values-canonical-binary-full-width-revisions-and-uncertain-receipts-passed\n";
}
