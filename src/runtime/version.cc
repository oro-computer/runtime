#include "version.hh"

namespace oro::runtime::version {
  const String VERSION_FULL_STRING = String(
    CONVERT_TO_STRING(ORO_RUNTIME_VERSION) " ("
    CONVERT_TO_STRING(ORO_RUNTIME_VERSION_HASH) ")"
  );
  const String VERSION_HASH_STRING = String(
    CONVERT_TO_STRING(ORO_RUNTIME_VERSION_HASH)
  );
  const String VERSION_STRING = String(
    CONVERT_TO_STRING(ORO_RUNTIME_VERSION)
  );
}
