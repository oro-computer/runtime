#include "../config.hh"
#include "../string.hh"
#include "../debug.hh"
#include "../ini.hh"
#include "../toml.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>

using namespace oro::runtime::string;

namespace oro::runtime::config {
  static constexpr char NAMESPACE_SEPARATOR = '.';
  static const String NAMESPACE_SEPARATOR_STRING = String(1, NAMESPACE_SEPARATOR);

  Config::Config (const String& source) {
    this->map = INI::parse(source, NAMESPACE_SEPARATOR_STRING);
  }

  Config::Config (const Config& source) : prefix(source.prefix) {
    this->map = source.data();
  }

  Config::Config (const Map<String, String>& source) {
    this->map = source;
  }

  Config::Config (const String& prefix, const Map<String, String>& source)
    : prefix(prefix) {
    this->map = source;
  }

  Config::Config (const String& prefix, const Config& source) : prefix(prefix) {
    this->map = source.data();
  }

  const String Config::get (const String& key) const noexcept {
    if (this->contains(key)) {
      return this->at(key);
    }

    return "";
  }

  const String& Config::at (const String& key) const {
    return this->map.at(key);
  }

  void Config::set (const String& key, const String& value) noexcept {
    this->map.insert_or_assign(key, value);
  }

  const std::size_t Config::size () const noexcept {
    return this->map.size();
  }

  bool Config::contains (const String& key) const noexcept {
    if (this->map.contains(key) && this->map.at(key).size() > 0) {
      return true;
    }

    return this->query(key).size() > 0;
  }

  bool Config::erase (const String& key) noexcept {
    if (this->map.contains(key)) {
      this->map.erase(key);
      return true;
    }

    const auto view = this->query(key);
    bool erased = false;

    for (const auto& tuple : view) {
      if (this->map.contains(tuple.first)) {
        this->map.erase(tuple.first);
        erased = true;
      }
    }

    return erased;
  }

  const Map<String, String>& Config::data () const noexcept {
    return this->map;
  }

  const Config Config::slice (const String& key) const noexcept {
    const auto view = this->query("[" + key + "]");
    Map<String, String> slice;

    for (const auto& tuple : view) {
      if (
        tuple.first.starts_with(key + NAMESPACE_SEPARATOR) &&
        tuple.second.size() > 0
      ) {
        const auto k = tuple.first.substr(key.size() + 1, tuple.first.size());
        const auto v = tuple.second;
        slice.insert_or_assign(k, v);
      }
    }

    return Config { key, slice };
  }

  const Config Config::query (const String& input) const noexcept {
    struct State {
      Vector<String> paths;
      Vector<String> targets;
      String property;
      String compared;
      String token;
      bool parsingSingleQuote = false;
      bool parsingDoubleQuote = false;
      bool parsingNamespace = false;
      bool parsingProperty = false;
      bool negate = false;
      bool compare = false;
    };

    String query = trim(input);
    State state;
    Map<String, String> results;

    if (!query.starts_with("[") && !query.starts_with(NAMESPACE_SEPARATOR_STRING)) {
      query = "[" + query + "]";
    }

    if (query.starts_with(NAMESPACE_SEPARATOR_STRING)) {
      query = "[*]" + query;
    }

    for (int i = 0; i < query.size(); ++i) {
      const auto ch = query[i];

      if (ch == '[') {
        if (state.parsingNamespace) {
          return Config {}; // error
        }

        state.parsingNamespace = true;
        state.token = "";
      } else if (ch == ']') {
        if (!state.parsingNamespace) {
          return Config {}; // error
        }

        state.parsingNamespace = false;
        if (state.token.size() == 0) {
          // [] implies [*]
          state.paths.push_back("*");
        } else {
          state.paths.push_back(state.token);
          state.token = "";
        }
      } else if (state.parsingNamespace) {
        state.token += ch;
      } else if (ch == '.') {
        state.parsingProperty = true;
      } else if (state.parsingProperty) {
        if (ch == ' ' && state.token.size() == 0) {
          continue;
        } else if (ch == '"') {
          if (!state.parsingSingleQuote) {
            state.parsingDoubleQuote = state.token.size() == 0;
            continue;
          }
        } else if (ch == '\'') {
          if (!state.parsingDoubleQuote) {
            state.parsingSingleQuote = state.token.size() == 0;
            continue;
          }
        } else if (ch == '!' && !state.parsingDoubleQuote && !state.parsingSingleQuote) {
          if (query[i + 1] == '=') {
            state.negate = true;
            state.compare = true;
            continue;
          } else {
            return Config {}; // error
          }
        } else if (ch == '=' && !state.parsingDoubleQuote && !state.parsingSingleQuote) {
          state.compare = true;
          continue;
        }

        if (state.compare) {
          state.compared += ch;
        } else {
          state.token += ch;
        }
      }
    }

    if (state.parsingProperty && state.token.size()) {
      state.property = trim(state.token);
      state.token = "";
    }

    state.compared = trim(state.compared);

    const auto& path = join(state.paths, NAMESPACE_SEPARATOR_STRING);
    for (const auto& tuple : this->map) {
      const auto parts = split(tuple.first, NAMESPACE_SEPARATOR_STRING);
      const auto& target = tuple.first;
      const auto prefix = join(
        Vector<String>(parts.begin(), parts.begin() + parts.size() - 1),
        NAMESPACE_SEPARATOR_STRING
      );

      bool match = false;
      if (path.starts_with(NAMESPACE_SEPARATOR_STRING)) {
        if (state.compare) {
          match = prefix.ends_with(path);
        } else {
          match = prefix.find(path) != String::npos;
        }
      } else if (path == "*") {
        match = true;
      } else if (prefix.starts_with(path)) {
        match = true;
      }

      if (match) {
        if (state.property == "*") {
          state.targets.push_back(target);
          state.compare = false;
        } else if (state.compare || state.property.size() > 0) {
          state.targets.push_back(prefix);
        } else {
          state.targets.push_back(target);
        }
      }
    }

    for (const auto& target : state.targets) {
      const auto key = state.compare || state.property.size() > 0
        ? target + NAMESPACE_SEPARATOR_STRING + state.property
        : target;

      if (!this->map.contains(key)) {
        continue;
      }

      const auto& value = this->map.at(key);

      if (state.compare) {
        if (state.negate) {
          if (value != state.compared) {
            results[key] = value;
          }
        } else if (value == state.compared) {
          results[key] = value;
        }
      } else {
        results[key] = value;
      }
    }

    return Config { results };
  }

