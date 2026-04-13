#ifndef ORO_RUNTIME_BACKGROUND_OPTIONS_H
#define ORO_RUNTIME_BACKGROUND_OPTIONS_H

#include "../platform.hh"

namespace oro::runtime::background {
  using types::Map;
  using types::String;
  using types::Vector;

  enum class TriggerKind {
    None,
    Interval,
    Processing,
    Refresh,
    Unknown
  };

  struct TriggerOptions {
    TriggerKind kind = TriggerKind::None;
    uint64_t minimumIntervalMs = 0;
  };

  struct ServiceOptions {
    String id;
    String entry;
    bool keepAlive = false;
    bool requiredIOS = false;
    Vector<String> permissions;
    TriggerOptions trigger;
    Map<String, String> rawProperties;
  };

  struct Options {
    bool enabled = false;
    String defaultEntry;
    Map<String, ServiceOptions> services;
  };

  Options parse (const Map<String, String>& userConfig);
  TriggerKind parseTriggerKind (const String& value);
}
#endif
