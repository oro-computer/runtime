#include "android_services.hh"
#include "service_values.hh"
#include <sodium.h>
#include <charconv>

namespace oro::runtime::native::service {
  std::string bytes (const RecordBytes& input) {
    if (input.size () > 1048576)
      throw StateError ("E2BIG");
    std::string encoded (sodium_base64_ENCODED_LEN (input.size (), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
    sodium_bin2base64 (encoded.data (), encoded.size (), input.data (), input.size (), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    encoded.resize (encoded.size () - 1);
    return encoded;
  }
  RecordBytes bytes (const std::string& input) {
    if (input.size () > 1398102)
      throw StateError ("E2BIG");
    RecordBytes result (input.size ());
    size_t size = 0;
    if (sodium_base642bin (result.data (), result.size (), input.data (), input.size (), nullptr, &size, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING))
      throw StateError ("EBADMSG");
    result.resize (size);
    if (bytes (result) != input)
      throw StateError ("EBADMSG");
    return result;
  }
  int64_t revision (const JSON::Object& value, const std::string& key) {
    const auto text = value.get (key).as<JSON::String> ().value ();
    int64_t number = 0;
    const auto [last, error] = std::from_chars (text.data (), text.data () + text.size (), number);
    if (error != std::errc {} || last != text.data () + text.size () || number < 0 || std::to_string (number) != text)
      throw StateError ("EBADMSG");
    return number;
  }
  JSON::Object changes (const RecordChanges& updates) {
    JSON::Object result;
    for (const auto& [key, value] : updates)
      result[key] = value ? JSON::Any (bytes (*value)) : JSON::Any (nullptr);
    return result;
  }
  RecordSnapshot snapshot (const JSON::Object& value) {
    RecordSnapshot result;
    result.revision = revision (value, "revision");
    result.commitId = value.get ("commitId").as<JSON::String> ().value ();
    const auto records = value.get ("records").as<JSON::Object> ();
    for (const auto& [key, encoded] : records.value ())
      result.records[key] = bytes (encoded.as<JSON::String> ().value ());
    return result;
  }
  JSON::Object ticket (const CoordinationTicket& value) {
    return JSON::Object::Entries{{"scope", value.scope}, {"slot", value.slot}, {"generation", value.generation},
                                {"revision", std::to_string (value.revision)}};
  }
  CoordinationState coordination (const JSON::Object& value) {
    const auto input = value.get ("ticket").as<JSON::Object> ();
    return {{input.get ("scope").as<JSON::String> ().value (), input.get ("slot").as<JSON::String> ().value (),
             input.get ("generation").as<JSON::String> ().value (), revision (input, "revision")},
            value.get ("operationRef").as<JSON::String> ().value (), value.get ("phase").as<JSON::String> ().value (),
            value.get ("ownerProcessMatches").as<JSON::Boolean> ().value (),
            bytes (value.get ("receipt").as<JSON::String> ().value ())};
  }
}  // namespace oro::runtime::native::service

namespace oro::runtime::native {
  RecordSnapshot AndroidV1Records::read () const {
    return service::snapshot (JSON::parse (request ("read", "{}")).as<JSON::Object> ());
  }
  NativeV1RecordStatus AndroidV1Records::status () const {
    const auto value = JSON::parse (request ("status", "{}")).as<JSON::Object> ();
    return {{service::revision (value, "revision"), value.get ("keyPresent").as<JSON::Boolean> ().value (),
            value.get ("requiresRecovery").as<JSON::Boolean> ().value ()}, value.get ("commitId").as<JSON::String> ().value ()};
  }
  RecordSnapshot AndroidV1Records::apply (int64_t revision, const RecordChanges& changes, const std::string& commitId) {
    const auto value = JSON::Object::Entries{{"revision", std::to_string (revision)}, {"changes", service::changes (changes)},
                                            {"commitId", commitId}};
    return service::snapshot (JSON::parse (request ("apply", JSON::Object (value).str ())).as<JSON::Object> ());
  }
  RecordSnapshot AndroidV1Records::migrate (const std::string& id, const std::string& digest, int64_t revision,
                                            const RecordValues& values) {
    RecordChanges updates;
    for (const auto& [key, value] : values)
      updates[key] = value;
    JSON::Object options = JSON::Object::Entries{{"revision", std::to_string (revision)}, {"changes", service::changes (updates)},
                                                {"migrationId", id}, {"sourceSha256", digest}};
    return service::snapshot (JSON::parse (request ("migrate", options.str ())).as<JSON::Object> ());
  }
  std::optional<CoordinationState> AndroidV1Records::get (const std::string& slot) const {
    JSON::Object options = JSON::Object::Entries{{"slot", slot}};
    const auto value = JSON::parse (request ("get", options.str ()));
    if (value.type == JSON::Type::Null)
      return std::nullopt;
    return service::coordination (value.as<JSON::Object> ());
  }
  CoordinationState AndroidV1Records::prepare (const std::string& slot, const std::string& operation, int64_t revision) {
    JSON::Object options = JSON::Object::Entries{{"slot", slot}, {"operationRef", operation}, {"revision", std::to_string (revision)}};
    return service::coordination (JSON::parse (request ("prepare", options.str ())).as<JSON::Object> ());
  }
  CoordinationState AndroidV1Records::transition (const std::string& action, const CoordinationTicket& ticket,
                                                  const RecordChanges& changes, const RecordBytes& receipt, bool applied) {
    JSON::Object options = JSON::Object::Entries{{"ticket", service::ticket (ticket)}, {"changes", service::changes (changes)},
                                                {"receipt", service::bytes (receipt)}, {"applied", applied}};
    return service::coordination (JSON::parse (request (action, options.str ())).as<JSON::Object> ());
  }
  CoordinationState AndroidV1Records::begin (const CoordinationTicket& ticket) { return transition ("begin", ticket); }
  CoordinationState AndroidV1Records::unknown (const CoordinationTicket& ticket) { return transition ("unknown", ticket); }
  CoordinationState AndroidV1Records::abort (const CoordinationTicket& ticket) { return transition ("abort", ticket); }
  CoordinationState AndroidV1Records::recover (const CoordinationTicket& ticket) { return transition ("recover", ticket); }
  CoordinationState AndroidV1Records::commit (const CoordinationTicket& ticket, const RecordChanges& changes, const RecordBytes& receipt) {
    return transition ("commit", ticket, changes, receipt);
  }
  CoordinationState AndroidV1Records::resolve (const CoordinationTicket& ticket, bool applied, const RecordChanges& changes,
                                               const RecordBytes& receipt) {
    return transition ("resolve", ticket, changes, receipt, applied);
  }
  RecordSnapshot AndroidV1Records::release (const CoordinationTicket& ticket) {
    JSON::Object options = JSON::Object::Entries{{"ticket", service::ticket (ticket)}};
    return service::snapshot (JSON::parse (request ("release", options.str ())).as<JSON::Object> ());
  }
}  // namespace oro::runtime::native