  const Vector<String> Config::keys () const noexcept {
    Vector<String> results;
    for (const auto& tuple : this->map) {
      results.push_back(tuple.first);
    }
    return results;
  }

  const String Config::operator[] (const String& key) const {
    return this->map.at(key);
  }

  const String& Config::operator[] (const String& key) {
    return this->map[key];
  }

  const Config::Iterator Config::begin () const noexcept {
    return this->map.begin();
  }

  const Config::Iterator Config::end () const noexcept {
    return this->map.end();
  }

  const bool Config::clear () noexcept {
    if (this->map.size() == 0) {
      return false;
    }

    this->map.clear();
    return true;
  }

  const Vector<Config> Config::children () const noexcept {
    Vector<Config> children;
    Vector<String> seen;
    for (const auto& tuple : this->map) {
      const auto parts = split(tuple.first, NAMESPACE_SEPARATOR_STRING);
      const auto duplicate = std::find(seen.begin(), seen.end(), parts[0]) != seen.end();
      if (parts.size() > 1 && !duplicate) {
        seen.push_back(parts[0]);
        children.push_back(Config(parts[0], this->slice(parts[0])));
      }
    }
    return children;
  }

  // Helpers for normalized flattened keys
  const String normalizeKeySegment (const String& in) {
    String out = in;
    for (auto &ch : out) {
      const bool alnum = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
      if (!alnum) ch = '_';
    }
    return out;
  }

  const String key (const Vector<String>& segments) {
    String out;
    bool first = true;
    for (const auto &seg : segments) {
      auto s = normalizeKeySegment(seg);
      if (s.size() == 0) continue;
      if (!first) out += "_";
      out += s;
      first = false;
    }
    return out;
  }

  const String key (std::initializer_list<String> segments) {
    return key(Vector<String>(segments.begin(), segments.end()));
  }

  namespace {
    using oro::runtime::StringStream;

    inline String joinKey (
      const String& prefix,
      const String& key,
      const String& separator
    ) {
      if (prefix.empty()) {
        return key;
      } else if (key.empty()) {
        return prefix;
      }
      return prefix + separator + key;
    }

    inline bool isScalarTomlValue (const TOML::Value& value) {
      switch (value.type()) {
        case TOML::Type::Boolean:
        case TOML::Type::Integer:
        case TOML::Type::Float:
        case TOML::Type::String:
        case TOML::Type::Date:
        case TOML::Type::Time:
        case TOML::Type::DateTime:
        case TOML::Type::Empty:
          return true;
        case TOML::Type::Array:
        case TOML::Type::Table:
          return false;
      }

      return false;
    }

    inline String formatInteger (int64_t value) {
      return std::to_string(value);
    }

    inline String formatFloat (double value) {
      StringStream stream;
      stream << std::setprecision(15) << value;
      return stream.str();
    }

    inline String formatDate (const TOML::Date& date) {
      StringStream stream;
      stream << std::setw(4) << std::setfill('0') << date.year
             << "-" << std::setw(2) << std::setfill('0') << date.month
             << "-" << std::setw(2) << std::setfill('0') << date.day;
      return stream.str();
    }

    inline String formatTime (const TOML::Time& time) {
      StringStream stream;
      stream << std::setw(2) << std::setfill('0') << time.hour
             << ":" << std::setw(2) << std::setfill('0') << time.minute
             << ":" << std::setw(2) << std::setfill('0') << time.second;

      if (time.nanosecond > 0) {
        stream << "." << std::setw(9) << std::setfill('0') << time.nanosecond;
      }

      return stream.str();
    }

    inline String formatDateTime (const TOML::DateTime& value) {
      StringStream stream;
      stream << formatDate(value.date) << "T" << formatTime(value.time);
      if (value.hasOffset) {
        const int minutes = value.offsetMinutes;
        const char sign = minutes >= 0 ? '+' : '-';
        const int absolute = std::abs(minutes);
        const int hours = absolute / 60;
        const int mins = absolute % 60;
        stream << sign
               << std::setw(2) << std::setfill('0') << hours
               << ":" << std::setw(2) << std::setfill('0') << mins;
      }
      return stream.str();
    }

    inline String formatScalarTomlValue (const TOML::Value& value) {
      switch (value.type()) {
        case TOML::Type::Boolean:
          return value.asBool() ? "true" : "false";
        case TOML::Type::Integer:
          return formatInteger(value.asInteger());
        case TOML::Type::Float:
          return formatFloat(value.asFloat());
        case TOML::Type::String:
          return value.asString();
        case TOML::Type::Date:
          return formatDate(value.asDate());
        case TOML::Type::Time:
          return formatTime(value.asTime());
        case TOML::Type::DateTime:
          return formatDateTime(value.asDateTime());
        case TOML::Type::Empty:
        case TOML::Type::Array:
        case TOML::Type::Table:
          return "";
      }

      return "";
    }

