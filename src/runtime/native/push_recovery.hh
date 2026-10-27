#ifndef ORO_RUNTIME_NATIVE_PUSH_RECOVERY_H
#define ORO_RUNTIME_NATIVE_PUSH_RECOVERY_H

#include "records.hh"

namespace oro::runtime::native {
  /** Exact authenticated application account scope; no wildcard discovery. */
  struct PushRecoveryScope {
    std::string authorityRef;
    std::string accountRef;
    std::string kind;
  };
  /** Redacted native recovery metadata. Never contains transport or enrollment secrets. */
  struct PushRecoveryRegistration {
    std::string id;
    std::string kind;
    std::string phase;
    std::string revision;
  };
  /** Reject empty/wildcard scopes before reading encrypted registration state. */
  inline void validatePushRecoveryScope (const PushRecoveryScope& scope) {
    const auto opaque = [] (const std::string& value) {
      return !value.empty () && value.size () <= 128 &&
             value.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                 std::string::npos;
    };
    if (!opaque (scope.authorityRef) || !opaque (scope.accountRef) || scope.kind.empty () || scope.kind.size () > 64 ||
        scope.kind.front () < 'a' || scope.kind.front () > 'z' ||
        scope.kind.find_first_not_of ("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos)
      throw StateError ("EINVAL");
  }
}  // namespace oro::runtime::native
#endif
