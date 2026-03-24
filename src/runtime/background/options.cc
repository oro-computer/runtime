#include "options.hh"
#include "../string.hh"
#include <algorithm>

namespace oro::runtime::background {
  namespace {
    using oro::runtime::string::toLowerCase;
    using oro::runtime::string::trim;
    using oro::runtime::string::parseStringList;

    bool parseBool (const String& value) {
      const auto lowered = toLowerCase(trim(value));
      return lowered == "1" ||
             lowered == "true" ||
             lowered == "yes" ||
             lowered == "on";
    }

    uint64_t parseUInt (const String& value) {
      const auto trimmed = trim(value);
      if (trimmed.size() == 0) {
        return 0;
      }
      try {
        return static_cast<uint64_t>(std::stoull(trimmed, nullptr, 10));
      } catch (...) {
        return 0;
      }
    }

    Vector<String> parseList (const String& value) {
      if (value.find(',') != String::npos) {
        return parseStringList(value, ',');
      }
      return parseStringList(value);
    }

    void hydrateTriggerProperty (
      ServiceOptions& service,
      const String& property,
      const String& value
    ) {
      if (property == "trigger.type" || property == "trigger_type") {
        service.trigger.kind = parseTriggerKind(value);
      } else if (
        property == "trigger.minimum_interval" ||
        property == "trigger.minimumInterval" ||
        property == "trigger_minimum_interval"
      ) {
        service.trigger.minimumIntervalMs = parseUInt(value);
      }
    }

    void hydratePlatformProperty (
      ServiceOptions& service,
      const String& property,
      const String& value
    ) {
      // Preserve for platform-specific overrides; parsed later by platform code.
      service.rawProperties.insert_or_assign(property, value);
    }
  }

  TriggerKind parseTriggerKind (const String& value) {
    const auto lowered = toLowerCase(trim(value));
    if (lowered == "interval") {
      return TriggerKind::Interval;
    } else if (lowered == "processing") {
      return TriggerKind::Processing;
    } else if (lowered == "refresh") {
      return TriggerKind::Refresh;
    } else if (lowered == "on_demand" || lowered == "ondemand" || lowered == "manual") {
      return TriggerKind::None;
    }

    return TriggerKind::Unknown;
  }

  Options parse (const Map<String, String>& userConfig) {
    Options options;
    bool enabledSet = false;

    for (const auto& tuple : userConfig) {
      const auto& key = tuple.first;
      const auto& value = tuple.second;

      String normalized = key;
      std::replace(normalized.begin(), normalized.end(), '.', '_');

      if (!normalized.starts_with("background_")) {
        continue;
      }

      const auto suffix = normalized.substr(String("background_").size());

      if (suffix == "enabled") {
        options.enabled = parseBool(value);
        enabledSet = true;
        continue;
      }

      if (suffix == "default_entry" || suffix == "default.entry") {
        options.defaultEntry = trim(value);
        continue;
      }

      if (!suffix.starts_with("service_")) {
        // Preserve unrecognised background namespace entries.
        continue;
      }

      const auto remainder = suffix.substr(String("service_").size());

      if (remainder.size() == 0) {
        continue;
      }

      String property;
      String id;

      const auto platformMarker = String("_platform_");
      const auto platformPosition = remainder.find(platformMarker);

      if (platformPosition != String::npos) {
        id = remainder.substr(0, platformPosition);
        auto platformSuffix = remainder.substr(platformPosition + platformMarker.size());
        if (platformSuffix.size() > 0) {
          std::replace(platformSuffix.begin(), platformSuffix.end(), '_', '.');
          property = "platform." + platformSuffix;
        } else {
          property = "platform";
        }
      } else {
        struct PropertySuffix {
          const char* suffix;
          const char* canonical;
        };

        static constexpr PropertySuffix propertySuffixes[] = {
          {"_trigger_minimum_interval", "trigger.minimum_interval"},
          {"_trigger_minimumInterval", "trigger.minimumInterval"},
          {"_trigger_type", "trigger.type"},
          {"_keep_alive", "keep_alive"},
          {"_keepAlive", "keepAlive"},
          {"_permissions", "permissions"},
          {"_required", "required"},
          {"_entry", "entry"}
        };

        for (const auto& def : propertySuffixes) {
          const String suffixView = def.suffix;
          if (remainder.size() > suffixView.size() &&
              remainder.ends_with(suffixView)) {
            id = remainder.substr(0, remainder.size() - suffixView.size());
            property = def.canonical;
            break;
          }
        }

        if (property.empty()) {
          const auto underscore = remainder.find('_');
          if (underscore != String::npos && underscore < remainder.size() - 1) {
            id = remainder.substr(0, underscore);
            property = remainder.substr(underscore + 1);
          }
        }
      }

      if (id.size() == 0 || property.size() == 0) {
        continue;
      }

      auto& service = options.services[id];
      service.id = id;
      service.rawProperties.insert_or_assign(property, value);

      if (property == "entry") {
        service.entry = trim(value);
      } else if (property == "keep_alive" || property == "keepAlive") {
        service.keepAlive = parseBool(value);
      } else if (property == "permissions") {
        const auto permissions = parseList(value);
        for (const auto& permission : permissions) {
          const auto trimmed = trim(permission);
          if (trimmed.size() > 0) {
            service.permissions.push_back(trimmed);
          }
        }
      } else if (property == "required") {
        const auto targets = parseList(value);
        for (const auto& target : targets) {
          if (toLowerCase(trim(target)) == "ios") {
            service.requiredIOS = true;
          }
        }
      } else if (property.starts_with("trigger")) {
        hydrateTriggerProperty(service, property, value);
      } else if (property.starts_with("platform")) {
        hydratePlatformProperty(service, property, value);
      }
    }

    if (!enabledSet && (!options.services.empty() || options.defaultEntry.size() > 0)) {
      options.enabled = true;
    }

    return options;
  }
}