    inline bool endsWith (const String& value, const String& suffix) {
      if (suffix.size() > value.size()) {
        return false;
      }
      return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    String joinScalarArray (
      const TOML::Value::Array& array,
      bool useNewlines
    ) {
      if (array.empty()) {
        return "";
      }

      StringStream stream;
      bool first = true;
      for (const auto& entry : array) {
        if (!isScalarTomlValue(entry)) {
          continue;
        }

        if (!first) {
          stream << (useNewlines ? '\n' : ' ');
        }

        stream << formatScalarTomlValue(entry);
        first = false;
      }

      return stream.str();
    }

    void flattenTomlValue (
      const TOML::Value& value,
      const String& prefix,
      const String& separator,
      Map<String, String>& output
    ) {
      switch (value.type()) {
        case TOML::Type::Array: {
          const auto& arr = value.asArray();
          const bool allScalars = std::all_of(
            arr.begin(),
            arr.end(),
            [](const auto& entry) {
              return isScalarTomlValue(entry);
            }
          );

          if (arr.empty()) {
            output[prefix] = "";
            return;
          }

          if (allScalars) {
            const bool useNewlines = endsWith(prefix, "_headers")
              || endsWith(prefix, "tls_pins")
              || endsWith(prefix, "tls.pins");
            output[prefix] = joinScalarArray(arr, useNewlines);
            return;
          }

          for (size_t i = 0; i < arr.size(); ++i) {
            const auto indexKey = joinKey(prefix, std::to_string(i), separator);
            flattenTomlValue(arr[i], indexKey, separator, output);
          }
          break;
        }

        case TOML::Type::Table: {
          const auto& table = value.asTable();
          for (const auto& entry : table) {
            const auto composed = joinKey(prefix, entry.first, separator);
            flattenTomlValue(entry.second, composed, separator, output);
          }
          break;
        }

        case TOML::Type::Boolean:
        case TOML::Type::Integer:
        case TOML::Type::Float:
        case TOML::Type::String:
        case TOML::Type::Date:
        case TOML::Type::Time:
        case TOML::Type::DateTime:
        case TOML::Type::Empty:
          output[prefix] = formatScalarTomlValue(value);
          break;
      }
    }

    Map<String, String> parseTomlSource (
      const String& source,
      const String& separator
    ) {
      Map<String, String> flattened;
      if (source.empty()) {
        return flattened;
      }

      const auto document = TOML::parse(source);
      if (!document.isTable()) {
        return flattened;
      }

      flattenTomlValue(document, "", separator, flattened);
      return flattened;
    }

    Map<String, String> normalizeDynamicConfigKeys (
      const Map<String, String>& flattened,
      const String& separator
    ) {
      Map<String, String> normalized;
      for (const auto& entry : flattened) {
        // Per-model lookups normalize the model-name segment through key().
        // Preserve every other flattened key because names such as extension
        // identifiers may intentionally contain hyphens.
        const auto modelPrefix = "ai_llm_model_";
        if (separator == "_" && entry.first.starts_with(modelPrefix)) {
          normalized[normalizeKeySegment(entry.first)] = entry.second;
        } else {
          normalized[entry.first] = entry.second;
        }
      }
      return normalized;
    }
  } // namespace

  Map<String, String> parseUserConfigSource (
    const String& source,
    UserConfigFormat format,
    const String& keyPathSeparator
  ) {
    if (source.empty()) {
      return {};
    }

    switch (format) {
      case UserConfigFormat::Ini:
        return normalizeDynamicConfigKeys(
          INI::parse(source, keyPathSeparator),
          keyPathSeparator
        );
      case UserConfigFormat::Toml:
        return normalizeDynamicConfigKeys(
          parseTomlSource(source, keyPathSeparator),
          keyPathSeparator
        );
    }

    return {};
  }

