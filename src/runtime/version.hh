#ifndef ORO_RUNTIME_VERSION_H
#define ORO_RUNTIME_VERSION_H

#include "string.hh"
#include "config.hh"

namespace oro::runtime::version {
  inline const auto VERSION_FULL_STRING = String(CONVERT_TO_STRING(ORO_RUNTIME_VERSION) " (" CONVERT_TO_STRING(ORO_RUNTIME_VERSION_HASH) ")");
  inline const auto VERSION_HASH_STRING = String(CONVERT_TO_STRING(ORO_RUNTIME_VERSION_HASH));
  inline const auto VERSION_STRING = String(CONVERT_TO_STRING(ORO_RUNTIME_VERSION));
}
#endif
