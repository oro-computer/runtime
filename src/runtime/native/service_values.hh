#ifndef ORO_RUNTIME_NATIVE_SERVICE_VALUES_H
#define ORO_RUNTIME_NATIVE_SERVICE_VALUES_H
#include "records.hh"
#include "../json.hh"
namespace oro::runtime::native::service {
  std::string bytes (const RecordBytes&);
  RecordBytes bytes (const std::string&);
  JSON::Object changes (const RecordChanges&);
  RecordSnapshot snapshot (const JSON::Object&);
  JSON::Object ticket (const CoordinationTicket&);
  CoordinationState coordination (const JSON::Object&);
}  // namespace oro::runtime::native::service
#endif
