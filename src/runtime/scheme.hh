#ifndef ORO_RUNTIME_SCHEME_H
#define ORO_RUNTIME_SCHEME_H

#include "string.hh"

namespace oro::runtime::scheme {
  static constexpr const char* PRIMARY = "oro";

  inline bool isRuntimeScheme (const String& scheme) {
    return scheme == PRIMARY;
  }

  inline bool startsWithRuntimeSpecifier (const String& value) {
    return value.starts_with(String(PRIMARY) + ":");
  }

  inline bool startsWithRuntimeURL (const String& value) {
    return value.starts_with(String(PRIMARY) + "://");
  }

  inline String replaceScheme (
    const String& value,
    const String& scheme,
    bool isURL = false
  ) {
    if (value.empty()) {
      return value;
    }

    const auto delimiter = isURL ? "://" : ":";
    const auto desired = scheme + String(delimiter);

    const auto primaryPrefix = String(PRIMARY) + delimiter;
    if (value.starts_with(primaryPrefix)) {
      if (scheme == PRIMARY) {
        return value;
      }

      return desired + value.substr(primaryPrefix.size());
    }

    return value;
  }

  inline String canonicalURL (const String& value) {
    return replaceScheme(value, PRIMARY, true);
  }

  inline bool matchesBundleURL (const String& value, const String& bundle) {
    if (bundle.empty()) {
      return false;
    }

    const auto lowered = string::toLowerCase(bundle);
    const auto primaryPrefix = String(PRIMARY) + "://";

    return (
      value.starts_with(primaryPrefix + bundle) ||
      value.starts_with(primaryPrefix + lowered)
    );
  }
}

#endif
