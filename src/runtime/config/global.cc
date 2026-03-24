#include "../config.hh"
#include "../filesystem.hh"
#include "../ini.hh"
#include "../string.hh"
#include "../webview/tls_pins.hh"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <optional>

using oro::runtime::InputFileStream;
using oro::runtime::StringStream;
using oro::runtime::Path;
using oro::runtime::String;
using oro::runtime::Map;
using oro::runtime::Vector;
using oro::runtime::filesystem::Resource;

namespace {
  using Candidate = std::pair<Path, oro::runtime::config::UserConfigFormat>;

  inline std::optional<Path> envPath (const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
      return Path(value);
    }
    return std::nullopt;
  }

  inline oro::runtime::config::UserConfigFormat detectFormat (const Path& path) {
    const auto extension = oro::runtime::string::toLowerCase(path.extension().string());
    if (extension == ".toml") {
      return oro::runtime::config::UserConfigFormat::Toml;
    }
    return oro::runtime::config::UserConfigFormat::Ini;
  }

  inline Vector<Path> defaultSearchOrder (
    const Path& resourcesPath,
    const String& basename
  ) {
    return Vector<Path> {
      Resource::getResourcePath(Path(basename)),
      resourcesPath / basename,
      resourcesPath.parent_path() / basename,
      oro::fs::current_path() / basename
    };
  }

  inline std::optional<Path> firstExisting (const Vector<Path>& candidates) {
    for (const auto& candidate : candidates) {
      if (candidate.empty()) {
        continue;
      }

      std::error_code ec;
      if (oro::fs::is_regular_file(candidate, ec)) {
        return candidate;
      }
    }
    return std::nullopt;
  }

  inline String readFileContents (const Path& path) {
    InputFileStream stream(path);
    if (!stream.is_open()) {
      return "";
    }

    StringStream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
  }

  void warnTlsPinsConfigIssues (const String& origin, const Map<String, String>& config) {
    const auto warnIssues = [&](const char* primary, const char* alias, const char* label) {
      const bool primarySet = config.contains(primary) && !config.at(primary).empty();
      const bool aliasSet = config.contains(alias) && !config.at(alias).empty();

      if (!primarySet && !aliasSet) {
        return;
      }

      if (primarySet && aliasSet && config.at(primary) != config.at(alias)) {
        std::fprintf(
          stderr,
          "warn: %s: config keys '%s' and '%s' differ; using '%s'.\n",
          origin.c_str(),
          primary,
          alias,
          primary
        );
      }

      const auto& value = primarySet
        ? config.at(primary)
        : config.at(alias);

      const auto issues = oro::runtime::webview::diagnoseTlsPinConfig(value);
      if (issues.empty()) {
        return;
      }

      std::fprintf(
        stderr,
        "warn: %s: invalid %s configuration detected; pins may be ignored or fail closed.\n",
        origin.c_str(),
        label
      );

      for (const auto& issue : issues) {
        std::fprintf(
          stderr,
          "warn: %s:%s:%zu: %s\n",
          origin.c_str(),
          label,
          issue.line,
          issue.message.c_str()
        );
      }
    };

    warnIssues("tls_pins", "tls.pins", "tls_pins");
    warnIssues("webview_tls_pins", "webview.tls_pins", "webview_tls_pins");
  }
}

namespace oro::runtime::config {
  bool isDebugEnabled () {
    return oro_runtime_init_is_debug_enabled();
  }

  const Map<String, String> getUserConfig () {
    static const Map<String, String> cached = [] () {
      const auto bytes = oro_runtime_init_get_user_config_bytes();
      const auto size = oro_runtime_init_get_user_config_bytes_size();
      const auto embeddedFormatValue = oro_runtime_init_get_user_config_format();
      const auto embeddedFormat = embeddedFormatValue == static_cast<int>(UserConfigFormat::Toml)
        ? UserConfigFormat::Toml
        : UserConfigFormat::Ini;

      if (bytes != nullptr && size > 0) {
        const auto source = String(
          reinterpret_cast<const char*>(bytes),
          size
        );

        if (!source.empty()) {
          const auto config = parseUserConfigSource(source, embeddedFormat);
          warnTlsPinsConfigIssues("<embedded user config>", config);
          return config;
        }
      }

      const auto resourcesPath = filesystem::Resource::getResourcesPath();
      const auto preferredSearch = defaultSearchOrder(resourcesPath, "oro.toml");

      const auto preferredPath = firstExisting(preferredSearch);

      Vector<Candidate> orderedCandidates;

      if (const auto preferredEnv = envPath("ORO_CONFIG"); preferredEnv.has_value()) {
        orderedCandidates.emplace_back(preferredEnv.value(), detectFormat(preferredEnv.value()));
      }

      if (preferredPath.has_value()) {
        orderedCandidates.emplace_back(preferredPath.value(), UserConfigFormat::Toml);
      }

      for (const auto& candidate : orderedCandidates) {
        const auto source = readFileContents(candidate.first);
        if (source.empty()) {
          continue;
        }

        const auto config = parseUserConfigSource(source, candidate.second);
        warnTlsPinsConfigIssues(candidate.first.string(), config);
        return config;
      }

      return Map<String, String> {};
    }();

    return cached;
  }

  const String getDevHost () {
    return oro_runtime_init_get_dev_host();
  }

  int getDevPort () {
    return oro_runtime_init_get_dev_port();
  }
}