  namespace {
    // Static registry of known configuration keys. This is intentionally
    // conservative and focuses on commonly used and security-sensitive
    // options; new keys should be added here when introduced.
    const Vector<ConfigKeyInfo> kConfigRegistry = {
      // Filesystem and sandboxing
      {
        "filesystem_sandbox_enabled",
        "filesystem.sandbox_enabled",
        "Enable the filesystem sandbox for non-Apple platforms.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "filesystem_no_follow_symlinks",
        "filesystem.no_follow_symlinks",
        "Disallow following symlinks when resolving resource paths.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "filesystem_disable_links",
        "filesystem.disable_links",
        "Disallow creating hard links even when sandbox allows.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "extensions_allowed_roots",
        "extensions.allowed_roots",
        "Space-separated list of absolute directories allowed for loading native extensions.",
        "",
        ConfigValueType::String,
        false
      },

      // Lifecycle and debug
      {
        "lifecycle_desktop_always_running",
        "lifecycle.desktop_always_running",
        "Keep desktop runtimes always running instead of performing native pause/resume.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "debug_lifecycle",
        "debug.lifecycle",
        "Enable verbose lifecycle logging for desktop pause/resume/stop.",
        "false",
        ConfigValueType::Bool,
        false
      },

      // WebView HTTP headers and CORS
      {
        "webview_csp",
        "webview.csp",
        "Content-Security-Policy header value for custom scheme responses.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_referrer_policy",
        "webview.referrer_policy",
        "Referrer-Policy header value for custom scheme responses.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_cors_allow_all",
        "webview.cors_allow_all",
        "Allow all origins for custom scheme CORS (when true, allowed_origins is ignored).",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_cors_allow_credentials",
        "webview.cors_allow_credentials",
        "Allow credentials in custom scheme CORS responses.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_cors_allow_headers",
        "webview.cors_allow_headers",
        "Space-separated list of headers allowed in custom scheme CORS.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_cors_allow_methods",
        "webview.cors_allow_methods",
        "Space-separated list of HTTP methods allowed in custom scheme CORS.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_cors_allowed_origins",
        "webview.cors_allowed_origins",
        "Space-separated allowlist of origins when cors_allow_all is false.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_default_index",
        "webview.default_index",
        "Default index path for WebView navigation when a directory is requested.",
        "/index.html",
        ConfigValueType::String,
        false
      },
      {
        "webview_allow_any_route",
        "webview.allow_any_route",
        "Enable SPA-style fallback so unmatched routes resolve to the default index.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_watch",
        "webview.watch",
        "Enable file watching in development to emit filedidchange events.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_watch_reload",
        "webview.watch_reload",
        "Automatically reload the page when a filedidchange event is emitted.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_navigator_mounts",
        "webview.navigator.mounts",
        "Prefix for navigator mounts section; keys map host paths to navigator roots.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_navigator_enable_navigation_destures",
        "webview.navigator.enable_navigation_destures",
        "Deprecated typo; preserved for backward compatibility. Prefer webview_navigator_enable_navigation_gestures.",
        "",
        ConfigValueType::Bool,
        true
      },

      // Global metadata
      {
        "meta_title",
        "meta.title",
        "Human-readable application title used in OS and window chrome.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_description",
        "meta.description",
        "Short description of the application for manifests and stores.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_lang",
        "meta.lang",
        "BCP-47 language tag for the primary application locale.",
        "en-US",
        ConfigValueType::String,
        false
      },
      {
        "meta_version",
        "meta.version",
        "Semantic version string for the application bundle.",
        "1.0.0",
        ConfigValueType::String,
        false
      },
      {
        "meta_bundle_identifier",
        "meta.bundle_identifier",
        "Reverse-DNS bundle identifier used by platforms and stores.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_application_protocol",
        "meta.application_protocol",
        "Application protocol scheme used for deep linking.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_application_links",
        "meta.application_links",
        "Space-separated application link patterns used for URL handling.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_copyright",
        "meta.copyright",
        "Copyright string used in about dialogs and metadata.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_file_limit",
        "meta.file_limit",
        "Maximum number of file descriptors the process is allowed to open.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "meta_maintainer",
        "meta.maintainer",
        "Maintainer name/email used in about dialogs and package metadata.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_type",
        "meta.type",
        "Application type; when set to 'extension' the build targets an extension bundle.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "meta_revision",
        "meta.revision",
        "Optional packaging revision appended to version for Linux-style package naming.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "meta_compile_bitcode",
        "meta.compile_bitcode",
        "Enable bitcode compilation where supported (historically used for Apple toolchains).",
        "",
        ConfigValueType::Bool,
        true
      },
      {
        "meta_upload_bitcode",
        "meta.upload_bitcode",
        "Upload bitcode to Apple services during notarization/archive steps.",
        "",
        ConfigValueType::Bool,
        true
      },
      {
        "meta_upload_symbols",
        "meta.upload_symbols",
        "Upload debug symbols to Apple services during notarization/archive steps.",
        "",
        ConfigValueType::Bool,
        true
      },

      // I18n locales
      {
        "i18n_locales",
        "i18n.locales",
        "Prefix for locale key/value pairs mapping locale codes to resource paths.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "i18n_locales_default",
        "i18n.locales_default",
        "Default base path for locale bundles when per-locale overrides are not provided.",
        "i18n/locales",
        ConfigValueType::String,
        false
      },

      // Build configuration
      {
        "build_name",
        "build.name",
        "Short application name used for bundle names and packaging.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_output",
        "build.output",
        "Relative output directory for build artifacts.",
        "build",
        ConfigValueType::String,
        false
      },
      {
        "build_copy",
        "build.copy",
        "Directory or file pattern to copy into the build output.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_copy_map",
        "build.copy_map",
        "Path to a copy-map configuration file controlling build inputs.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_headless",
        "build.headless",
        "Start the application in headless mode (no visible window).",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_env",
        "build.env",
        "Space-separated list of environment variable names propagated into the runtime.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_script",
        "build.script",
        "Shell script to run before the build copy phase.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_script_after",
        "build.script_after",
        "Shell script to run after the build lifecycle completes.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "build_extensions_abi_strict",
        "build.extensions_abi_strict",
        "Require native extensions to match the runtime ABI exactly.",
        "true",
        ConfigValueType::Bool,
        false
      },

      // Exec and tooling toggles
      {
        "allow_exec",
        "build.allow_exec",
        "Allow the CLI and build system to execute external tools (NDK, Gradle, git, scripts).",
        "false",
        ConfigValueType::Bool,
        false
      },

      // CommonJS loader configuration
      {
        "commonjs_fscheck",
        "commonjs.fscheck",
        "Enable filesystem existence checks for CommonJS module resolution when using oro: URLs.",
        "true",
        ConfigValueType::Bool,
        false
      },

      // Debug configuration
      {
        "debug_flags",
        "debug.flags",
        "Additional compiler/linker flags used for debug builds (for example, '-g').",
        "",
        ConfigValueType::String,
        false
      },

      // CLI rc-only toggles (not mirrored into application.config)
      {
        "build_only",
        "cli.build_only",
        "When true, 'oroc build' only builds and skips running.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_codesign",
        "cli.build_codesign",
        "When true, 'oroc build' performs codesigning where supported.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_run",
        "cli.build_run",
        "When true, 'oroc build' runs the app after a successful build.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_notarize",
        "cli.build_notarize",
        "When true, 'oroc build' performs notarization for supported Apple targets.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_package",
        "cli.build_package",
        "When true, 'oroc build' packages artifacts for distribution.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_watch",
        "cli.build_watch",
        "Enable watch mode for 'oroc build' to rebuild when sources change.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_quiet",
        "cli.build_quiet",
        "Reduce build logging; mirrors the '--quiet' flag for applicable subcommands.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "build_watch_debounce_timeout",
        "cli.build_watch_debounce_timeout",
        "Debounce timeout in milliseconds for file watching in build mode.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "build_watch_sources",
        "cli.build_watch_sources",
        "Space-separated list of paths to watch for rebuilds.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "enable_sanitizers",
        "cli.enable_sanitizers",
        "Enable ASan/UBSan for desktop core builds via ORO_ENABLE_SANITIZERS.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "list-devices_udid",
        "cli.list_devices.udid",
        "When true in rc, 'oroc list-devices' defaults to printing UDIDs.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "list-devices_ecid",
        "cli.list_devices.ecid",
        "When true in rc, 'oroc list-devices' defaults to printing ECIDs.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "list-devices_only",
        "cli.list_devices.only",
        "When true in rc, 'oroc list-devices' prints only the first matching device identifier.",
        "false",
        ConfigValueType::Bool,
        false
      },

      // Platform packaging (Android)
      {
        "android_aapt_no_compress",
        "android.aapt_no_compress",
        "Comma-separated list of file extensions that should not be compressed in the APK.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_enable_standard_ndk_build",
        "android.enable_standard_ndk_build",
        "Enable Gradle's standard NDK build instead of externalNativeBuild integration.",
        "",
        ConfigValueType::Bool,
        false
      },
      {
        "android_main_activity",
        "android.main_activity",
        "Android MainActivity class name; defaults to a runtime-provided main activity.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_manifest_permissions",
        "android.manifest_permissions",
        "Space- or comma-separated list of Android manifest permission names to request.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_native_abis",
        "android.native_abis",
        "Comma-separated list of Android ABIs to build for (e.g., arm64-v8a,x86_64).",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_native_cflags",
        "android.native_cflags",
        "Additional compiler flags for Android native sources.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_native_sources",
        "android.native_sources",
        "Additional Android native sources to compile.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_native_makefile",
        "android.native_makefile",
        "Custom Android.mk/CMakeLists file used for native builds.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_sources",
        "android.sources",
        "Additional Java/Kotlin source roots for the Android Gradle project.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_icon",
        "android.icon",
        "Application icon for Android builds.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "android_icon_sizes",
        "android.icon_sizes",
        "Icon sizes/scales to generate for Android (comma-separated).",
        "",
        ConfigValueType::String,
        false
      },

      // Platform packaging (iOS)
      {
        "ios_codesign_identity",
        "ios.codesign_identity",
        "iOS code signing identity (for example, 'iPhone Developer: Name (TEAMID)').",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ios_distribution_method",
        "ios.distribution_method",
        "Xcode export method (app-store, package, release-testing, enterprise, debugging, developer-id).",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ios_provisioning_profile",
        "ios.provisioning_profile",
        "Path to the provisioning profile used for signing iOS apps.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ios_simulator_device",
        "ios.simulator_device",
        "Name of the iOS Simulator device to target (for example, 'iPhone 15').",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ios_nonexempt_encryption",
        "ios.nonexempt_encryption",
        "Indicate whether the app uses non-exempt encryption for export compliance.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "ios_icon",
        "ios.icon",
        "Application icon for iOS builds.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ios_icon_sizes",
        "ios.icon_sizes",
        "Icon sizes/scales to generate for iOS (comma-separated).",
        "",
        ConfigValueType::String,
        false
      },

      // Platform packaging (Linux)
      {
        "linux_categories",
        "linux.categories",
        "Comma-separated list of desktop entry categories for Linux desktop environments.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "linux_icon",
        "linux.icon",
        "Application icon for Linux desktop environments.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "linux_icon_sizes",
        "linux.icon_sizes",
        "Icon sizes/scales to generate for Linux (comma-separated).",
        "",
        ConfigValueType::String,
        false
      },
      {
        "linux_skip_desktop_extension",
        "linux.skip_desktop_extension",
        "Skip building the Linux desktop runtime extension shared library.",
        "false",
        ConfigValueType::Bool,
        false
      },

      // Platform packaging (macOS)
      {
        "mac_category",
        "mac.category",
        "macOS App Store category identifier for the app.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_codesign_identity",
        "mac.codesign_identity",
        "macOS code signing identity.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_codesign_paths",
        "mac.codesign_paths",
        "Additional paths to codesign in the macOS app bundle.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_minimum_supported_version",
        "mac.minimum_supported_version",
        "Minimum supported macOS version (for example, '13.0.0').",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_window_control_offsets",
        "mac.window_control_offsets",
        "Offsets for traffic-light window controls when using hiddenInset titlebars.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_icon",
        "mac.icon",
        "Application icon for macOS builds.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mac_icon_sizes",
        "mac.icon_sizes",
        "Icon sizes/scales to generate for macOS (comma-separated).",
        "",
        ConfigValueType::String,
        false
      },

      // Platform packaging (Windows)
      {
        "win_pfx",
        "win.pfx",
        "Relative path to the PFX file used for signing Windows packages.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "win_publisher",
        "win.publisher",
        "Publisher string for Windows appx packaging APIs.",
        "",
        ConfigValueType::String,
        false
      },

      // Window defaults
      {
        "window_width",
        "window.width",
        "Default window width as CSS-style percentage or pixel value.",
        "80%",
        ConfigValueType::String,
        false
      },
      {
        "window_height",
        "window.height",
        "Default window height as CSS-style percentage or pixel value.",
        "80%",
        ConfigValueType::String,
        false
      },
      {
        "window_max_width",
        "window.max_width",
        "Maximum window width as CSS-style percentage or pixel value.",
        "90%",
        ConfigValueType::String,
        false
      },
      {
        "window_max_height",
        "window.max_height",
        "Maximum window height as CSS-style percentage or pixel value.",
        "90%",
        ConfigValueType::String,
        false
      },
      {
        "window_min_width",
        "window.min_width",
        "Minimum window width as CSS-style percentage or pixel value.",
        "50%",
        ConfigValueType::String,
        false
      },
      {
        "window_min_height",
        "window.min_height",
        "Minimum window height as CSS-style percentage or pixel value.",
        "50%",
        ConfigValueType::String,
        false
      },
      {
        "window_resizable",
        "window.resizable",
        "Allow the primary window to be resized by the user.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "window_frameless",
        "window.frameless",
        "Create a frameless (chrome-less) window.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "window_utility",
        "window.utility",
        "Mark the window as a utility/tool window where supported.",
        "false",
        ConfigValueType::Bool,
        false
      },

      // Application-level options
      {
        "application_agent",
        "application.agent",
        "Enable application agent (tray-only) behavior where supported.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "application_signals",
        "application.signals",
        "Comma-separated process signals that the runtime should trap and forward.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "application_tray_icon",
        "application.tray_icon",
        "Asset path to use for the application tray icon on desktop.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "tray_tooltip",
        "tray.tooltip",
        "Tooltip text for the application tray icon.",
        "",
        ConfigValueType::String,
        false
      },

      // Platform-specific window options
      {
        "window_alert_title",
        "window.alert_title",
        "Title used for native alert dialogs spawned by the runtime.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "window_background_color_dark",
        "window.background_color_dark",
        "Window background color in dark mode (CSS color string).",
        "",
        ConfigValueType::String,
        false
      },
      {
        "window_background_color_light",
        "window.background_color_light",
        "Window background color in light mode (CSS color string).",
        "",
        ConfigValueType::String,
        false
      },
      {
        "window_titlebar_style",
        "window.titlebar_style",
        "Titlebar style for supported platforms (for example, 'hidden', 'hiddenInset').",
        "",
        ConfigValueType::String,
        false
      },
      {
        "window_maximizable",
        "window.maximizable",
        "Whether the main window can be maximized.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "window_minimizable",
        "window.minimizable",
        "Whether the main window can be minimized.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "window_closable",
        "window.closable",
        "Whether the main window can be closed by the user.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "win_icon",
        "win.icon",
        "Windows application icon resource path.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "win_logo",
        "win.logo",
        "Windows branding logo resource path for installer UI.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "win_cmd",
        "win.cmd",
        "Windows-specific launch command for the app binary during development.",
        "",
        ConfigValueType::String,
        false
      },

      // Tray configuration
      {
        "tray_icon",
        "tray.icon",
        "Icon to be displayed in the operating system tray.",
        "",
        ConfigValueType::String,
        false
      },

      // Service worker and fetch integration
      {
        "webview_service_worker_mode",
        "webview.service_worker_mode",
        "Service worker mode; 'hybrid' enables embedded service worker orchestration.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_service_worker_frame",
        "webview.service_worker_frame",
        "Enable hosting service workers in a dedicated frame; set to 'false' to disable.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_service_worker_fetch_event_timeout",
        "webview.service_worker_fetch_event_timeout",
        "Timeout in milliseconds for service worker fetch events before failing closed.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "webview_service_worker_fetch_event_max_response_redirects",
        "webview.service_worker_fetch_event_max_response_redirects",
        "Maximum number of redirects allowed for service worker-controlled fetches.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "webview_fetch_headers_filter",
        "webview.fetch_headers_filter",
        "Space-separated glob patterns of headers that should not be forwarded for XHR.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_fetch_allow_runtime_headers",
        "webview.fetch_allow_runtime_headers",
        "Allow runtime-injected headers (e.g., Runtime-Client-ID) on matching requests.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_cache-control",
        "webview.cache_control",
        "Cache-Control header value applied to WebView custom-scheme responses.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_url_protocols",
        "webview.url_protocols",
        "Space-separated list of URL protocol schemes treated as webview-safe origins.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_protocol-handlers",
        "webview.protocol_handlers",
        "Space-separated list of protocol handlers to register for the URL shim.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_root",
        "webview.root",
        "Root path under the app origin used as the default navigation base.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_navigator_enable_navigation_gestures",
        "webview.navigator.enable_navigation_gestures",
        "Enable back/forward navigation gestures in the embedded WebView.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_navigator_navigation_gestures_pull_to_refresh_title",
        "webview.navigator.navigation_gestures_pull_to_refresh_title",
        "Title used in pull-to-refresh UI when navigation gestures are enabled.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_navigator_policies_allowed",
        "webview.navigator.policies_allowed",
        "Space-separated list of navigator policy names allowed for mounted paths.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_insecure_domains",
        "webview.insecure_domains",
        "Space-separated list of domains treated as insecure for mixed-content handling.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "webview_filesystem_picker_require_user_activation",
        "webview.filesystem_picker_require_user_activation",
        "Require a prior user activation before allowing filesystem picker APIs.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "webview_tls_pins",
        "webview.tls_pins",
        "Newline-separated list of TLS certificate pins for WebView HTTPS connections; each line has the form '<host> sha256/<base64>'.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "tls_pins",
        "tls.pins",
        "Newline-separated list of TLS certificate pins for runtime TLS client connections; each line has the form '<host> sha256/<base64>'.",
        "",
        ConfigValueType::String,
        false
      },

      // HTTP IPC bridge
      {
        "http_ipc_enable",
        "http.ipc.enable",
        "Enable the embedded HTTP→IPC bridge server.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "http_ipc_host",
        "http.ipc.host",
        "Host interface for the HTTP→IPC bridge.",
        "127.0.0.1",
        ConfigValueType::String,
        false
      },
      {
        "http_ipc_port",
        "http.ipc.port",
        "Port for the HTTP→IPC bridge (must be > 0 to bind).",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "http_ipc_path",
        "http.ipc.path",
        "Path prefix for HTTP→IPC bridge routes.",
        "/ipc",
        ConfigValueType::String,
        false
      },
      {
        "http_ipc_allow_cors",
        "http.ipc.allow_cors",
        "Allow permissive CORS on HTTP→IPC responses (enables OPTIONS handler).",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "http_ipc_shared_key",
        "http.ipc.shared_key",
        "Shared secret required in X-ORO-Auth header for HTTP→IPC requests.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "http_ipc_timeout_ms",
        "http.ipc.timeout_ms",
        "Timeout in milliseconds for HTTP→IPC bridge requests.",
        "",
        ConfigValueType::Int,
        false
      },

      // Web process extension (Linux)
      {
        "web-process-extension_cwd",
        "web_process_extension.cwd",
        "Working directory for the Linux WebKit web process extension helper.",
        "",
        ConfigValueType::String,
        false
      },

      // Native build inputs (legacy)
      {
        "native_files",
        "native.files",
        "Legacy list of native source files to add to the compile step (prefer build.extensions.*).",
        "",
        ConfigValueType::String,
        true
      },
      {
        "native_headers",
        "native.headers",
        "Legacy list of native header include directories (prefer build.extensions.*).",
        "",
        ConfigValueType::String,
        true
      },
      {
        "web-process-extension_host",
        "web_process_extension.host",
        "Host for the Linux WebKit web process extension shim.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "web-process-extension_port",
        "web_process_extension.port",
        "Port for the Linux WebKit web process extension shim.",
        "",
        ConfigValueType::Int,
        false
      },

      // Permissions (default allow unless explicitly set to false)
      {
        "permissions_allow_notifications",
        "permissions.allow_notifications",
        "Allow notifications permission prompts and delivery.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_geolocation",
        "permissions.allow_geolocation",
        "Allow geolocation prompts and access.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_geolocation_in_background",
        "permissions.allow_geolocation_in_background",
        "Allow geolocation updates while the app is in background (platform permitting).",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_camera",
        "permissions.allow_camera",
        "Allow camera access via getUserMedia and related APIs.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_microphone",
        "permissions.allow_microphone",
        "Allow microphone access via getUserMedia and related APIs.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_user_media",
        "permissions.allow_user_media",
        "Master toggle for combined camera/microphone getUserMedia access.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_clipboard",
        "permissions.allow_clipboard",
        "Allow programmatic clipboard read/write operations.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_fullscreen",
        "permissions.allow_fullscreen",
        "Allow WebView fullscreen requests.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_data_access",
        "permissions.allow_data_access",
        "Allow persistent storage and data access APIs.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_device_info",
        "permissions.allow_device_info",
        "Allow access to limited device identifying information.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_device_orientation",
        "permissions.allow_device_orientation",
        "Allow access to device orientation sensors.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_media_key_system",
        "permissions.allow_media_key_system",
        "Allow Encrypted Media Extensions key system access.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_pointer_lock",
        "permissions.allow_pointer_lock",
        "Allow pointer lock (mouse capture) requests.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_sensors",
        "permissions.allow_sensors",
        "Allow access to generic sensor APIs.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_bluetooth",
        "permissions.allow_bluetooth",
        "Allow Web Bluetooth usage.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_airplay",
        "permissions.allow_airplay",
        "Allow AirPlay media routing on supported Apple platforms.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_autoplay",
        "permissions.allow_autoplay",
        "Allow media autoplay where the platform permits.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_local_fonts",
        "permissions.allow_local_fonts",
        "Allow access to local system fonts.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_hotkeys",
        "permissions.allow_hotkeys",
        "Allow global hotkey registrations.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_read_media",
        "permissions.allow_read_media",
        "Allow access to user media libraries when supported.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_read_media_images",
        "permissions.allow_read_media_images",
        "Allow access to user image libraries.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_read_media_video",
        "permissions.allow_read_media_video",
        "Allow access to user video libraries.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_read_media_audio",
        "permissions.allow_read_media_audio",
        "Allow access to user audio libraries.",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_push_notifications",
        "permissions.allow_push_notifications",
        "Allow push notification registration and delivery.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_allow_unvalidated_native_libraries",
        "permissions.allow_unvalidated_native_libraries",
        "Allow loading native libraries that have not been validated by tooling.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_hardware_acceleration_disabled",
        "permissions.hardware_acceleration_disabled",
        "Disable hardware acceleration for the embedded WebView where supported.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "permissions_policy_direct_sockets",
        "permissions.policy_direct_sockets",
        "Opt-in toggle for the Direct Sockets permissions policy (JS gating only).",
        "true",
        ConfigValueType::Bool,
        false
      },
      {
        "permission_allow_autoplay",
        "permissions.allow_autoplay_legacy",
        "Legacy alias for autoplay permission; prefer permissions_allow_autoplay.",
        "",
        ConfigValueType::Bool,
        true
      },
      {
        "permissions_allow_otp",
        "permissions.allow_otp",
        "Allow Web OTP (SMS code retrieval) in the application.",
        "true",
        ConfigValueType::Bool,
        false
      },

      // AI / LLaMA server configuration (subset of commonly used keys)
      {
        "ai_llm_server_prefix",
        "ai.llm.server_prefix",
        "Route prefix for the embedded LLaMA server.",
        "/ai/llama",
        ConfigValueType::String,
        false
      },
      {
        "ai_llm_default_model",
        "ai.llm.default_model",
        "Default model name to load for the embedded LLaMA server.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ai_llm_rate_limit_concurrency",
        "ai.llm.rate_limit_concurrency",
        "Maximum concurrent LLaMA requests; 0 or unset uses the built-in default.",
        "4",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_rate_limit_rps",
        "ai.llm.rate_limit_rps",
        "Overall requests-per-second limit; 0 disables rate limiting.",
        "0",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_rate_limit_burst",
        "ai.llm.rate_limit_burst",
        "Burst size for request rate limiting; 0 disables burst limits.",
        "0",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_default_max_tokens",
        "ai.llm.default_max_tokens",
        "Default maximum tokens to generate when the client omits max_tokens.",
        "128",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_default_temperature",
        "ai.llm.default_temperature",
        "Default sampling temperature for LLaMA server requests.",
        "0.8",
        ConfigValueType::Float,
        false
      },
      {
        "ai_llm_default_top_p",
        "ai.llm.default_top_p",
        "Default nucleus sampling top-p value.",
        "0.95",
        ConfigValueType::Float,
        false
      },
      {
        "ai_llm_default_top_k",
        "ai.llm.default_top_k",
        "Default top-k sampling value.",
        "40",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_default_min_p",
        "ai.llm.default_min_p",
        "Default minimum probability mass for sampling.",
        "0.05",
        ConfigValueType::Float,
        false
      },
      {
        "ai_llm_model_path",
        "ai.llm.model_path",
        "Directory containing LLaMA model files.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ai_llm_n_threads",
        "ai.llm.n_threads",
        "Default number of CPU threads for LLaMA contexts.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_context_n_batch",
        "ai.llm.context_n_batch",
        "Default batch size for LLaMA evaluation contexts.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_context_n_ubatch",
        "ai.llm.context_n_ubatch",
        "Default micro-batch size for LLaMA contexts.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_gpu_layer_count",
        "ai.llm.gpu_layer_count",
        "Number of model layers to offload to GPU when supported.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_http_enable",
        "ai.llm.http_enable",
        "Enable the optional external HTTP bridge for the LLaMA server.",
        "false",
        ConfigValueType::Bool,
        false
      },
      {
        "ai_llm_http_host",
        "ai.llm.http_host",
        "Bind host for the external HTTP bridge.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "ai_llm_http_port",
        "ai.llm.http_port",
        "Bind port for the external HTTP bridge.",
        "",
        ConfigValueType::Int,
        false
      },
      {
        "ai_llm_http_allow_cors",
        "ai.llm.http_allow_cors",
        "Allow permissive CORS on the external HTTP bridge.",
        "",
        ConfigValueType::Bool,
        false
      },

      // MCP server bridge
      {
        "mcp_host",
        "mcp.host",
        "Default host for the embedded MCP HTTP/SSE bridge.",
        "127.0.0.1",
        ConfigValueType::String,
        false
      },
      {
        "mcp_port",
        "mcp.port",
        "Default port for the embedded MCP HTTP/SSE bridge (0 = ephemeral).",
        "0",
        ConfigValueType::Int,
        false
      },
      {
        "mcp_endpoint",
        "mcp.endpoint",
        "Base HTTP/SSE path for the MCP bridge.",
        "/mcp",
        ConfigValueType::String,
        false
      },
      {
        "mcp_token",
        "mcp.token",
        "Static bearer token enforced by the MCP bridge when provided.",
        "",
        ConfigValueType::String,
        false
      },
      {
        "mcp_auth_timeout_ms",
        "mcp.auth_timeout_ms",
        "Timeout in milliseconds when awaiting dynamic MCP authorization handlers.",
        "5000",
        ConfigValueType::Int,
        false
      }
    };
  } // namespace

  const Vector<ConfigKeyInfo>& getConfigRegistry () {
    return kConfigRegistry;
  }

  const ConfigKeyInfo* findConfigKeyInfo (const String& name) {
    if (name.size() == 0) {
      return nullptr;
    }

    for (const auto& entry : kConfigRegistry) {
      if ((entry.key != nullptr && name == entry.key) ||
          (entry.path != nullptr && name == entry.path)) {
        return &entry;
      }
    }
    return nullptr;
  }
}
