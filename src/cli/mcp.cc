#include "mcp.hh"

#include "../cli.hh"
#include "../runtime/env.hh"
#include "../runtime/bytes.hh"
#include "../runtime/config.hh"
#include "../runtime/json.hh"
#include "../runtime/ini.hh"
#include "../runtime/mcp/http_server.hh"
#include "../runtime/mcp/protocol.hh"
#include "../runtime/mcp/resource.hh"
#include "../runtime/mcp/tool.hh"
#include "../runtime/toml.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace oro::cli::mcp {
  using runtime::JSON::Any;
  using runtime::JSON::Array;
  using runtime::JSON::Boolean;
  using runtime::JSON::Null;
  using runtime::JSON::Number;
  using runtime::JSON::Object;
  using runtime::JSON::String;
  using runtime::Vector;

  using runtime::mcp::Error;
  using runtime::mcp::ErrorCode;
  using runtime::mcp::Request;
  using runtime::mcp::Response;

  namespace {
    static constexpr size_t kDefaultMaxFileBytes = 1024 * 1024;
    static constexpr size_t kDefaultMaxToolOutputBytes = 1024 * 1024;
    static constexpr size_t kMaxFileBytes = 16 * 1024 * 1024;
    static constexpr size_t kMaxToolOutputBytes = 16 * 1024 * 1024;
    static constexpr size_t kMaxWorkspaceWriteBytes = 4 * 1024 * 1024;
    static constexpr size_t kMaxDirectoryEntries = 10000;
    static constexpr size_t kMaxDirectoryDepth = 64;
    static constexpr int kMaxToolTimeoutMilliseconds = 24 * 60 * 60 * 1000;
    static constexpr size_t kMaxStdioMessageBytes = 8 * 1024 * 1024;

    static inline bool startsWith(const oro::runtime::String& value, const char* prefix) {
      if (prefix == nullptr) return false;
      const auto n = std::strlen(prefix);
      if (value.size() < n) return false;
      return value.compare(0, n, prefix) == 0;
    }

    static inline oro::runtime::String lowerCopy(oro::runtime::String value) {
      for (auto& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return value;
    }

    static inline oro::runtime::String trimCopy(oro::runtime::String value) {
      auto begin = value.begin();
      while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
      }
      auto end = value.end();
      while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
      }
      return oro::runtime::String(begin, end);
    }

    static bool isForbiddenWorkspaceReadTarget(const fs::path& rel) {
      if (rel.empty()) return true;
      auto it = rel.begin();
      if (it == rel.end()) return true;
      const auto first = it->string();
      if (first == ".git") {
        return true;
      }
      return false;
    }

    static bool isForbiddenWorkspaceWriteTarget(const fs::path& rel) {
      if (isForbiddenWorkspaceReadTarget(rel)) return true;
      for (const auto& part : rel) {
        if (part == ".git") return true;
      }
      auto it = rel.begin();
      if (it == rel.end()) return true;
      const auto first = it->string();
      if (first == "build" || first == "tmp") {
        return true;
      }
      const auto normalized = rel.generic_string();
      if (normalized == "api/index.d.ts" || normalized == "api/index.tmp.d.ts") {
        return true;
      }
      return false;
    }

    static bool containsDotDot(const fs::path& rel) {
      for (const auto& part : rel) {
        if (part == "..") return true;
      }
      return false;
    }

    static bool isDefinitelyBinaryExtension(const fs::path& path) {
      const auto ext = lowerCopy(path.extension().string());
      if (ext == ".png") return true;
      if (ext == ".jpg") return true;
      if (ext == ".jpeg") return true;
      if (ext == ".gif") return true;
      if (ext == ".webp") return true;
      if (ext == ".ico") return true;
      if (ext == ".icns") return true;
      if (ext == ".zip") return true;
      if (ext == ".pdf") return true;
      return false;
    }

    static oro::runtime::String guessMimeType(const fs::path& path, bool isDirectory) {
      if (isDirectory) {
        return "application/json";
      }

      const auto filename = lowerCopy(path.filename().string());
      if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".d.ts") {
        return "text/plain";
      }

      const auto ext = lowerCopy(path.extension().string());
      if (ext == ".toml") return "text/plain";
      if (ext == ".ini") return "text/plain";
      if (ext == ".json") return "application/json";
      if (ext == ".md") return "text/markdown";
      if (ext == ".js" || ext == ".mjs") return "application/javascript";
      if (ext == ".ts" || ext == ".mts") return "text/plain";
      if (ext == ".cc" || ext == ".hh" || ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp") return "text/plain";
      if (ext == ".sh") return "text/plain";
      if (ext == ".yml" || ext == ".yaml") return "text/plain";
      if (ext == ".txt") return "text/plain";
      if (ext == ".svg") return "image/svg+xml";
      if (ext == ".png") return "image/png";
      if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
      if (ext == ".gif") return "image/gif";
      if (ext == ".webp") return "image/webp";
      if (ext == ".ico") return "image/x-icon";
      if (ext == ".icns") return "image/icns";
      return "text/plain";
    }

    static runtime::config::UserConfigFormat detectConfigFormatForPath(const fs::path& path) {
      const auto filename = lowerCopy(path.filename().string());
      if (filename == "oro.toml" || lowerCopy(path.extension().string()) == ".toml") {
        return runtime::config::UserConfigFormat::Toml;
      }
      return runtime::config::UserConfigFormat::Ini;
    }

    static oro::runtime::String configFormatName(runtime::config::UserConfigFormat format) {
      switch (format) {
        case runtime::config::UserConfigFormat::Toml:
          return "toml";
        case runtime::config::UserConfigFormat::Ini:
          return "ini";
      }

      return "unknown";
    }

    static std::optional<fs::path> normalizeExistingPath(const fs::path& path, bool requireDirectory) {
      if (path.empty()) {
        return std::nullopt;
      }

      std::error_code ec;
      auto canonical = fs::weakly_canonical(path, ec);
      if (ec) {
        ec.clear();
        canonical = fs::absolute(path, ec);
        if (ec) {
          return std::nullopt;
        }
      }

      if (!fs::exists(canonical, ec) || ec) {
        return std::nullopt;
      }

      if (requireDirectory && (!fs::is_directory(canonical, ec) || ec)) {
        return std::nullopt;
      }

      return canonical.lexically_normal();
    }

    static std::optional<fs::path> resolveExecutableFromPath(const oro::runtime::String& executable) {
      if (executable.empty()) {
        return std::nullopt;
      }

      fs::path requested(executable);
      if (requested.has_parent_path() || requested.is_absolute()) {
        return normalizeExistingPath(requested, false);
      }

      const auto pathEnv = runtime::env::get("PATH");
      if (pathEnv.empty()) {
        return std::nullopt;
      }

      const char separator =
      #if defined(_WIN32)
        ';';
      #else
        ':';
      #endif

      size_t start = 0;
      while (start <= pathEnv.size()) {
        auto end = pathEnv.find(separator, start);
        if (end == oro::runtime::String::npos) {
          end = pathEnv.size();
        }

        const auto rawDir = pathEnv.substr(start, end - start);
        const auto dir = trimCopy(rawDir);
        if (!dir.empty()) {
          auto candidate = normalizeExistingPath(fs::path(dir) / requested, false);
          if (candidate.has_value()) {
            return candidate;
          }

        #if defined(_WIN32)
          if (!requested.has_extension()) {
            candidate = normalizeExistingPath(fs::path(dir) / (requested.string() + ".exe"), false);
            if (candidate.has_value()) {
              return candidate;
            }
          }
        #endif
        }

        if (end == pathEnv.size()) {
          break;
        }
        start = end + 1;
      }

      return std::nullopt;
    }

    static bool runtimeRootHasInstalledDocs(const fs::path& root) {
      static const std::array<fs::path, 5> candidates = {
        fs::path("share/doc/oroc/README.md"),
        fs::path("api/README.md"),
        fs::path("share/man/man1"),
        fs::path("share/man/man3"),
        fs::path("share/man/man7")
      };

      for (const auto& rel : candidates) {
        std::error_code ec;
        if (fs::exists(root / rel, ec) && !ec) {
          return true;
        }
      }

      return false;
    }

    static std::optional<fs::path> resolveRuntimeInstallRoot(const Options& options) {
      auto tryRuntimeRoot = [](const fs::path& candidate) -> std::optional<fs::path> {
        auto root = normalizeExistingPath(candidate, true);
        if (!root.has_value()) {
          return std::nullopt;
        }

        if (!runtimeRootHasInstalledDocs(*root)) {
          return std::nullopt;
        }

        return root;
      };

      const auto runtimeHomeApi = trimCopy(runtime::env::get("ORO_HOME_API"));
      if (!runtimeHomeApi.empty()) {
        auto apiDir = normalizeExistingPath(fs::path(runtimeHomeApi), true);
        if (apiDir.has_value()) {
          auto root = tryRuntimeRoot(apiDir->parent_path());
          if (root.has_value()) {
            return root;
          }
        }
      }

      if (!options.cliExecutable.empty()) {
        auto cliPath = resolveExecutableFromPath(options.cliExecutable);
        if (cliPath.has_value()) {
          auto parent = cliPath->parent_path();
          if (!parent.empty()) {
            auto root = tryRuntimeRoot(parent.parent_path());
            if (root.has_value()) {
              return root;
            }

            root = tryRuntimeRoot(parent);
            if (root.has_value()) {
              return root;
            }
          }
        }
      }

      const auto runtimeHome = trimCopy(runtime::env::get("ORO_HOME"));
      if (!runtimeHome.empty()) {
        auto root = tryRuntimeRoot(fs::path(runtimeHome));
        if (root.has_value()) {
          return root;
        }
      }

      return std::nullopt;
    }

    static fs::path resolveActiveConfigRelativePath(const Options& options, const fs::path& workspaceRoot, bool* exists = nullptr) {
      if (exists != nullptr) {
        *exists = false;
      }

      if (!options.configPath.empty()) {
        const auto rel = options.configPath.lexically_normal();
        if (exists != nullptr) {
          std::error_code ec;
          const auto abs = (workspaceRoot / rel).lexically_normal();
          *exists = fs::exists(abs, ec) && !ec && fs::is_regular_file(abs, ec);
        }
        return rel;
      }

      {
        std::error_code ec;
        const auto toml = (workspaceRoot / "oro.toml").lexically_normal();
        if (fs::exists(toml, ec) && !ec && fs::is_regular_file(toml, ec)) {
          if (exists != nullptr) {
            *exists = true;
          }
          return fs::path("oro.toml");
        }
      }

      {
        std::error_code ec;
        const auto ini = (workspaceRoot / "oro.ini").lexically_normal();
        if (fs::exists(ini, ec) && !ec && fs::is_regular_file(ini, ec)) {
          if (exists != nullptr) {
            *exists = true;
          }
          return fs::path("oro.ini");
        }
      }

      return fs::path("oro.toml");
    }

    struct EffectiveCliCommand {
      oro::runtime::String name;
      size_t subcommandIndex = 0;
      size_t insertAfterIndex = 0;
    };

    static std::optional<EffectiveCliCommand> findEffectiveCliCommand(const Vector<oro::runtime::String>& rawArgs) {
      if (rawArgs.empty()) {
        return std::nullopt;
      }

      size_t i = 0;
      while (i < rawArgs.size()) {
        const auto& arg = rawArgs[i];
        if (arg == "--verbose" || arg == "-V" ||
            arg == "--debug" || arg == "-D" ||
            arg == "--quiet" || arg == "-q" ||
            arg == "--no-color" ||
            arg == "--json") {
          i++;
          continue;
        }

        if (arg == "--log-file") {
          if (i + 1 >= rawArgs.size()) {
            return std::nullopt;
          }
          i += 2;
          continue;
        }

        if (startsWith(arg, "--log-file=")) {
          i++;
          continue;
        }

        if (!arg.empty() && arg.front() == '-') {
          return std::nullopt;
        }

        EffectiveCliCommand result;
        result.name = arg;
        result.subcommandIndex = i;
        result.insertAfterIndex = i;

        if (arg == "update" && i + 1 < rawArgs.size()) {
          const auto& nested = rawArgs[i + 1];
          if (!nested.empty() && nested.front() != '-') {
            result.name = "update-" + nested;
            result.insertAfterIndex = i + 1;
          }
        }

        return result;
      }

      return std::nullopt;
    }

    static bool cliCommandSupportsConfig(const oro::runtime::String& command) {
      static const std::unordered_set<oro::runtime::String> commands = {
        "build",
        "config",
        "env",
        "install-app",
        "print-build-dir",
        "run",
        "update-bundle",
        "update-init",
        "version"
      };

      return commands.find(command) != commands.end();
    }

    static bool hasExplicitConfigArg(const Vector<oro::runtime::String>& rawArgs, size_t startIndex) {
      if (startIndex >= rawArgs.size()) {
        return false;
      }

      for (size_t i = startIndex; i < rawArgs.size(); i++) {
        if (rawArgs[i] == "--config" || startsWith(rawArgs[i], "--config=")) {
          return true;
        }
      }

      return false;
    }

    static std::optional<fs::path> resourcePathFromDescriptor(const runtime::mcp::ResourceDescriptor& desc) {
      if (!desc.metadata.isObject()) {
        return std::nullopt;
      }

      const auto& metadata = desc.metadata.as<Object>();
      if (!metadata.contains("runtime_path")) {
        return std::nullopt;
      }

      const auto& pathValue = metadata.get("runtime_path");
      if (!pathValue.isString()) {
        return std::nullopt;
      }

      return normalizeExistingPath(fs::path(pathValue.as<String>().value()), false);
    }

    static bool isSubpath(const fs::path& base, const fs::path& candidate) {
      auto b = base.lexically_normal();
      auto c = candidate.lexically_normal();
      auto bIt = b.begin();
      auto cIt = c.begin();
      for (; bIt != b.end(); ++bIt, ++cIt) {
        if (cIt == c.end()) return false;
        if (*bIt != *cIt) return false;
      }
      return true;
    }

    static std::optional<fs::path> resolveWorkspacePath(
      const fs::path& workspaceRoot,
      const oro::runtime::String& userPath,
      bool requireExisting,
      bool requireFile,
      bool requireDirectory,
      bool allowCreateDirs,
      oro::runtime::String& error
    ) {
      error = "";

      if (userPath.size() == 0) {
        if (requireFile) {
          error = "path is required";
          return std::nullopt;
        }
        // Empty path resolves to workspace root.
        std::error_code ec;
        auto root = fs::weakly_canonical(workspaceRoot, ec);
        if (ec) {
          error = "failed to resolve workspace root: " + ec.message();
          return std::nullopt;
        }
        return root;
      }

      fs::path rel = fs::path(userPath).lexically_normal();
      if (rel.empty()) {
        error = "invalid path";
        return std::nullopt;
      }

      if (rel.is_absolute() || rel.has_root_path() || rel.has_root_name() || rel.has_root_directory()) {
        error = "absolute paths are not allowed";
        return std::nullopt;
      }

      if (containsDotDot(rel)) {
        error = "path traversal is not allowed";
        return std::nullopt;
      }

      if (isForbiddenWorkspaceReadTarget(rel)) {
        error = "path is not allowed";
        return std::nullopt;
      }

      if (!requireExisting || allowCreateDirs) {
        if (isForbiddenWorkspaceWriteTarget(rel)) {
          error = "path is not allowed";
          return std::nullopt;
        }
      }

      std::error_code ec;
      auto rootCanonical = fs::weakly_canonical(workspaceRoot, ec);
      if (ec) {
        error = "failed to resolve workspace root: " + ec.message();
        return std::nullopt;
      }

      fs::path candidate = (rootCanonical / rel).lexically_normal();

      if (requireExisting) {
        auto resolved = fs::canonical(candidate, ec);
        if (ec) {
          error = "path does not exist: " + ec.message();
          return std::nullopt;
        }
        if (!isSubpath(rootCanonical, resolved)) {
          error = "path escapes workspace root";
          return std::nullopt;
        }
        if (requireFile) {
          if (!fs::is_regular_file(resolved, ec) || ec) {
            error = "path is not a file";
            return std::nullopt;
          }
        }
        if (requireDirectory) {
          if (!fs::is_directory(resolved, ec) || ec) {
            error = "path is not a directory";
            return std::nullopt;
          }
        }
        return resolved;
      }

      // For non-existing targets: validate nearest existing ancestor doesn't escape.
      fs::path ancestor = candidate.parent_path();
      while (!ancestor.empty()) {
        if (fs::exists(ancestor, ec) && !ec) {
          break;
        }
        auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
      }

      auto resolvedAncestor = fs::weakly_canonical(ancestor, ec);
      if (ec) {
        error = "failed to resolve path: " + ec.message();
        return std::nullopt;
      }
      if (!isSubpath(rootCanonical, resolvedAncestor)) {
        error = "path escapes workspace root";
        return std::nullopt;
      }

      if (allowCreateDirs) {
        auto parent = candidate.parent_path();
        if (!parent.empty()) {
          fs::create_directories(parent, ec);
          if (ec) {
            error = "failed to create directories: " + ec.message();
            return std::nullopt;
          }
          auto resolvedParent = fs::weakly_canonical(parent, ec);
          if (ec) {
            error = "failed to resolve created directories: " + ec.message();
            return std::nullopt;
          }
          if (!isSubpath(rootCanonical, resolvedParent)) {
            error = "path escapes workspace root";
            return std::nullopt;
          }
        }
      }

      // If the file already exists, ensure symlinks don't escape.
      if (fs::exists(candidate, ec) && !ec) {
        auto resolved = fs::canonical(candidate, ec);
        if (ec) {
          error = "failed to resolve existing path: " + ec.message();
          return std::nullopt;
        }
        if (!isSubpath(rootCanonical, resolved)) {
          error = "path escapes workspace root";
          return std::nullopt;
        }
        return resolved;
      }

      // Keep candidate under the canonical root lexically.
      if (!isSubpath(rootCanonical, candidate)) {
        error = "path escapes workspace root";
        return std::nullopt;
      }

      return candidate;
    }

    static std::optional<oro::runtime::String> readFileBounded(
      const fs::path& path,
      size_t maxBytes,
      bool& truncated,
      oro::runtime::String& error
    ) {
      truncated = false;
      error = "";

      std::error_code ec;
      const auto sizeValue = fs::file_size(path, ec);
      size_t readLimit = maxBytes;
      if (!ec) {
        if (sizeValue > maxBytes) {
          truncated = true;
          readLimit = maxBytes;
        } else {
          readLimit = static_cast<size_t>(sizeValue);
        }
      }

      std::ifstream in(path, std::ios::in | std::ios::binary);
      if (!in.is_open()) {
        error = "failed to open file";
        return std::nullopt;
      }

      oro::runtime::String data;
      data.resize(readLimit);
      in.read(data.data(), static_cast<std::streamsize>(readLimit));
      data.resize(static_cast<size_t>(in.gcount()));

      if (!in.eof() && data.size() == maxBytes) {
        truncated = true;
      }

      return data;
    }

    static std::optional<Vector<uint8_t>> readFileBytesBounded(
      const fs::path& path,
      size_t maxBytes,
      bool& truncated,
      oro::runtime::String& error
    ) {
      auto text = readFileBounded(path, maxBytes, truncated, error);
      if (!text.has_value()) return std::nullopt;
      Vector<uint8_t> bytes;
      bytes.reserve(text->size());
      for (const auto c : *text) {
        bytes.push_back(static_cast<uint8_t>(c));
      }
      return bytes;
    }

    static bool writeFile(
      const fs::path& path,
      const oro::runtime::String& content,
      bool overwrite,
      oro::runtime::String& error
    ) {
      error = "";
      std::error_code ec;
      if (fs::exists(path, ec) && !ec && !overwrite) {
        error = "file already exists";
        return false;
      }

      std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
      if (!out.is_open()) {
        error = "failed to open file for writing";
        return false;
      }
      out.write(content.data(), static_cast<std::streamsize>(content.size()));
      if (!out.good()) {
        error = "failed to write file";
        return false;
      }
      return true;
    }

    static Object toolResultText(const oro::runtime::String& text, bool isError) {
      Object::Entries item {
        {"type", String("text")},
        {"text", String(text)}
      };
      Array content(Array::Entries { Object(item) });
      return Object(Object::Entries {
        {"content", content},
        {"isError", Boolean(isError)}
      });
    }

    static Object toolResultStructured(
      const Any& structuredContent,
      bool isError = false,
      const oro::runtime::String& textOverride = ""
    ) {
      const auto text = textOverride.size() > 0
        ? textOverride
        : runtime::JSON::stringify(structuredContent);

      Object::Entries item {
        {"type", String("text")},
        {"text", String(text)}
      };
      Array content(Array::Entries { Object(item) });
      return Object(Object::Entries {
        {"content", content},
        {"structuredContent", structuredContent},
        {"isError", Boolean(isError)}
      });
    }

    static Object makeToolAnnotations(
      const oro::runtime::String& title,
      bool readOnly,
      bool destructive,
      bool idempotent,
      bool openWorld
    ) {
      return Object(Object::Entries {
        {"title", String(title)},
        {"readOnlyHint", Boolean(readOnly)},
        {"destructiveHint", Boolean(destructive)},
        {"idempotentHint", Boolean(idempotent)},
        {"openWorldHint", Boolean(openWorld)}
      });
    }

    static Object makeToolMetadata(
      const oro::runtime::String& category,
      const oro::runtime::String& whenToUse,
      const oro::runtime::String& returns,
      const Vector<oro::runtime::String>& examples = {},
      const Vector<oro::runtime::String>& prefers = {},
      const Vector<oro::runtime::String>& tags = {}
    ) {
      Array exampleItems;
      for (const auto& example : examples) {
        exampleItems.push(String(example));
      }

      Array prefersItems;
      for (const auto& item : prefers) {
        prefersItems.push(String(item));
      }

      Array tagItems;
      for (const auto& tag : tags) {
        tagItems.push(String(tag));
      }

      return Object(Object::Entries {
        {"category", String(category)},
        {"when_to_use", String(whenToUse)},
        {"returns", String(returns)},
        {"examples", exampleItems},
        {"preferred_over", prefersItems},
        {"tags", tagItems}
      });
    }

    struct ToolRegistry {
      Vector<runtime::mcp::Tool> tools;
      std::unordered_map<oro::runtime::String, std::function<Object(const Object&)>> handlers;
    };

    static void addTool(
      ToolRegistry& registry,
      const runtime::mcp::Tool& tool,
      std::function<Object(const Object&)> handler
    ) {
      registry.tools.push_back(tool);
      registry.handlers.insert_or_assign(tool.name, std::move(handler));
    }

    static std::optional<oro::runtime::String> requireString(
      const Object& obj,
      const char* key,
      oro::runtime::String& error,
      bool allowEmpty = false
    ) {
      if (!obj.contains(key) || !obj.get(key).isString()) {
        error = oro::runtime::String("missing required string field '") + key + "'";
        return std::nullopt;
      }
      const auto v = obj.get(key).as<String>().value();
      if (!allowEmpty && v.size() == 0) {
        error = oro::runtime::String("field '") + key + "' must be non-empty";
        return std::nullopt;
      }
      return v;
    }

    static size_t getOptionalSize(const Object& obj, const char* key, size_t fallback, size_t maximum) {
      if (!obj.contains(key)) return fallback;
      const auto& any = obj.get(key);
      if (!any.isNumber()) return fallback;
      const auto n = any.as<Number>().value();
      if (!std::isfinite(n) || n < 0 || std::floor(n) != n) return fallback;
      if (n > static_cast<double>(maximum)) return maximum;
      return static_cast<size_t>(n);
    }

    static bool getOptionalBool(const Object& obj, const char* key, bool fallback) {
      if (!obj.contains(key)) return fallback;
      const auto& any = obj.get(key);
      if (!any.isBoolean()) return fallback;
      return any.as<Boolean>().value();
    }

    static oro::runtime::String getOptionalString(const Object& obj, const char* key, const oro::runtime::String& fallback = "") {
      if (!obj.contains(key)) return fallback;
      const auto& any = obj.get(key);
      if (!any.isString()) return fallback;
      return any.as<String>().value();
    }

    static Vector<oro::runtime::String> getStringArray(const Object& obj, const char* key, oro::runtime::String& error) {
      error = "";
      Vector<oro::runtime::String> result;
      if (!obj.contains(key)) {
        error = oro::runtime::String("missing required field '") + key + "'";
        return result;
      }
      const auto& any = obj.get(key);
      if (!any.isArray()) {
        error = oro::runtime::String("field '") + key + "' must be an array";
        return result;
      }
      for (const auto& entry : any.as<Array>().value()) {
        if (!entry.isString()) {
          error = oro::runtime::String("field '") + key + "' must be an array of strings";
          result.clear();
          return result;
        }
        result.push_back(entry.as<String>().value());
      }
      return result;
    }

    static oro::runtime::String quotePosix(const oro::runtime::String& value) {
      oro::runtime::String out = "'";
      for (char c : value) {
        if (c == '\'') {
          out += "'\\''";
        } else {
          out.push_back(c);
        }
      }
      out += "'";
      return out;
    }

    static oro::runtime::String quoteCmd(const oro::runtime::String& value) {
      bool needs = value.empty();
      for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '^' || c == '&' || c == '<' || c == '>' || c == '|') {
          needs = true;
          break;
        }
      }
      if (!needs) return value;
      oro::runtime::String out = "\"";
      for (char c : value) {
        if (c == '"') {
          out += "\"\"";
        } else {
          out.push_back(c);
        }
      }
      out += "\"";
      return out;
    }

    static oro::runtime::String joinCommand(const oro::runtime::String& executable, const Vector<oro::runtime::String>& args) {
      oro::runtime::String out;
      if (runtime::platform.win) {
        out += quoteCmd(executable);
        for (const auto& arg : args) {
          out.push_back(' ');
          out += quoteCmd(arg);
        }
        return out;
      }

      out += quotePosix(executable);
      for (const auto& arg : args) {
        out.push_back(' ');
        out += quotePosix(arg);
      }
      return out;
    }

    struct ExecResult {
      int exitCode = 0;
      bool timedOut = false;
      oro::runtime::String stdoutText;
      oro::runtime::String stderrText;
      oro::runtime::String commandLine;
      oro::runtime::String cwd;
    };

    static ExecResult execCli(
      const Options& options,
      const Vector<oro::runtime::String>& rawArgs,
      const fs::path& cwd,
      const oro::runtime::String& stdinText,
      int timeoutMs,
      size_t maxOutputBytes,
      bool quiet,
      bool noColor
    ) {
      ExecResult result;
      result.cwd = cwd.string();

      Vector<oro::runtime::String> args;
      args.reserve(rawArgs.size() + 4);
      bool hasQuietFlag = false;
      bool hasNoColorFlag = false;
      std::optional<size_t> configInsertAfter;
      std::optional<oro::runtime::String> injectedConfigPath;
      for (const auto& arg : rawArgs) {
        if (arg == "--quiet" || arg == "-q") {
          hasQuietFlag = true;
        } else if (arg == "--no-color") {
          hasNoColorFlag = true;
        }
      }

      if (const auto command = findEffectiveCliCommand(rawArgs); command.has_value()) {
        if (cliCommandSupportsConfig(command->name) &&
            !hasExplicitConfigArg(rawArgs, command->subcommandIndex + 1)) {
          bool activeConfigExists = false;
          const auto activeConfigRel = resolveActiveConfigRelativePath(options, options.workspaceRoot, &activeConfigExists);
          if (!activeConfigRel.empty() && (!options.configPath.empty() || activeConfigExists)) {
            const auto activeConfigAbs = fs::absolute(options.workspaceRoot / activeConfigRel).lexically_normal();
            configInsertAfter = command->insertAfterIndex;
            injectedConfigPath = activeConfigAbs.string();
          }
        }
      }

      if (quiet && !hasQuietFlag) {
        args.push_back("--quiet");
      }
      if (noColor && !hasNoColorFlag) {
        args.push_back("--no-color");
      }
      for (size_t i = 0; i < rawArgs.size(); i++) {
        args.push_back(rawArgs[i]);
        if (configInsertAfter.has_value() && injectedConfigPath.has_value() && *configInsertAfter == i) {
          args.push_back("--config");
          args.push_back(*injectedConfigPath);
        }
      }

      const auto executable = options.cliExecutable.size() > 0 ? options.cliExecutable : options.cliDisplayName;
      result.commandLine = joinCommand(executable, args);

      oro::runtime::String processCommand = executable;
      oro::runtime::String processArgv;
      for (size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
          processArgv.push_back(static_cast<char>(0x01));
        }
        processArgv += args[index];
      }

      oro::runtime::Vector<oro::runtime::String> envs;

      oro::runtime::process::ProcessConfig cfg;
      cfg.bufferSize = 131072;
      cfg.useDirectArguments = true;
      cfg.argumentCount = args.size();
      cfg.rawOutput = true;

      std::mutex mutex;
      auto pushBounded = [&](oro::runtime::String& target, const oro::runtime::String& chunk, bool addNewline) {
        std::lock_guard<std::mutex> lock(mutex);
        if (target.size() >= maxOutputBytes) return;
        const auto remaining = maxOutputBytes - target.size();
        oro::runtime::String piece = chunk;
        if (addNewline) {
          piece += "\n";
        }
        if (piece.size() > remaining) {
          piece.resize(remaining);
        }
        target += piece;
      };

      oro::runtime::process::Process process(
        processCommand,
        processArgv,
        envs,
        cwd.string(),
        [&](const oro::runtime::String& out) { pushBounded(result.stdoutText, out, false); },
        [&](const oro::runtime::String& out) { pushBounded(result.stderrText, out, false); },
        nullptr,
        true,
        cfg
      );

      process.open();

      if (stdinText.size() > 0) {
        process.write(stdinText);
      }
      process.closeStdin();

      if (timeoutMs > 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!process.closed.load() && std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (!process.closed.load()) {
          result.timedOut = true;
          process.kill();
        }
      }

      const int code = process.wait();
      result.exitCode = result.timedOut ? 124 : code;
      return result;
    }

    static Object resultFromExec(const ExecResult& exec) {
      return Object(Object::Entries {
        {"exit_code", Number(exec.exitCode)},
        {"timed_out", Boolean(exec.timedOut)},
        {"stdout", String(exec.stdoutText)},
        {"stderr", String(exec.stderrText)},
        {"cwd", String(exec.cwd)},
        {"command", String(exec.commandLine)}
      });
    }

    static Object handleToolsList(const ToolRegistry& registry) {
      Array::Entries entries;
      entries.reserve(registry.tools.size());
      for (const auto& tool : registry.tools) {
        entries.push_back(Object(tool.toJSON()));
      }
      return Object(Object::Entries {
        {"tools", Array(entries)}
      });
    }

    static Object capabilities(bool mcp2025 = false) {
      Object resources(Object::Entries { {"listChanged", Boolean(false)} });
      if (mcp2025) {
        resources.set("subscribe", Boolean(false));
      }
      return Object(Object::Entries {
        {"tools", Object(Object::Entries {{"listChanged", Boolean(false)}})},
        {"resources", resources},
        {"prompts", Object(Object::Entries {{"listChanged", Boolean(false)}})}
      });
    }

    struct Resources {
      runtime::Vector<runtime::mcp::ResourceDescriptor> descriptors;
    };

    static std::optional<fs::path> resolveConfigReference(
      const fs::path& workspaceRoot,
      const fs::path& baseDir,
      const oro::runtime::String& rawValue,
      bool requireExisting,
      oro::runtime::String& error
    ) {
      error = "";
      const auto value = trimCopy(rawValue);
      if (value.empty()) {
        error = "empty path";
        return std::nullopt;
      }

      if (value[0] == '$' || value[0] == '~') {
        error = "non-workspace path";
        return std::nullopt;
      }

      fs::path rel = fs::path(value).lexically_normal();
      if (rel.empty()) {
        error = "invalid path";
        return std::nullopt;
      }
      if (rel.is_absolute() || rel.has_root_path() || rel.has_root_name() || rel.has_root_directory()) {
        error = "absolute paths are not allowed";
        return std::nullopt;
      }

      std::error_code ec;
      const auto rootCanonical = fs::weakly_canonical(workspaceRoot, ec);
      if (ec) {
        error = "failed to resolve workspace root: " + ec.message();
        return std::nullopt;
      }

      fs::path candidate = (baseDir / rel).lexically_normal();
      if (requireExisting) {
        auto resolved = fs::canonical(candidate, ec);
        if (ec) {
          error = "path does not exist: " + ec.message();
          return std::nullopt;
        }
        if (!isSubpath(rootCanonical, resolved)) {
          error = "path escapes workspace root";
          return std::nullopt;
        }
        return resolved;
      }

      auto resolved = fs::weakly_canonical(candidate, ec);
      if (ec) {
        error = "failed to resolve path: " + ec.message();
        return std::nullopt;
      }
      if (!isSubpath(rootCanonical, resolved)) {
        error = "path escapes workspace root";
        return std::nullopt;
      }
      return resolved;
    }

    static void addResourceDescriptor(
      Resources& resources,
      std::unordered_set<oro::runtime::String>& seen,
      runtime::mcp::ResourceDescriptor descriptor
    ) {
      if (descriptor.uri.empty()) return;
      if (!seen.insert(descriptor.uri).second) return;
      resources.descriptors.push_back(std::move(descriptor));
    }

    static void addMetadataField(runtime::mcp::ResourceDescriptor& desc, const oro::runtime::String& key, const Any& value) {
      if (desc.metadata.isNull()) {
        desc.metadata = Object(Object::Entries {{key, value}});
        return;
      }
      if (!desc.metadata.isObject()) {
        return;
      }
      auto& obj = desc.metadata.as<Object>();
      obj.set(key, value);
    }

    static Resources buildResources(const Options& options, const fs::path& workspaceRoot) {
      Resources resources;
      std::unordered_set<oro::runtime::String> seen;
      const auto workspaceRootCanonical = normalizeExistingPath(workspaceRoot, true).value_or(workspaceRoot.lexically_normal());

      runtime::mcp::ResourceDescriptor root;
      root.uri = "workspace:/";
      root.name = "workspace";
      root.description = "Workspace root listing. Use this first to discover files before reading or editing them.";
      root.mimeType = "application/json";
      addResourceDescriptor(resources, seen, std::move(root));

      bool activeConfigExists = false;
      const fs::path configRelPath = resolveActiveConfigRelativePath(options, workspaceRoot, &activeConfigExists);
      const auto configRelGeneric = configRelPath.lexically_normal().generic_string();
      if (activeConfigExists) {
        runtime::mcp::ResourceDescriptor config;
        config.uri = "workspace:/" + configRelGeneric;
        config.name = "config";
        config.description = "Primary project configuration file used by Oro CLI commands and builds.";
        config.mimeType = "text/plain";
        addResourceDescriptor(resources, seen, std::move(config));
      }

      auto addWorkspaceDoc = [&](const fs::path& relPath, const oro::runtime::String& name, const oro::runtime::String& description) {
        std::error_code ec;
        const auto absolute = (workspaceRoot / relPath).lexically_normal();
        if (!fs::exists(absolute, ec) || ec) {
          return;
        }

        const auto relGeneric = relPath.lexically_normal().generic_string();
        if (relGeneric.empty()) {
          return;
        }

        const bool isDir = fs::is_directory(absolute, ec) && !ec;
        runtime::mcp::ResourceDescriptor descriptor;
        descriptor.uri = "workspace:/" + relGeneric;
        descriptor.name = name;
        descriptor.description = description;
        descriptor.mimeType = guessMimeType(absolute, isDir);
        descriptor.metadata = Object(Object::Entries {
          {"category", String("guide")},
          {"audience", String("human-and-agent")},
          {"workspace_path", String(relGeneric)}
        });
        addResourceDescriptor(resources, seen, std::move(descriptor));
      };

      addWorkspaceDoc("README.md", "readme", "Repository overview, release context, and high-level development guidance.");
      addWorkspaceDoc("docs/llms.txt", "llm-reference", "Compact agent-oriented map of the runtime surface, APIs, and installed reference documents.");
      addWorkspaceDoc("docs/MCP.md", "mcp-guide", "CLI/runtime MCP behavior, transport notes, and integration guidance.");
      addWorkspaceDoc("docs/BUILD_ENVIRONMENT.md", "source-build-environment", "Runtime source-build environment controls, including the independent NO_ANDROID and NO_IOS target exclusions.");
      addWorkspaceDoc("api", "api-tree", "Workspace API reference and declaration tree for oro:* modules.");
      addWorkspaceDoc("api/README.md", "api-reference", "Generated JavaScript API reference for oro:* modules.");
      addWorkspaceDoc("api/CONFIG.md", "config-reference", "Generated configuration reference for oro.toml and .ororc keys.");
      addWorkspaceDoc("api/CLI.md", "cli-reference", "Generated CLI reference for oroc commands and options.");
      addWorkspaceDoc("share/man/man1", "cli-manpages", "Workspace-generated man1 pages for oroc commands.");
      addWorkspaceDoc("share/man/man3", "api-manpages", "Workspace-generated man3 pages for JavaScript and C APIs.");
      addWorkspaceDoc("share/man/man7", "guide-manpages", "Workspace-generated man7 guides for IPC routes, concepts, and agent workflows.");
      addWorkspaceDoc("share/man/man7/oro-agent-workflows.7", "agent-workflows", "Agent-oriented workflow guide for exploring, editing, and validating Oro projects.");
      addWorkspaceDoc("share/man/man7/oro-api-concepts.7", "api-concepts", "Concept guide for the runtime API surface and common development patterns.");
      addWorkspaceDoc("share/man/man7/oro-ipc-routes.7", "ipc-routes", "Generated catalog of statically discoverable IPC routes across the JavaScript API surface.");

      if (const auto runtimeRoot = resolveRuntimeInstallRoot(options); runtimeRoot.has_value()) {
        auto addRuntimeDoc = [&](const fs::path& absolutePath, const oro::runtime::String& uri, const oro::runtime::String& name, const oro::runtime::String& description) {
          auto resolved = normalizeExistingPath(absolutePath, false);
          if (!resolved.has_value()) {
            return;
          }

          if (isSubpath(workspaceRootCanonical, *resolved)) {
            return;
          }

          std::error_code runtimeEc;
          const bool isDir = fs::is_directory(*resolved, runtimeEc) && !runtimeEc;
          runtime::mcp::ResourceDescriptor descriptor;
          descriptor.uri = uri;
          descriptor.name = name;
          descriptor.description = description;
          descriptor.mimeType = guessMimeType(*resolved, isDir);
          descriptor.metadata = Object(Object::Entries {
            {"category", String("guide")},
            {"audience", String("human-and-agent")},
            {"source", String("runtime-installation")},
            {"runtime_path", String(resolved->string())}
          });
          addResourceDescriptor(resources, seen, std::move(descriptor));
        };

        const auto runtimeDocsRoot = *runtimeRoot / "share" / "doc" / "oroc";
        addRuntimeDoc(runtimeDocsRoot / "README.md", "runtime-doc:/README.md", "runtime-readme", "Installed runtime overview and project setup reference.");
        addRuntimeDoc(runtimeDocsRoot / "docs" / "BUILD_ENVIRONMENT.md", "runtime-doc:/BUILD_ENVIRONMENT.md", "runtime-build-environment", "Installed runtime source-build environment contract, including independent Android and iOS target exclusions.");
        addRuntimeDoc(runtimeDocsRoot / "MCP.md", "runtime-doc:/MCP.md", "runtime-mcp-guide", "Installed MCP integration guide for Oro CLI and runtime workflows.");
        addRuntimeDoc(runtimeDocsRoot / "llms.txt", "runtime-doc:/llms.txt", "runtime-llm-reference", "Installed compact agent-oriented map of the runtime surface, discovery order, and reference docs.");
        addRuntimeDoc(*runtimeRoot / "api", "runtime-doc:/api", "runtime-api-tree", "Installed API directory containing generated references and declarations for oro:* modules.");
        addRuntimeDoc(*runtimeRoot / "api" / "README.md", "runtime-doc:/api/README.md", "runtime-api-reference", "Installed generated JavaScript API reference for oro:* modules.");
        addRuntimeDoc(*runtimeRoot / "api" / "CONFIG.md", "runtime-doc:/api/CONFIG.md", "runtime-config-reference", "Installed generated configuration reference for oro.toml and .ororc keys.");
        addRuntimeDoc(*runtimeRoot / "api" / "CLI.md", "runtime-doc:/api/CLI.md", "runtime-cli-reference", "Installed generated CLI reference for oroc commands and options.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man1", "runtime-doc:/man1", "runtime-cli-manpages", "Installed man1 tree for oroc commands.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man3", "runtime-doc:/man3", "runtime-api-manpages", "Installed man3 tree for JavaScript and C APIs.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man7", "runtime-doc:/man7", "runtime-guide-manpages", "Installed man7 guide tree for IPC routes, concepts, and workflows.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man7" / "oro-agent-workflows.7", "runtime-doc:/man7/oro-agent-workflows.7", "runtime-agent-workflows", "Installed agent-oriented workflow guide for exploring, editing, and validating Oro projects.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man7" / "oro-api-concepts.7", "runtime-doc:/man7/oro-api-concepts.7", "runtime-api-concepts", "Installed concept guide for the runtime API surface and common development patterns.");
        addRuntimeDoc(*runtimeRoot / "share" / "man" / "man7" / "oro-ipc-routes.7", "runtime-doc:/man7/oro-ipc-routes.7", "runtime-ipc-routes", "Installed catalog of statically discoverable IPC routes across the JavaScript API surface.");
      }

      oro::runtime::String error;
      auto configResolved = resolveConfigReference(workspaceRoot, workspaceRoot, configRelGeneric, true, error);
      if (!configResolved.has_value()) {
        return resources;
      }

      const fs::path configDir = configResolved->parent_path();
      bool configTruncated = false;
      auto configContent = readFileBounded(*configResolved, kDefaultMaxFileBytes, configTruncated, error);
      if (!configContent.has_value() || configTruncated) {
        return resources;
      }

      const auto configFormat = detectConfigFormatForPath(*configResolved);

      auto addPathFromConfig = [&](const oro::runtime::String& keyPath, const oro::runtime::String& rawPath) {
        oro::runtime::String resolveError;
        auto resolved = resolveConfigReference(workspaceRoot, configDir, rawPath, true, resolveError);
        if (!resolved.has_value()) return;

        std::error_code ec;
        const bool isDir = fs::is_directory(*resolved, ec) && !ec;
        const auto mime = guessMimeType(*resolved, isDir);

        auto relPath = fs::relative(*resolved, workspaceRoot, ec);
        if (ec) return;
        const auto relGeneric = relPath.lexically_normal().generic_string();
        if (relGeneric.empty()) return;

        runtime::mcp::ResourceDescriptor d;
        d.uri = "workspace:/" + relGeneric;
        d.name = relGeneric;
        d.description = "Discovered from config: " + keyPath;
        d.mimeType = mime;
        d.metadata = Object(Object::Entries {
          {"source", String(configRelGeneric)},
          {"key", String(keyPath)}
        });
        addResourceDescriptor(resources, seen, std::move(d));
      };

      auto addSplitPaths = [&](const oro::runtime::String& keyPath, const oro::runtime::String& raw) {
        size_t start = 0;
        while (start < raw.size()) {
          auto end = raw.find(';', start);
          if (end == oro::runtime::String::npos) {
            end = raw.size();
          }
          auto piece = trimCopy(raw.substr(start, end - start));
          if (!piece.empty()) {
            addPathFromConfig(keyPath, piece);
          }
          start = end + 1;
        }
      };

      auto addStringOrArray = [&](const oro::runtime::String& keyPath, const runtime::TOML::Value& value) {
        if (value.is(runtime::TOML::Type::String)) {
          addSplitPaths(keyPath, value.asString());
        } else if (value.is(runtime::TOML::Type::Array)) {
          for (const auto& entry : value.asArray()) {
            if (!entry.is(runtime::TOML::Type::String)) continue;
            addSplitPaths(keyPath, entry.asString());
          }
        }
      };

      auto addCopyList = [&](const oro::runtime::String& keyPath, const runtime::TOML::Value& value) {
        auto addItem = [&](oro::runtime::String item) {
          item = trimCopy(std::move(item));
          if (item.empty()) return;
          auto pos = item.find('=');
          if (pos == oro::runtime::String::npos) {
            pos = item.find(':');
          }
          if (pos != oro::runtime::String::npos) {
            item = trimCopy(item.substr(0, pos));
          }
          addPathFromConfig(keyPath, item);
        };

        if (value.is(runtime::TOML::Type::String)) {
          addItem(value.asString());
        } else if (value.is(runtime::TOML::Type::Array)) {
          for (const auto& entry : value.asArray()) {
            if (!entry.is(runtime::TOML::Type::String)) continue;
            addItem(entry.asString());
          }
        }
      };

      auto addTablePathValues = [&](const oro::runtime::String& keyPath, const runtime::TOML::Value& value, const auto& selfRef) -> void {
        if (!value.is(runtime::TOML::Type::Table)) return;
        for (const auto& kv : value.asTable()) {
          const auto childKey = keyPath.size() > 0 ? keyPath + "." + kv.first : kv.first;
          const auto& child = kv.second;
          if (child.is(runtime::TOML::Type::String)) {
            addPathFromConfig(childKey, child.asString());
          } else if (child.is(runtime::TOML::Type::Array)) {
            for (const auto& entry : child.asArray()) {
              if (!entry.is(runtime::TOML::Type::String)) continue;
              addPathFromConfig(childKey, entry.asString());
            }
          } else if (child.is(runtime::TOML::Type::Table)) {
            selfRef(childKey, child, selfRef);
          }
        }
      };

      auto addCopyMapPaths = [&](const oro::runtime::String& copyMapRaw) {
        addPathFromConfig("build.copy_map", copyMapRaw);

        oro::runtime::String copyMapError;
        auto copyMapResolved = resolveConfigReference(workspaceRoot, configDir, copyMapRaw, true, copyMapError);
        if (!copyMapResolved.has_value()) {
          return;
        }

        const fs::path mapDir = copyMapResolved->parent_path();
        const auto ext = lowerCopy(copyMapResolved->extension().string());
        size_t added = 0;
        const size_t maxMapped = 200;

        auto addCopyMapEntry = [&](const oro::runtime::String& rawPath) -> bool {
          oro::runtime::String resolveError;
          auto resolved = resolveConfigReference(workspaceRoot, mapDir, rawPath, true, resolveError);
          if (!resolved.has_value()) return false;

          std::error_code resourceEc;
          const bool isDir = fs::is_directory(*resolved, resourceEc) && !resourceEc;
          const auto mime = guessMimeType(*resolved, isDir);

          auto relPath = fs::relative(*resolved, workspaceRoot, resourceEc);
          if (resourceEc) return false;
          const auto relGeneric = relPath.lexically_normal().generic_string();
          if (relGeneric.empty()) return false;

          runtime::mcp::ResourceDescriptor d;
          d.uri = "workspace:/" + relGeneric;
          d.name = relGeneric;
          d.description = "Discovered from copy map: " + copyMapResolved->filename().string();
          d.mimeType = mime;
          d.metadata = Object(Object::Entries {
            {"source", String(configRelGeneric)},
            {"key", String("build.copy_map")}
          });
          addResourceDescriptor(resources, seen, std::move(d));
          return true;
        };

        try {
          if (ext == ".toml") {
            const auto mapDoc = runtime::TOML::parseFile(*copyMapResolved);
            if (mapDoc.isTable()) {
              for (const auto& kv : mapDoc.asTable()) {
                if (added >= maxMapped) break;
                if (addCopyMapEntry(kv.first)) {
                  added++;
                }
              }
            }
          } else if (ext == ".ini" || ext == ".map") {
            const auto flat = runtime::INI::parseFileFlat(*copyMapResolved);
            for (const auto& kv : flat) {
              if (added >= maxMapped) break;
              if (addCopyMapEntry(kv.first)) {
                added++;
              }
            }
          }
        } catch (...) {
        }
      };

      if (configFormat == runtime::config::UserConfigFormat::Toml) {
        runtime::TOML::Value doc;
        try {
          doc = runtime::TOML::parse(*configContent);
        } catch (...) {
          return resources;
        }
        if (!doc.isTable()) {
          return resources;
        }

        const auto& build = doc["build"];
        if (build.isTable()) {
          addCopyList("build.copy", build["copy"]);
          addStringOrArray("build.input", build["input"]);
          addStringOrArray("build.output", build["output"]);
          addStringOrArray("build.script", build["script"]);

          const auto& copyMap = build["copy_map"];
          if (copyMap.is(runtime::TOML::Type::String)) {
            addCopyMapPaths(copyMap.asString());
          }

          addTablePathValues("build.extensions", build["extensions"], addTablePathValues);
        }

        const auto& i18n = doc["i18n"];
        if (i18n.isTable()) {
          addTablePathValues("i18n.locales", i18n["locales"], addTablePathValues);
        }

        const auto& ios = doc["ios"];
        if (ios.isTable()) {
          addStringOrArray("ios.icon", ios["icon"]);
        }

        const auto& android = doc["android"];
        if (android.isTable()) {
          addStringOrArray("android.icon", android["icon"]);
        }
      } else {
        runtime::INI::Document doc;
        try {
          doc = runtime::INI::parseDocument(*configContent);
        } catch (...) {
          return resources;
        }

        auto addIniStringOrList = [&](const oro::runtime::String& keyPath, const runtime::INI::Value& value) {
          if (value.isScalar()) {
            addSplitPaths(keyPath, value.asString());
          } else if (value.isList()) {
            for (const auto& entry : value.asList()) {
              addSplitPaths(keyPath, entry);
            }
          }
        };

        auto addIniCopyList = [&](const oro::runtime::String& keyPath, const runtime::INI::Value& value) {
          auto addItem = [&](oro::runtime::String item) {
            item = trimCopy(std::move(item));
            if (item.empty()) return;
            auto pos = item.find('=');
            if (pos == oro::runtime::String::npos) {
              pos = item.find(':');
            }
            if (pos != oro::runtime::String::npos) {
              item = trimCopy(item.substr(0, pos));
            }
            addPathFromConfig(keyPath, item);
          };

          if (value.isScalar()) {
            addItem(value.asString());
          } else if (value.isList()) {
            for (const auto& entry : value.asList()) {
              addItem(entry);
            }
          }
        };

        auto addIniSectionPathValues = [&](const oro::runtime::String& sectionPrefix) {
          for (const auto& sectionEntry : doc.sections()) {
            const auto& sectionName = sectionEntry.first;
            if (sectionName.empty()) continue;
            if (sectionName != sectionPrefix && !startsWith(sectionName, (sectionPrefix + ".").c_str())) {
              continue;
            }

            const auto& section = sectionEntry.second;
            for (const auto& key : section.order()) {
              const auto fullKey = sectionName + "." + key;
              const auto& value = section.value(key);
              if (value.isScalar()) {
                addPathFromConfig(fullKey, value.asString());
              } else if (value.isList()) {
                for (const auto& entry : value.asList()) {
                  addPathFromConfig(fullKey, entry);
                }
              }
            }
          }
        };

        if (doc.hasSection("build")) {
          const auto& build = doc.section("build");
          if (build.has("copy")) addIniCopyList("build.copy", build.value("copy"));
          if (build.has("input")) addIniStringOrList("build.input", build.value("input"));
          if (build.has("output")) addIniStringOrList("build.output", build.value("output"));
          if (build.has("script")) addIniStringOrList("build.script", build.value("script"));
          if (build.has("copy_map") && build.value("copy_map").isScalar()) {
            addCopyMapPaths(build.value("copy_map").asString());
          }
        }

        addIniSectionPathValues("build.extensions");
        addIniSectionPathValues("i18n.locales");

        if (doc.hasSection("ios")) {
          const auto& ios = doc.section("ios");
          if (ios.has("icon")) addIniStringOrList("ios.icon", ios.value("icon"));
        }

        if (doc.hasSection("android")) {
          const auto& android = doc.section("android");
          if (android.has("icon")) addIniStringOrList("android.icon", android.value("icon"));
        }
      }

      return resources;
    }

    static bool resourceMetadataStringEquals(const runtime::mcp::ResourceDescriptor& desc, const char* key, const char* expected) {
      if (key == nullptr || expected == nullptr) {
        return false;
      }
      if (!desc.metadata.isObject()) {
        return false;
      }
      const auto& metadata = desc.metadata.as<Object>();
      if (!metadata.contains(key)) {
        return false;
      }
      const auto& value = metadata.get(key);
      return value.isString() && value.as<String>().value() == expected;
    }

    static bool isSearchableDocResource(const runtime::mcp::ResourceDescriptor& desc) {
      if (desc.uri == "workspace:/") {
        return false;
      }
      if (!startsWith(desc.uri, "workspace:/") && !startsWith(desc.uri, "runtime-doc:/")) {
        return false;
      }
      if (desc.name == "config") {
        return true;
      }
      return resourceMetadataStringEquals(desc, "category", "guide");
    }

    static bool isSearchableDocFile(const fs::path& path) {
      const auto filename = lowerCopy(path.filename().string());
      if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".d.ts") {
        return true;
      }

      const auto ext = lowerCopy(path.extension().string());
      if (ext == ".md" || ext == ".txt" || ext == ".toml" || ext == ".ini") {
        return true;
      }
      if (ext == ".1" || ext == ".3" || ext == ".7") {
        return true;
      }
      if (filename == "readme" || filename == "license" || filename == "copying") {
        return true;
      }
      return false;
    }

    static oro::runtime::String normalizeSearchText(oro::runtime::String value) {
      for (auto& c : value) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
          c = static_cast<char>(std::tolower(uc));
        } else {
          c = ' ';
        }
      }
      return trimCopy(value);
    }

    static Vector<oro::runtime::String> tokenizeSearchQuery(const oro::runtime::String& query) {
      Vector<oro::runtime::String> tokens;
      const auto normalized = normalizeSearchText(query);
      if (normalized.empty()) {
        return tokens;
      }

      size_t start = 0;
      while (start < normalized.size()) {
        while (start < normalized.size() && normalized[start] == ' ') {
          start++;
        }
        if (start >= normalized.size()) {
          break;
        }
        auto end = normalized.find(' ', start);
        if (end == oro::runtime::String::npos) {
          end = normalized.size();
        }
        auto token = normalized.substr(start, end - start);
        if (!token.empty()) {
          tokens.push_back(token);
        }
        start = end + 1;
      }

      return tokens;
    }

    static int scoreSearchText(const oro::runtime::String& haystack, const oro::runtime::String& query, const Vector<oro::runtime::String>& tokens) {
      if (haystack.empty()) {
        return 0;
      }

      int score = 0;
      if (!query.empty() && haystack.find(query) != oro::runtime::String::npos) {
        score += 100;
      }

      for (const auto& token : tokens) {
        if (haystack.find(token) != oro::runtime::String::npos) {
          score += 15;
        }
      }

      return score;
    }

    static oro::runtime::String makeSearchExcerpt(const oro::runtime::String& line, const oro::runtime::String& query, const Vector<oro::runtime::String>& tokens) {
      if (line.empty()) {
        return "";
      }

      size_t anchor = oro::runtime::String::npos;
      if (!query.empty()) {
        anchor = lowerCopy(line).find(query);
      }

      if (anchor == oro::runtime::String::npos) {
        const auto lowered = lowerCopy(line);
        for (const auto& token : tokens) {
          anchor = lowered.find(token);
          if (anchor != oro::runtime::String::npos) {
            break;
          }
        }
      }

      constexpr size_t kMaxExcerpt = 220;
      if (anchor == oro::runtime::String::npos || line.size() <= kMaxExcerpt) {
        return trimCopy(line);
      }

      size_t start = anchor > 60 ? anchor - 60 : 0;
      if (start + kMaxExcerpt > line.size()) {
        start = line.size() > kMaxExcerpt ? line.size() - kMaxExcerpt : 0;
      }

      auto excerpt = trimCopy(line.substr(start, kMaxExcerpt));
      if (start > 0) {
        excerpt = "..." + excerpt;
      }
      if (start + kMaxExcerpt < line.size()) {
        excerpt += "...";
      }
      return excerpt;
    }

    struct Context {
      Options options;
      ToolRegistry tools;
      Resources resources;
      fs::path workspaceRoot;
      std::mutex resourcesMutex;
    };

    struct SearchableDocFile {
      fs::path path;
      oro::runtime::String uri;
      oro::runtime::String title;
      oro::runtime::String description;
      oro::runtime::String scope;
      oro::runtime::String mimeType;
    };

    static Vector<SearchableDocFile> collectSearchableDocFiles(const fs::path& workspaceRoot, const Resources& resources, size_t maxFiles) {
      Vector<SearchableDocFile> files;
      std::unordered_set<oro::runtime::String> seen;
      files.reserve(128);

      auto pushFile = [&](const fs::path& path,
                          const oro::runtime::String& uri,
                          const oro::runtime::String& title,
                          const oro::runtime::String& description,
                          const oro::runtime::String& scope,
                          const oro::runtime::String& mimeType) {
        if (files.size() >= maxFiles) {
          return;
        }
        if (!isSearchableDocFile(path)) {
          return;
        }
        auto normalized = normalizeExistingPath(path, false);
        if (!normalized.has_value()) {
          return;
        }
        const auto key = normalized->string();
        if (!seen.insert(key).second) {
          return;
        }

        files.push_back(SearchableDocFile {
          .path = *normalized,
          .uri = uri,
          .title = title,
          .description = description,
          .scope = scope,
          .mimeType = mimeType
        });
      };

      auto appendDirectory = [&](const fs::path& dir,
                                 const oro::runtime::String& uriBase,
                                 const oro::runtime::String& title,
                                 const oro::runtime::String& description,
                                 const oro::runtime::String& scope) {
        std::error_code iterEc;
        fs::recursive_directory_iterator it(dir, iterEc);
        fs::recursive_directory_iterator end;
        for (; it != end && !iterEc && files.size() < maxFiles; ++it) {
          std::error_code typeEc;
          if (!it->is_regular_file(typeEc) || typeEc) {
            continue;
          }

          std::error_code relEc;
          const auto rel = fs::relative(it->path(), dir, relEc);
          if (relEc || containsDotDot(rel)) {
            continue;
          }

          const auto relGeneric = rel.lexically_normal().generic_string();
          if (relGeneric.empty()) {
            continue;
          }

          pushFile(
            it->path(),
            uriBase + "/" + relGeneric,
            title,
            description,
            scope,
            guessMimeType(it->path(), false)
          );
        }
      };

      for (const auto& desc : resources.descriptors) {
        if (!isSearchableDocResource(desc)) {
          continue;
        }

        std::optional<fs::path> resolved;
        oro::runtime::String scope = startsWith(desc.uri, "runtime-doc:/") ? "runtime" : "workspace";
        if (startsWith(desc.uri, "runtime-doc:/")) {
          resolved = resourcePathFromDescriptor(desc);
        } else if (startsWith(desc.uri, "workspace:/")) {
          oro::runtime::String error;
          const auto rel = desc.uri.substr(std::strlen("workspace:/"));
          resolved = resolveWorkspacePath(workspaceRoot, rel, true, false, false, false, error);
        }

        if (!resolved.has_value()) {
          continue;
        }

        std::error_code ec;
        const bool isDir = fs::is_directory(*resolved, ec) && !ec;
        if (isDir) {
          appendDirectory(*resolved, desc.uri, desc.name, desc.description, scope);
        } else {
          pushFile(*resolved, desc.uri, desc.name, desc.description, scope, desc.mimeType);
        }
      }

      return files;
    }

    static Object searchDocs(Context& ctx, const Object& args) {
      oro::runtime::String error;
      auto query = requireString(args, "query", error);
      if (!query.has_value()) {
        return toolResultText(error, true);
      }

      const auto scope = lowerCopy(getOptionalString(args, "scope", "all"));
      if (scope != "all" && scope != "workspace" && scope != "runtime") {
        return toolResultText("scope must be one of: all, workspace, runtime", true);
      }

      const size_t maxHits = std::max<size_t>(1, getOptionalSize(args, "max_hits", 20, 100));
      const size_t maxFiles = std::max<size_t>(1, getOptionalSize(args, "max_files", 400, 1000));
      const auto normalizedQuery = normalizeSearchText(*query);
      const auto tokens = tokenizeSearchQuery(*query);
      if (normalizedQuery.empty() || tokens.empty()) {
        return toolResultText("query must contain searchable text", true);
      }

      Resources latest;
      {
        std::lock_guard<std::mutex> lock(ctx.resourcesMutex);
        latest = buildResources(ctx.options, ctx.workspaceRoot);
        ctx.resources = latest;
      }

      const auto files = collectSearchableDocFiles(ctx.workspaceRoot, latest, maxFiles);

      struct SearchHit {
        int score = 0;
        oro::runtime::String uri;
        oro::runtime::String path;
        oro::runtime::String scope;
        oro::runtime::String title;
        oro::runtime::String description;
        oro::runtime::String mimeType;
        oro::runtime::String excerpt;
        int line = 0;
        oro::runtime::String matchedIn;
      };

      Vector<SearchHit> hits;
      hits.reserve(128);
      bool truncated = false;
      constexpr size_t kMaxSearchFileBytes = 512 * 1024;

      for (const auto& file : files) {
        if (scope != "all" && file.scope != scope) {
          continue;
        }

        const auto normalizedPath = normalizeSearchText(file.path.filename().string() + " " + file.uri + " " + file.title + " " + file.description);
        const int metadataScore = scoreSearchText(normalizedPath, normalizedQuery, tokens);

        bool contentTruncated = false;
        auto content = readFileBounded(file.path, kMaxSearchFileBytes, contentTruncated, error);
        if (!content.has_value()) {
          continue;
        }

        if (contentTruncated) {
          truncated = true;
        }

        size_t lineStart = 0;
        int lineNumber = 1;
        int fileHitCount = 0;
        while (lineStart <= content->size() && hits.size() < maxHits) {
          auto lineEnd = content->find('\n', lineStart);
          if (lineEnd == oro::runtime::String::npos) {
            lineEnd = content->size();
          }

          auto rawLine = content->substr(lineStart, lineEnd - lineStart);
          auto trimmedLine = trimCopy(rawLine);
          auto normalizedLine = normalizeSearchText(rawLine);
          const int lineScore = scoreSearchText(normalizedLine, normalizedQuery, tokens);
          if (lineScore > 0) {
            hits.push_back(SearchHit {
              .score = lineScore + metadataScore,
              .uri = file.uri,
              .path = file.path.string(),
              .scope = file.scope,
              .title = file.title,
              .description = file.description,
              .mimeType = file.mimeType,
              .excerpt = makeSearchExcerpt(trimmedLine.empty() ? rawLine : trimmedLine, normalizedQuery, tokens),
              .line = lineNumber,
              .matchedIn = "content"
            });
            fileHitCount++;
          }

          if (lineEnd == content->size()) {
            break;
          }
          lineStart = lineEnd + 1;
          lineNumber++;

          if (fileHitCount >= 3) {
            break;
          }
        }

        if (hits.size() >= maxHits) {
          truncated = true;
          break;
        }

        if (metadataScore > 0 && fileHitCount == 0 && hits.size() < maxHits) {
          hits.push_back(SearchHit {
            .score = metadataScore,
            .uri = file.uri,
            .path = file.path.string(),
            .scope = file.scope,
            .title = file.title,
            .description = file.description,
            .mimeType = file.mimeType,
            .excerpt = file.description,
            .line = 0,
            .matchedIn = "metadata"
          });
        }
      }

      std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.uri != b.uri) return a.uri < b.uri;
        return a.line < b.line;
      });

      if (hits.size() > maxHits) {
        hits.resize(maxHits);
        truncated = true;
      }

      Array::Entries resultEntries;
      resultEntries.reserve(hits.size());
      for (const auto& hit : hits) {
        Object entry(Object::Entries {
          {"uri", String(hit.uri)},
          {"path", String(hit.path)},
          {"scope", String(hit.scope)},
          {"title", String(hit.title)},
          {"description", String(hit.description)},
          {"mime_type", String(hit.mimeType)},
          {"excerpt", String(hit.excerpt)},
          {"score", Number(static_cast<int64_t>(hit.score))},
          {"matched_in", String(hit.matchedIn)}
        });
        if (hit.line > 0) {
          entry.set("line", Number(static_cast<int64_t>(hit.line)));
        }
        resultEntries.push_back(std::move(entry));
      }

      Object payload(Object::Entries {
        {"query", String(*query)},
        {"scope", String(scope)},
        {"results", Array(resultEntries)},
        {"truncated", Boolean(truncated)}
      });
      return toolResultStructured(Any(payload));
    }

    static Object handleResourcesList(const Resources& resources) {
      Array::Entries entries;
      entries.reserve(resources.descriptors.size());
      for (const auto& desc : resources.descriptors) {
        entries.push_back(Object(desc.toJSON()));
      }
      return Object(Object::Entries {
        {"resources", Array(entries)}
      });
    }

    static std::optional<Request> parseJsonRpcRequest(const Any& any, Error& error) {
      if (!any.isObject()) {
        error = Error(ErrorCode::InvalidRequest, "JSON-RPC request must be an object");
        return std::nullopt;
      }

      auto& obj = any.as<Object>();

      if (!obj.contains("jsonrpc") ||
          !obj.get("jsonrpc").isString() ||
          obj.get("jsonrpc").as<String>().value() != runtime::mcp::kJsonRpcVersion) {
        error = Error(ErrorCode::InvalidRequest, "jsonrpc must be exactly '2.0'");
        return std::nullopt;
      }

      if (!obj.contains("method") || !obj.get("method").isString()) {
        error = Error(ErrorCode::InvalidRequest, "missing JSON-RPC method");
        return std::nullopt;
      }

      Request req;
      req.jsonrpc = runtime::mcp::kJsonRpcVersion;
      req.method = obj.get("method").as<String>().value();
      if (obj.contains("params")) {
        req.params = obj.get("params");
      } else {
        req.params = Null();
      }
      if (obj.contains("id")) {
        if (!obj.get("id").isString() && !obj.get("id").isNumber()) {
          error = Error(ErrorCode::InvalidRequest, "JSON-RPC request id must be a string or number");
          return std::nullopt;
        }
        req.id = obj.get("id");
      } else {
        req.id = Null();
      }

      return req;
    }

    static Object handleToolCall(Context& ctx, const Object& params, oro::runtime::String& error) {
      error = "";
      auto name = requireString(params, "name", error);
      if (!name.has_value()) {
        return toolResultText(error, true);
      }

      Object args(Object::Entries {});
      if (params.contains("arguments")) {
        if (!params.get("arguments").isObject()) {
          return toolResultText("tool arguments must be an object", true);
        }
        args = params.get("arguments").as<Object>();
      }

      auto it = ctx.tools.handlers.find(*name);
      if (it == ctx.tools.handlers.end()) {
        return toolResultText("unknown tool: " + *name, true);
      }

      for (const auto& tool : ctx.tools.tools) {
        if (tool.name != *name) {
          continue;
        }
        oro::runtime::String validationError;
        if (!tool.validateArguments(
              runtime::JSON::stringify(Any(args)),
              validationError)) {
          return toolResultText(validationError, true);
        }
        break;
      }

      try {
        return it->second(args);
      } catch (const std::exception& e) {
        return toolResultText(oro::runtime::String("tool threw exception: ") + e.what(), true);
      }
    }

    static Object listDirectory(const fs::path& path, size_t maxEntries, bool recursive, size_t maxDepth) {
      Array::Entries entries;
      size_t count = 0;
      std::error_code ec;

      auto shouldHideEntry = [](const oro::runtime::String& relGeneric) -> bool {
        if (relGeneric == ".git" || startsWith(relGeneric, ".git/")) return true;
        if (relGeneric == "build" || startsWith(relGeneric, "build/")) return true;
        if (relGeneric == "tmp" || startsWith(relGeneric, "tmp/")) return true;
        return false;
      };

      auto pushEntry = [&](const fs::directory_entry& entry, const fs::path& base) {
        if (count >= maxEntries) return;
        const auto rel = fs::relative(entry.path(), base, ec);
        if (ec) return;
        if (containsDotDot(rel)) return;
        const auto relGeneric = rel.lexically_normal().generic_string();
        if (relGeneric.empty()) return;
        if (shouldHideEntry(relGeneric)) return;
        const auto kind = entry.is_directory(ec) && !ec ? "dir" : "file";
        entries.push_back(Object(Object::Entries {
          {"path", String(relGeneric)},
          {"kind", String(kind)}
        }));
        count++;
      };

      if (!recursive) {
        for (const auto& entry : fs::directory_iterator(path, ec)) {
          if (ec) break;
          pushEntry(entry, path);
        }
        return Object(Object::Entries {{"entries", Array(entries)}, {"truncated", Boolean(count >= maxEntries)}});
      }

      fs::recursive_directory_iterator it(path, ec);
      fs::recursive_directory_iterator end;
      for (; it != end && !ec; ++it) {
        if (it.depth() >= static_cast<int>(maxDepth)) {
          it.disable_recursion_pending();
        }
        const auto rel = fs::relative(it->path(), path, ec);
        if (ec) continue;
        if (containsDotDot(rel)) {
          it.disable_recursion_pending();
          continue;
        }
        const auto relGeneric = rel.lexically_normal().generic_string();
        std::error_code linkEc;
        if (it->is_symlink(linkEc) && !linkEc) {
          it.disable_recursion_pending();
        }
        std::error_code dirEc;
        if ((it->is_directory(dirEc) && !dirEc) &&
            (relGeneric == "build" || startsWith(relGeneric, "build/") ||
             relGeneric == "tmp" || startsWith(relGeneric, "tmp/") ||
             relGeneric == ".git" || startsWith(relGeneric, ".git/"))) {
          it.disable_recursion_pending();
          continue;
        }
        pushEntry(*it, path);
        if (count >= maxEntries) break;
      }

      return Object(Object::Entries {{"entries", Array(entries)}, {"truncated", Boolean(count >= maxEntries)}});
    }

    static void registerDefaultTools(Context& ctx) {
      using runtime::mcp::ToolBuilder;

      const auto arrayStringItems = Object(Object::Entries {{"type", String("string")}});

      addTool(
        ctx.tools,
        ToolBuilder("run_cli")
          .title("Run Oro CLI command")
          .description("Flexible fallback tool for any `oroc` workflow. Prefer the specialized MCP tools when they already model the action you need, because those tools are easier for clients to select and validate.")
          .annotations(makeToolAnnotations("Run Oro CLI command", false, true, false, true))
          .metadata(makeToolMetadata(
            "cli",
            "Use when you need a command that does not already have a dedicated MCP tool, or when you need full CLI parity.",
            "Structured command execution result with exit code, timeout flag, stdout, stderr, cwd, and a diagnostic command representation.",
            {
              R"({"args":["config","--describe","meta.version"]})",
              R"({"args":["build","--platform=ios","."],"timeout_ms":600000})"
            },
            { "config_get", "config_describe", "build_app", "run_app", "get_versions" },
            { "fallback", "cli", "process", "power-tool" }
          ))
          .addArray("args", "Arguments to pass after the CLI executable, as tokenized argv entries such as [\"build\", \"--platform=ios\", \".\"].", arrayStringItems, true)
          .addString("cwd", "Optional working directory relative to the workspace root. Defaults to the workspace root.", false)
          .addIntegerRange("timeout_ms", "Optional timeout in milliseconds, up to 24 hours. When reached, the process is terminated and the result is marked timed_out=true.", 0, kMaxToolTimeoutMilliseconds, false)
          .addStringMaxLength("stdin", "Optional stdin payload to write before closing stdin, up to 4 MiB.", kMaxWorkspaceWriteBytes, false)
          .addIntegerRange("max_output_bytes", "Maximum captured bytes for stdout and stderr individually. Defaults to 1 MiB each and is capped at 16 MiB.", 1, kMaxToolOutputBytes, false)
          .addBoolean("quiet", "Prepend --quiet unless already present. Defaults to true.", false)
          .addBoolean("no_color", "Prepend --no-color unless already present. Defaults to true.", false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto cliArgs = getStringArray(args, "args", error);
          if (error.size() > 0) {
            return toolResultText(error, true);
          }
          for (const auto& arg : cliArgs) {
            if (arg.find('\0') != oro::runtime::String::npos ||
                arg.find(static_cast<char>(0x01)) != oro::runtime::String::npos) {
              return toolResultText("CLI arguments cannot contain NUL or U+0001 characters", true);
            }
          }

          const bool quiet = getOptionalBool(args, "quiet", true);
          const bool noColor = getOptionalBool(args, "no_color", true);
          const int timeoutMs = static_cast<int>(getOptionalSize(
            args,
            "timeout_ms",
            0,
            static_cast<size_t>(kMaxToolTimeoutMilliseconds)
          ));
          const auto stdinText = getOptionalString(args, "stdin", "");
          if (stdinText.size() > kMaxWorkspaceWriteBytes) {
            return toolResultText("stdin exceeds the 4 MiB limit", true);
          }
          const size_t maxOutputBytes = getOptionalSize(
            args,
            "max_output_bytes",
            kDefaultMaxToolOutputBytes,
            kMaxToolOutputBytes
          );

          fs::path execCwd = ctx.workspaceRoot;
          const auto cwdRel = getOptionalString(args, "cwd", "");
          if (cwdRel.size() > 0) {
            auto resolved = resolveWorkspacePath(ctx.workspaceRoot, cwdRel, true, false, true, false, error);
            if (!resolved.has_value()) {
              return toolResultText(error, true);
            }
            execCwd = *resolved;
          }

          const auto exec = execCli(ctx.options, cliArgs, execCwd, stdinText, timeoutMs, maxOutputBytes, quiet, noColor);
          return toolResultStructured(Any(resultFromExec(exec)));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("read_workspace_file")
          .title("Read workspace text file")
          .description("Read a UTF-8 text file inside the workspace. Use this for exact source inspection when you already know the relative path.")
          .annotations(makeToolAnnotations("Read workspace text file", true, false, true, false))
          .metadata(makeToolMetadata(
            "workspace",
            "Prefer this over `run_cli` for direct source reads within the project tree.",
            "Path, UTF-8 file content, and a truncated flag when the max byte limit is reached.",
            {
              R"({"path":"src/cli/main.cc"})",
              R"({"path":"oro.toml","max_bytes":65536})"
            },
            { "run_cli" },
            { "read", "file", "workspace", "text" }
          ))
          .addString("path", "Path relative to the workspace root.", true)
          .addIntegerRange("max_bytes", "Maximum bytes to return before truncating the content. Defaults to 1 MiB and is capped at 16 MiB.", 1, kMaxFileBytes, false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto rel = requireString(args, "path", error);
          if (!rel.has_value()) return toolResultText(error, true);

          const size_t maxBytes = getOptionalSize(args, "max_bytes", kDefaultMaxFileBytes, kMaxFileBytes);
          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, *rel, true, true, false, false, error);
          if (!resolved.has_value()) {
            return toolResultText(error, true);
          }

          bool truncated = false;
          auto content = readFileBounded(*resolved, maxBytes, truncated, error);
          if (!content.has_value()) {
            return toolResultText(error, true);
          }

          Object payload(Object::Entries {
            {"path", String(fs::relative(*resolved, ctx.workspaceRoot).lexically_normal().generic_string())},
            {"content", String(*content)},
            {"truncated", Boolean(truncated)}
          });
          return toolResultStructured(Any(payload));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("workspace_info")
          .title("Inspect workspace MCP context")
          .description("Return the effective workspace root, config file path, transport, and filesystem policy used by this MCP server instance.")
          .annotations(makeToolAnnotations("Inspect workspace MCP context", true, false, true, false))
          .metadata(makeToolMetadata(
            "workspace",
            "Use first when you need to understand what directory, active project config path, and read policy the server is operating against.",
            "Workspace root, config path and format information, transport mode, and read-outside-workspace policy.",
            {},
            {},
            { "workspace", "context", "introspection" }
          ))
          .build(),
        [&](const Object&) -> Object {
          const auto configRel = resolveActiveConfigRelativePath(ctx.options, ctx.workspaceRoot).generic_string();

          std::error_code ec;
          auto rootCanonical = fs::weakly_canonical(ctx.workspaceRoot, ec);
          const auto rootPath = (ec ? ctx.workspaceRoot : rootCanonical).lexically_normal();

          oro::runtime::String resolveError;
          auto configResolved = resolveWorkspacePath(ctx.workspaceRoot, configRel, true, true, false, false, resolveError);

          Object payload(Object::Entries {
            {"workspace_root", String(rootPath.string())},
            {"config_path", String(configRel)},
            {"config_format", String(configFormatName(detectConfigFormatForPath(configRel)))},
            {"config_exists", Boolean(configResolved.has_value())},
            {"allow_read_outside_workspace", Boolean(ctx.options.allowReadOutsideWorkspace)},
            {"transport", String(ctx.options.useHttp ? "http" : "stdio")}
          });
          if (configResolved.has_value()) {
            payload.set("config_abs", String(configResolved->string()));
          } else if (!resolveError.empty()) {
            payload.set("config_error", String(resolveError));
          }

          return toolResultStructured(Any(payload));
        }
      );

      if (ctx.options.allowReadOutsideWorkspace) {
        addTool(
          ctx.tools,
          ToolBuilder("read_file")
            .title("Read absolute filesystem file")
            .description("Read a file outside the workspace by absolute path. This is intended for trusted local automation that needs host-level context such as SDK files, logs, or generated artifacts.")
            .annotations(makeToolAnnotations("Read absolute filesystem file", true, false, true, true))
            .metadata(makeToolMetadata(
              "filesystem",
              "Use only when the file is intentionally outside the workspace root and the server policy allows it.",
              "Absolute path, mime type, encoding, content, and a truncated flag.",
              {
                R"({"path":"/tmp/build.log"})",
                R"({"path":"/abs/path/icon.png","max_bytes":262144})"
              },
              { "read_workspace_file" },
              { "read", "file", "absolute-path", "host" }
            ))
            .addString("path", "Absolute path to the file.", true)
            .addIntegerRange("max_bytes", "Maximum bytes to return before truncation. Defaults to 1 MiB and is capped at 16 MiB.", 1, kMaxFileBytes, false)
            .build(),
          [&](const Object& args) -> Object {
            oro::runtime::String error;
            auto raw = requireString(args, "path", error);
            if (!raw.has_value()) return toolResultText(error, true);

            fs::path requested = fs::path(*raw).lexically_normal();
            if (!requested.is_absolute()) {
              return toolResultText("path must be absolute", true);
            }

            std::error_code ec;
            auto resolved = fs::canonical(requested, ec);
            if (ec) {
              return toolResultText("path does not exist: " + ec.message(), true);
            }
            if (!fs::is_regular_file(resolved, ec) || ec) {
              return toolResultText("path is not a file", true);
            }

            const size_t maxBytes = getOptionalSize(args, "max_bytes", kDefaultMaxFileBytes, kMaxFileBytes);
            const bool binary = isDefinitelyBinaryExtension(resolved);
            bool truncated = false;

            Object payload(Object::Entries {
              {"path", String(resolved.string())},
              {"mime_type", String(guessMimeType(resolved, false))},
              {"truncated", Boolean(false)}
            });

            if (binary) {
              auto bytes = readFileBytesBounded(resolved, maxBytes, truncated, error);
              if (!bytes.has_value()) return toolResultText(error, true);
              payload.set("encoding", String("base64"));
              payload.set("content", String(runtime::bytes::base64::encode(*bytes)));
              payload.set("truncated", Boolean(truncated));
              return toolResultStructured(Any(payload));
            }

            auto content = readFileBounded(resolved, maxBytes, truncated, error);
            if (!content.has_value()) return toolResultText(error, true);

            payload.set("encoding", String("utf-8"));
            payload.set("content", String(*content));
            payload.set("truncated", Boolean(truncated));
            return toolResultStructured(Any(payload));
          }
        );
      }

      addTool(
        ctx.tools,
        ToolBuilder("write_workspace_file")
          .title("Write workspace text file")
          .description("Write or create a UTF-8 text file inside the workspace. Use for deliberate source/config/document edits when you already know the target relative path.")
          .annotations(makeToolAnnotations("Write workspace text file", false, true, false, false))
          .metadata(makeToolMetadata(
            "workspace",
            "Use after reading or inspecting the target path. This tool changes workspace files and can overwrite content when explicitly allowed.",
            "Confirmation object with ok=true and the written workspace-relative path.",
            {
              R"({"path":"docs/notes.md","content":"hello","create_dirs":true})",
              R"({"path":"oro.toml","content":"[meta]\nversion=\"1.0.0\"\n","overwrite":true})"
            },
            {},
            { "write", "file", "workspace", "destructive" }
          ))
          .addString("path", "Path relative to the workspace root.", true)
          .addStringMaxLength("content", "UTF-8 file contents to write, up to 4 MiB.", kMaxWorkspaceWriteBytes, true)
          .addBoolean("overwrite", "Overwrite an existing file. Defaults to false.", false)
          .addBoolean("create_dirs", "Create parent directories when missing. Defaults to false.", false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto rel = requireString(args, "path", error);
          if (!rel.has_value()) return toolResultText(error, true);

          auto content = requireString(args, "content", error, true);
          if (!content.has_value()) return toolResultText(error, true);
          if (content->size() > kMaxWorkspaceWriteBytes) {
            return toolResultText("content exceeds the 4 MiB write limit", true);
          }

          const bool overwrite = getOptionalBool(args, "overwrite", false);
          const bool createDirs = getOptionalBool(args, "create_dirs", false);
          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, *rel, false, true, false, createDirs, error);
          if (!resolved.has_value()) {
            return toolResultText(error, true);
          }

          if (!writeFile(*resolved, *content, overwrite, error)) {
            return toolResultText(error, true);
          }

          Object payload(Object::Entries {{"ok", Boolean(true)}, {"path", String(*rel)}});
          return toolResultStructured(Any(payload));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("list_workspace")
          .title("List workspace files")
          .description("List files and directories inside the workspace. Hidden implementation directories such as .git, build, and tmp are intentionally skipped to keep discovery focused.")
          .annotations(makeToolAnnotations("List workspace files", true, false, true, false))
          .metadata(makeToolMetadata(
            "workspace",
            "Use for lightweight project discovery before reading files. Prefer this over shelling out to `find` or `ls`.",
            "Entry list with workspace-relative paths, entry kinds, and a truncated flag.",
            {
              R"({"path":"src","recursive":true,"max_depth":2})",
              R"({"recursive":false})"
            },
            { "run_cli" },
            { "list", "workspace", "discovery" }
          ))
          .addString("path", "Directory path relative to workspace root. Defaults to the workspace root.", false)
          .addBoolean("recursive", "Recursively list entries. Defaults to false.", false)
          .addIntegerRange("max_entries", "Maximum entries to return. Defaults to 200 and is capped at 10,000.", 1, kMaxDirectoryEntries, false)
          .addIntegerRange("max_depth", "Maximum recursion depth when recursive=true. Defaults to 4 and is capped at 64.", 0, kMaxDirectoryDepth, false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          const auto rel = getOptionalString(args, "path", "");
          const bool recursive = getOptionalBool(args, "recursive", false);
          const size_t maxEntries = getOptionalSize(args, "max_entries", 200, kMaxDirectoryEntries);
          const size_t maxDepth = getOptionalSize(args, "max_depth", 4, kMaxDirectoryDepth);

          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, rel, true, false, true, false, error);
          if (!resolved.has_value()) {
            return toolResultText(error, true);
          }

          const auto listing = listDirectory(*resolved, maxEntries, recursive, maxDepth);
          return toolResultStructured(Any(listing));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("search_docs")
          .title("Search docs and reference resources")
          .description("Search the advertised workspace and installed runtime docs by topic, keyword, or phrase. Use this before manually reading individual README, CLI, config, API, or manpage resources.")
          .annotations(makeToolAnnotations("Search docs and reference resources", true, false, true, false))
          .metadata(makeToolMetadata(
            "documentation",
            "Use for topic discovery when you know what you want to learn but not which specific resource or manpage contains it.",
            "Ranked result list with resource URIs, line numbers when content matches, score, and short excerpts.",
            {
              R"({"query":"ios signing"})",
              R"({"query":"update manifest signature","scope":"workspace","max_hits":10})"
            },
            { "list_workspace", "read_workspace_file", "read_config", "run_cli" },
            { "search", "docs", "manpage", "discovery", "agent" }
          ))
          .addString("query", "Topic, phrase, or keywords to search for across advertised docs and config references.", true)
          .addString("scope", "Search scope: `all` (default), `workspace`, or `runtime`.", false)
          .addIntegerRange("max_hits", "Maximum result rows to return. Defaults to 20, capped at 100.", 1, 100, false)
          .addIntegerRange("max_files", "Maximum files to scan while searching. Defaults to 400, capped at 1000.", 1, 1000, false)
          .build(),
        [&](const Object& args) -> Object {
          return searchDocs(ctx, args);
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("read_config")
          .title("Read project config")
          .description("Read the active project configuration file, which defaults to `oro.toml`, falls back to `oro.ini` when needed, or uses the custom path supplied when the MCP server started.")
          .annotations(makeToolAnnotations("Read project config", true, false, true, false))
          .metadata(makeToolMetadata(
            "config",
            "Use when you want the exact active config file contents before editing, validating, or describing keys.",
            "Workspace-relative config path, full text content, and a truncated flag.",
            {},
            { "read_workspace_file" },
            { "config", "read", "toml", "ini" }
          ))
          .build(),
        [&](const Object&) -> Object {
          oro::runtime::String error;
          const auto configRel = resolveActiveConfigRelativePath(ctx.options, ctx.workspaceRoot).generic_string();
          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, configRel, true, true, false, false, error);
          if (!resolved.has_value()) {
            return toolResultText(error, true);
          }
          bool truncated = false;
          auto content = readFileBounded(*resolved, kDefaultMaxFileBytes, truncated, error);
          if (!content.has_value()) return toolResultText(error, true);
          Object payload(Object::Entries {
            {"path", String(configRel)},
            {"content", String(*content)},
            {"truncated", Boolean(truncated)}
          });
          return toolResultStructured(Any(payload));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("write_config")
          .title("Write project config")
          .description("Write the active project configuration file. This is a focused variant of `write_workspace_file` for the config path selected by the MCP server.")
          .annotations(makeToolAnnotations("Write project config", false, true, false, false))
          .metadata(makeToolMetadata(
            "config",
            "Use when updating the active project config file, whether that is oro.toml, oro.ini, or an explicitly selected alternate path.",
            "Confirmation object with ok=true and the written config path.",
            {},
            { "write_workspace_file" },
            { "config", "write", "destructive" }
          ))
          .addStringMaxLength("content", "New configuration file content, up to 4 MiB.", kMaxWorkspaceWriteBytes, true)
          .addBoolean("overwrite", "Overwrite an existing file. Defaults to false.", false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto content = requireString(args, "content", error, true);
          if (!content.has_value()) return toolResultText(error, true);
          if (content->size() > kMaxWorkspaceWriteBytes) {
            return toolResultText("content exceeds the 4 MiB write limit", true);
          }
          const bool overwrite = getOptionalBool(args, "overwrite", false);
          const auto configRel = resolveActiveConfigRelativePath(ctx.options, ctx.workspaceRoot).generic_string();
          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, configRel, false, true, false, false, error);
          if (!resolved.has_value()) return toolResultText(error, true);
          if (!writeFile(*resolved, *content, overwrite, error)) {
            return toolResultText(error, true);
          }
          Object payload(Object::Entries {{"ok", Boolean(true)}, {"path", String(configRel)}});
          return toolResultStructured(Any(payload));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("validate_config")
          .title("Validate project config")
          .description("Parse the active project configuration file using the format implied by its path (TOML for `.toml`, INI otherwise) and report whether it is syntactically valid, including line and column details on parse errors.")
          .annotations(makeToolAnnotations("Validate project config", true, false, true, false))
          .metadata(makeToolMetadata(
            "config",
            "Use after editing the active project config file, whether it resolves to oro.toml, oro.ini, or a custom TOML/INI path.",
            "Validation result with ok=true on success, or ok=false plus parse diagnostics on failure, including the detected config format.",
            {},
            {},
            { "config", "validation", "toml", "ini" }
          ))
          .build(),
        [&](const Object&) -> Object {
          oro::runtime::String error;
          const auto configRel = resolveActiveConfigRelativePath(ctx.options, ctx.workspaceRoot).generic_string();
          auto resolved = resolveWorkspacePath(ctx.workspaceRoot, configRel, true, true, false, false, error);
          if (!resolved.has_value()) return toolResultText(error, true);

          bool truncated = false;
          auto content = readFileBounded(*resolved, kDefaultMaxFileBytes, truncated, error);
          if (!content.has_value()) return toolResultText(error, true);
          if (truncated) {
            return toolResultText("config file too large to validate", true);
          }

          try {
            const auto format = detectConfigFormatForPath(*resolved);
            if (format == runtime::config::UserConfigFormat::Toml) {
              runtime::TOML::parse(*content);
            } else {
              runtime::INI::parseDocument(*content);
            }
            Object payload(Object::Entries {
              {"ok", Boolean(true)},
              {"format", String(configFormatName(format))}
            });
            return toolResultStructured(Any(payload));
          } catch (const runtime::TOML::ParseError& e) {
            Object payload(Object::Entries {
              {"ok", Boolean(false)},
              {"format", String("toml")},
              {"message", String(e.what())},
              {"line", Number(static_cast<int64_t>(e.line()))},
              {"column", Number(static_cast<int64_t>(e.column()))}
            });
            return toolResultStructured(Any(payload));
          } catch (const runtime::INI::ParseError& e) {
            Object payload(Object::Entries {
              {"ok", Boolean(false)},
              {"format", String("ini")},
              {"message", String(e.what())},
              {"line", Number(static_cast<int64_t>(e.line()))},
              {"column", Number(static_cast<int64_t>(e.column()))}
            });
            return toolResultStructured(Any(payload));
          } catch (const std::exception& e) {
            Object payload(Object::Entries {{"ok", Boolean(false)}, {"message", String(e.what())}});
            return toolResultStructured(Any(payload));
          }
        }
      );

      auto simpleCliTool = [&](
        const oro::runtime::String& toolName,
        const oro::runtime::String& title,
        const oro::runtime::String& description,
        const Vector<oro::runtime::String>& baseArgs,
        const Object& annotations,
        const Object& metadata
      ) {
        addTool(
          ctx.tools,
          ToolBuilder(toolName)
            .title(title)
            .description(description)
            .annotations(annotations)
            .metadata(metadata)
            .addArray("args", "Additional arguments passed after the command as argv tokens.", arrayStringItems, false)
            .build(),
          [&, baseArgs](const Object& args) -> Object {
            oro::runtime::String error;
            Vector<oro::runtime::String> extra;
            if (args.contains("args")) {
              extra = getStringArray(args, "args", error);
              if (error.size() > 0) return toolResultText(error, true);
            }

            Vector<oro::runtime::String> callArgs;
            callArgs.reserve(baseArgs.size() + extra.size());
            for (const auto& v : baseArgs) callArgs.push_back(v);
            for (const auto& v : extra) callArgs.push_back(v);
            const auto exec = execCli(ctx.options, callArgs, ctx.workspaceRoot, "", 0, kDefaultMaxToolOutputBytes, true, true);
            return toolResultStructured(Any(resultFromExec(exec)));
          }
        );
      };

      simpleCliTool(
        "init_project",
        "Initialize project",
        "Run `oroc init` to scaffold a new Oro project. Use this for project creation rather than issuing raw CLI commands.",
        {"init"},
        makeToolAnnotations("Initialize project", false, true, false, false),
        makeToolMetadata("project", "Create a new project scaffold inside the workspace or a child directory.", "Structured command execution result from `oroc init`.", { R"({"args":["my-app"]})" }, { "run_cli" }, { "project", "scaffold", "init" })
      );
      simpleCliTool(
        "setup_deps",
        "Setup build dependencies",
        "Run `oroc setup` to install or validate host/target build dependencies such as SDKs and toolchains.",
        {"setup"},
        makeToolAnnotations("Setup build dependencies", false, false, false, true),
        makeToolMetadata("toolchain", "Prepare Android/iOS/desktop build prerequisites or confirm that a machine is ready to build.", "Structured command execution result from `oroc setup`.", { R"({"args":["--platform=android"]})" }, { "run_cli" }, { "setup", "toolchain", "sdk" })
      );
      simpleCliTool(
        "build_app",
        "Build app",
        "Run `oroc build` for the current workspace. Prefer this over `run_cli` when the goal is specifically to build the project.",
        {"build"},
        makeToolAnnotations("Build app", false, false, false, true),
        makeToolMetadata("build", "Compile/package the current project for a target platform.", "Structured command execution result from `oroc build`.", { R"({"args":["--platform=ios","."]})" }, { "run_cli" }, { "build", "artifact", "package" })
      );
      simpleCliTool(
        "run_app",
        "Run app",
        "Run `oroc run` for the current workspace. Useful for local execution, tests, and simulator/emulator runs.",
        {"run"},
        makeToolAnnotations("Run app", false, false, false, true),
        makeToolMetadata("run", "Launch the current app locally, headlessly, or on a connected simulator/device.", "Structured command execution result from `oroc run`.", { R"({"args":["--headless","--test=tests/smoke.js","."]})" }, { "run_cli" }, { "run", "launch", "test" })
      );
      simpleCliTool(
        "install_app",
        "Install app to device",
        "Run `oroc install-app` to deploy the built application to a selected device or simulator.",
        {"install-app"},
        makeToolAnnotations("Install app to device", false, true, false, true),
        makeToolMetadata("device", "Install an already-built artifact onto Android or Apple targets.", "Structured command execution result from `oroc install-app`.", { R"({"args":["--platform=android","--device","emulator-5554"]})" }, { "run_cli" }, { "device", "install", "deploy" })
      );
      simpleCliTool(
        "list_devices",
        "List connected devices",
        "Run `oroc list-devices` and return the discovered Android/iOS device inventory.",
        {"list-devices"},
        makeToolAnnotations("List connected devices", true, false, true, true),
        makeToolMetadata("device", "Inspect available Android or iOS device identifiers before install/run flows.", "Structured command execution result from `oroc list-devices`.", { R"({"args":["--platform=ios","--json"]})" }, { "run_cli" }, { "device", "discovery", "adb", "udid" })
      );
      simpleCliTool(
        "print_build_dir",
        "Print build output directory",
        "Run `oroc print-build-dir` to resolve the build output directory for the current workspace and target platform.",
        {"print-build-dir"},
        makeToolAnnotations("Print build output directory", true, false, true, false),
        makeToolMetadata("build", "Locate artifacts or build outputs without parsing config manually.", "Structured command execution result from `oroc print-build-dir`.", {}, { "run_cli" }, { "build", "path", "artifacts" })
      );
      simpleCliTool(
        "get_env",
        "Inspect effective environment",
        "Run `oroc env` to inspect the environment variables and derived toolchain values visible to Oro.",
        {"env"},
        makeToolAnnotations("Inspect effective environment", true, false, true, false),
        makeToolMetadata("environment", "Understand what build/runtime variables the CLI currently sees before debugging setup or signing issues.", "Structured command execution result from `oroc env`.", { R"({"args":["--json"]})" }, { "run_cli" }, { "env", "debugging", "toolchain" })
      );
      simpleCliTool(
        "project_version",
        "Inspect or bump project version",
        "Run `oroc version` to inspect or change the version defined in the active project config.",
        {"version"},
        makeToolAnnotations("Inspect or bump project version", false, true, false, false),
        makeToolMetadata("release", "Read the current version or apply a semantic version bump during release prep.", "Structured command execution result from `oroc version`.", { R"({"args":["minor"]})" }, { "run_cli" }, { "version", "release", "semver" })
      );
      simpleCliTool(
        "get_versions",
        "List Oro and dependency versions",
        "Run `oroc versions -f json` to get CLI/runtime/dependency versions in a stable machine-readable format.",
        {"versions", "-f", "json"},
        makeToolAnnotations("List Oro and dependency versions", true, false, true, false),
        makeToolMetadata("environment", "Inspect runtime and dependency versions for diagnostics, release notes, or support triage.", "Structured command execution result from `oroc versions -f json`.", {}, { "run_cli" }, { "versions", "dependencies", "diagnostics" })
      );

      // Config helpers backed by the `config` subcommand.
      addTool(
        ctx.tools,
        ToolBuilder("config_get")
          .title("Get config value")
          .description("Run `oroc config --key=<name>` and return the effective value of a configuration key.")
          .annotations(makeToolAnnotations("Get config value", true, false, true, false))
          .metadata(makeToolMetadata(
            "config",
            "Use for exact value lookup when you already know the key path.",
            "Structured command execution result from `oroc config --key`.",
            { R"({"name":"meta.version"})" },
            { "run_cli" },
            { "config", "read", "lookup" }
          ))
          .addString("name", "Configuration key name, for example `meta.version` or `webview.root`.", true)
          .addBoolean("strict", "Enable strict mode so unset keys return a non-zero exit code.", false)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto key = requireString(args, "name", error);
          if (!key.has_value()) return toolResultText(error, true);
          const bool strict = getOptionalBool(args, "strict", false);
          Vector<oro::runtime::String> callArgs = {"config", "--key", *key};
          if (strict) callArgs.push_back("--strict");
          const auto exec = execCli(ctx.options, callArgs, ctx.workspaceRoot, "", 0, kDefaultMaxToolOutputBytes, true, true);
          return toolResultStructured(Any(resultFromExec(exec)));
        }
      );

      addTool(
        ctx.tools,
        ToolBuilder("config_describe")
          .title("Describe config key")
          .description("Run `oroc config --describe=<name>` and return documentation, defaults, and type information for a configuration key.")
          .annotations(makeToolAnnotations("Describe config key", true, false, true, false))
          .metadata(makeToolMetadata(
            "config",
            "Use before editing config keys when you need documentation or expected types.",
            "Structured command execution result from `oroc config --describe`.",
            { R"({"name":"ios.provisioning_profile"})" },
            { "run_cli" },
            { "config", "docs", "describe" }
          ))
          .addString("name", "Configuration key name to describe.", true)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto key = requireString(args, "name", error);
          if (!key.has_value()) return toolResultText(error, true);
          Vector<oro::runtime::String> callArgs = {"config", "--describe", *key};
          const auto exec = execCli(ctx.options, callArgs, ctx.workspaceRoot, "", 0, kDefaultMaxToolOutputBytes, true, true);
          return toolResultStructured(Any(resultFromExec(exec)));
        }
      );

      simpleCliTool(
        "config_list",
        "List known config keys",
        "Run `oroc config --list` to enumerate known config keys, current values, defaults, and undocumented keys found in the active config.",
        {"config", "--list"},
        makeToolAnnotations("List known config keys", true, false, true, false),
        makeToolMetadata("config", "Use for broad config discovery when you do not yet know the exact key names.", "Structured command execution result from `oroc config --list`.", {}, { "run_cli" }, { "config", "discovery", "list" })
      );
      addTool(
        ctx.tools,
        ToolBuilder("config_format")
          .title("Dump merged config")
          .description("Run `oroc config --format=<format>` to print the merged effective configuration as TOML, INI, or JSON.")
          .annotations(makeToolAnnotations("Dump merged config", true, false, true, false))
          .metadata(makeToolMetadata(
            "config",
            "Use when a client wants the whole effective config instead of single-key queries.",
            "Structured command execution result from `oroc config --format`.",
            { R"({"format":"json"})" },
            { "run_cli" },
            { "config", "dump", "format" }
          ))
          .addString("format", "One of `toml`, `ini`, or `json`.", true)
          .build(),
        [&](const Object& args) -> Object {
          oro::runtime::String error;
          auto fmt = requireString(args, "format", error);
          if (!fmt.has_value()) return toolResultText(error, true);
          Vector<oro::runtime::String> callArgs = {"config", "--format", *fmt};
          const auto exec = execCli(ctx.options, callArgs, ctx.workspaceRoot, "", 0, kDefaultMaxToolOutputBytes, true, true);
          return toolResultStructured(Any(resultFromExec(exec)));
        }
      );

      // Update helpers. Use `args` to pass flags; `update_server` can be long-running, so encourage timeout via run_cli.
      simpleCliTool("update_keygen", "Generate update keys", "Run `oroc update keygen` for Ed25519 key generation.", {"update", "keygen"}, makeToolAnnotations("Generate update keys", false, false, false, false), makeToolMetadata("update", "Create signing key material for application updates.", "Structured command execution result from `oroc update keygen`.", {}, { "run_cli" }, { "update", "keys", "signing" }));
      simpleCliTool("update_init", "Scaffold update manifest", "Run `oroc update init` to scaffold a new update manifest JSON file.", {"update", "init"}, makeToolAnnotations("Scaffold update manifest", false, true, false, false), makeToolMetadata("update", "Initialize a manifest before filling in update targets and artifacts.", "Structured command execution result from `oroc update init`.", {}, { "run_cli" }, { "update", "manifest", "scaffold" }));
      simpleCliTool("update_sign", "Sign update manifest", "Run `oroc update sign` to produce a detached manifest signature.", {"update", "sign"}, makeToolAnnotations("Sign update manifest", false, true, false, false), makeToolMetadata("update", "Sign an update manifest after validating and preparing artifact entries.", "Structured command execution result from `oroc update sign`.", {}, { "run_cli" }, { "update", "sign", "manifest" }));
      simpleCliTool("update_verify", "Verify update manifest", "Run `oroc update verify` to verify a manifest signature against a public key.", {"update", "verify"}, makeToolAnnotations("Verify update manifest", true, false, true, false), makeToolMetadata("update", "Confirm that a manifest and signature pair are valid before release or deployment.", "Structured command execution result from `oroc update verify`.", {}, { "run_cli" }, { "update", "verify", "manifest" }));
      simpleCliTool("update_bundle", "Bundle update artifact", "Run `oroc update bundle` to create an update artifact tar file.", {"update", "bundle"}, makeToolAnnotations("Bundle update artifact", false, false, false, false), makeToolMetadata("update", "Package a directory or build output into a tar artifact suitable for update distribution.", "Structured command execution result from `oroc update bundle`.", {}, { "run_cli" }, { "update", "bundle", "artifact" }));
      simpleCliTool("update_extract", "Extract update bundle", "Run `oroc update extract` to unpack an update artifact tar file.", {"update", "extract"}, makeToolAnnotations("Extract update bundle", false, true, false, false), makeToolMetadata("update", "Inspect or unpack an update bundle during validation or debugging.", "Structured command execution result from `oroc update extract`.", {}, { "run_cli" }, { "update", "extract", "artifact" }));
      simpleCliTool("update_validate", "Validate update manifest", "Run `oroc update validate` to check manifest structure and schema compliance.", {"update", "validate"}, makeToolAnnotations("Validate update manifest", true, false, true, false), makeToolMetadata("update", "Perform structural checks on an update manifest before signing or serving it.", "Structured command execution result from `oroc update validate`.", {}, { "run_cli" }, { "update", "validate", "manifest" }));
      simpleCliTool("update_info", "Inspect update source", "Run `oroc update info` to inspect a static manifest or query an update server.", {"update", "info"}, makeToolAnnotations("Inspect update source", true, false, true, true), makeToolMetadata("update", "Read update metadata from manifests or servers during release validation and support triage.", "Structured command execution result from `oroc update info`.", {}, { "run_cli" }, { "update", "info", "server", "manifest" }));

      addTool(
        ctx.tools,
        ToolBuilder("update_server")
          .title("Run update server briefly")
          .description("Run `oroc update server` for a bounded duration. This is primarily for smoke tests and short-lived validation flows; use `run_cli` for more advanced long-running process control.")
          .annotations(makeToolAnnotations("Run update server briefly", false, false, false, true))
          .metadata(makeToolMetadata(
            "update",
            "Start a temporary update server during tests or validation. Requires timeout_ms so the MCP call always terminates.",
            "Structured command execution result from the bounded `oroc update server` run.",
            { R"({"timeout_ms":10000,"args":["--root","./updates"]})" },
            { "run_cli" },
            { "update", "server", "smoke-test" }
          ))
          .addIntegerRange("timeout_ms", "How long to run before terminating, in milliseconds, up to 24 hours. Required.", 1, kMaxToolTimeoutMilliseconds, true)
          .addArray("args", "Additional arguments passed after `update server`.", arrayStringItems, false)
          .build(),
        [&](const Object& args) -> Object {
          if (!args.contains("timeout_ms") || !args.get("timeout_ms").isNumber()) {
            return toolResultText("missing required integer field 'timeout_ms'", true);
          }
          const auto timeoutValue = args.get("timeout_ms").as<Number>().value();
          if (!std::isfinite(timeoutValue) ||
              std::floor(timeoutValue) != timeoutValue ||
              timeoutValue <= 0 ||
              timeoutValue > kMaxToolTimeoutMilliseconds) {
            return toolResultText("'timeout_ms' must be an integer from 1 through 86400000", true);
          }

          oro::runtime::String error;
          Vector<oro::runtime::String> extra;
          if (args.contains("args")) {
            extra = getStringArray(args, "args", error);
            if (error.size() > 0) return toolResultText(error, true);
          }

          Vector<oro::runtime::String> callArgs = {"update", "server"};
          for (const auto& v : extra) callArgs.push_back(v);
          const auto exec = execCli(
            ctx.options,
            callArgs,
            ctx.workspaceRoot,
            "",
            static_cast<int>(timeoutValue),
            kDefaultMaxToolOutputBytes,
            true,
            true
          );
          return toolResultStructured(Any(resultFromExec(exec)));
        }
      );
    }

    static Array mcpSupportedVersions() {
      Array::Entries versions;
      for (const auto& version : runtime::mcp::supportedProtocolVersions()) {
        versions.push_back(String(version));
      }
      return Array(versions);
    }

    static std::optional<Response> handleRequest(
      Context& ctx,
      const Request& req,
      const oro::runtime::String& negotiatedMcp2025ProtocolVersion
    ) {
      if (req.method == "initialize") {
        Object::Entries serverInfoEntries {
          {"name", String(ctx.options.cliDisplayName)},
          {"title", String("Oro Runtime CLI MCP Server")},
          {"version", String(runtime::VERSION_FULL_STRING)}
        };
        const auto instructions = String(
          "This MCP server is optimized for Oro Runtime development. "
          "Start with workspace_info, search_docs, list_workspace, and resources/list to understand the workspace. "
          "Use read_config when workspace_info reports config_exists=true or resources/list advertises the active config resource. "
          "Prefer specialized tools such as search_docs, config_get, config_describe, build_app, run_app, and get_versions over run_cli when they fit your goal, because they publish clearer intent and structured results. "
          "Successful tool calls return structuredContent alongside a text copy of the same JSON for compatibility. "
          "Key documentation resources are advertised through resources/list from the current workspace (`workspace:/...`) and, when installed, from the runtime distribution (`runtime-doc:/...`) for README, source-build environment, MCP, API, CLI, config, and man1/man3/man7 references. "
          "For runtime source bootstrap, NO_ANDROID and NO_IOS are independent presence flags that exclude only Android or iOS/iOS Simulator work respectively; neither selects an application build target. "
          "Prefer runtime-doc resources for runtime behavior and API contracts, and workspace resources for project-specific config or local docs. "
          "Use run_cli only as a fallback for unsupported commands or advanced flag combinations."
        );
        Object::Entries resultEntries {
          {"protocolVersion", String(negotiatedMcp2025ProtocolVersion)},
          {"capabilities", capabilities(true)},
          {"serverInfo", Object(serverInfoEntries)},
          {"instructions", String(instructions)}
        };
        return Response::success(req.id, Object(resultEntries));
      }

      if (req.method == "server/discover") {
        Object serverInfo(Object::Entries {
          {"name", String(ctx.options.cliDisplayName)},
          {"title", String("Oro Runtime CLI MCP Server")},
          {"version", String(runtime::VERSION_FULL_STRING)}
        });
        Object meta(Object::Entries {
          {"io.modelcontextprotocol/serverInfo", serverInfo}
        });
        return Response::success(req.id, Object(Object::Entries {
          {"supportedVersions", mcpSupportedVersions()},
          {"capabilities", capabilities()},
          {"_meta", meta},
          {"instructions", String("Use specialized Oro tools before run_cli and keep file access scoped to the configured workspace.")},
          {"ttlMs", Number(60000)},
          {"cacheScope", String("private")}
        }));
      }

      if (req.method == "notifications/initialized") {
        return std::nullopt;
      }

      if (req.method == "ping") {
        return Response::success(req.id, Object(Object::Entries {}));
      }

      if (req.method == "tools/list") {
        return Response::success(req.id, handleToolsList(ctx.tools));
      }

      if (req.method == "tools/call") {
        if (!req.params.isObject()) {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "tools/call params must be an object"));
        }
        oro::runtime::String error;
        auto result = handleToolCall(ctx, req.params.as<Object>(), error);
        return Response::success(req.id, result);
      }

      if (req.method == "resources/list") {
        auto latest = buildResources(ctx.options, ctx.workspaceRoot);
        {
          std::lock_guard<std::mutex> lock(ctx.resourcesMutex);
          ctx.resources = latest;
        }
        return Response::success(req.id, handleResourcesList(latest));
      }

      if (req.method == "resources/read") {
        if (!req.params.isObject()) {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "resources/read params must be an object"));
        }
        auto& params = req.params.as<Object>();
        if (!params.contains("uri") || !params.get("uri").isString()) {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "missing uri"));
        }
        const auto uri = params.get("uri").as<String>().value();

        runtime::mcp::ResourceDescriptor desc;
        bool found = false;
        {
          std::lock_guard<std::mutex> lock(ctx.resourcesMutex);
          for (const auto& d : ctx.resources.descriptors) {
            if (d.uri == uri) {
              desc = d;
              found = true;
              break;
            }
          }
        }
        if (!found && startsWith(uri, "workspace:/")) {
          desc.uri = uri;
          desc.name = "workspace";
          desc.description = "Workspace resource.";
          desc.mimeType = "text/plain";
          found = true;
        }
        if (!found) {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "unknown resource uri"));
        }

        oro::runtime::String error;
        std::optional<fs::path> resolved;
        if (startsWith(uri, "workspace:/")) {
          const auto pathPart = uri.substr(std::strlen("workspace:/"));
          resolved = resolveWorkspacePath(ctx.workspaceRoot, pathPart, true, false, false, false, error);
          if (!resolved.has_value()) {
            return Response::failure(req.id, Error(ErrorCode::InvalidParams, error));
          }
        } else if (startsWith(uri, "runtime-doc:/")) {
          resolved = resourcePathFromDescriptor(desc);
          if (!resolved.has_value()) {
            return Response::failure(req.id, Error(ErrorCode::InvalidParams, "runtime documentation resource is unavailable"));
          }
        } else {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "unsupported resource uri"));
        }

        std::error_code ec;
        const bool isDir = fs::is_directory(*resolved, ec) && !ec;
        desc.mimeType = guessMimeType(*resolved, isDir);

        if (isDir) {
          const auto listing = listDirectory(*resolved, 200, false, 1);
          auto content = runtime::mcp::ResourceContent::textContent(runtime::JSON::stringify(Any(listing)));
          Array contents(Array::Entries { Object(content.toJSON(desc)) });
          return Response::success(req.id, Object(Object::Entries {{"contents", contents}}));
        }

        if (!fs::is_regular_file(*resolved, ec) || ec) {
          return Response::failure(req.id, Error(ErrorCode::InvalidParams, "resource is not a file"));
        }

        const bool binary = isDefinitelyBinaryExtension(*resolved);
        bool truncated = false;

        if (binary) {
          auto bytes = readFileBytesBounded(*resolved, kDefaultMaxFileBytes, truncated, error);
          if (!bytes.has_value()) {
            return Response::failure(req.id, Error(ErrorCode::InternalError, error));
          }
          if (truncated) {
            addMetadataField(desc, "truncated", Boolean(true));
          }
          auto content = runtime::mcp::ResourceContent::binaryContent(*bytes);
          Array contents(Array::Entries { Object(content.toJSON(desc)) });
          return Response::success(req.id, Object(Object::Entries {{"contents", contents}}));
        }

        auto contentText = readFileBounded(*resolved, kDefaultMaxFileBytes, truncated, error);
        if (!contentText.has_value()) {
          return Response::failure(req.id, Error(ErrorCode::InternalError, error));
        }
        if (truncated) {
          addMetadataField(desc, "truncated", Boolean(true));
        }

        auto content = runtime::mcp::ResourceContent::textContent(*contentText);
        Array contents(Array::Entries { Object(content.toJSON(desc)) });
        return Response::success(req.id, Object(Object::Entries {{"contents", contents}}));
      }

      if (req.method == "resources/templates/list") {
        return Response::success(req.id, Object(Object::Entries {{"resourceTemplates", Array(Array::Entries {})}}));
      }

      if (req.method == "prompts/list") {
        return Response::success(req.id, Object(Object::Entries {{"prompts", Array(Array::Entries {})}}));
      }

      if (req.method == "logging/setLevel") {
        return Response::success(req.id, Object(Object::Entries {}));
      }

      return Response::failure(req.id, Error(ErrorCode::MethodNotFound, "method not found: " + req.method));
    }

    static oro::runtime::String getRequestProtocolVersion(const Request& request) {
      if (!request.params.isObject()) {
        return "";
      }

      const auto& params = request.params.as<Object>();
      if (!params.contains("_meta") || !params.get("_meta").isObject()) {
        return "";
      }

      const auto& metadata = params.get("_meta").as<Object>();
      if (!metadata.contains("io.modelcontextprotocol/protocolVersion") ||
          !metadata.get("io.modelcontextprotocol/protocolVersion").isString()) {
        return "";
      }

      return metadata.get("io.modelcontextprotocol/protocolVersion").as<String>().value();
    }

    static bool hasCurrentClientCapabilities(const Request& request) {
      if (!request.params.isObject()) {
        return false;
      }

      const auto& params = request.params.as<Object>();
      if (!params.contains("_meta") || !params.get("_meta").isObject()) {
        return false;
      }

      const auto& metadata = params.get("_meta").as<Object>();
      return metadata.contains("io.modelcontextprotocol/clientCapabilities") &&
        metadata.get("io.modelcontextprotocol/clientCapabilities").isObject();
    }

    static void prepareCurrentResponse(Context& ctx, const Request& request, Response& response) {
      if (response.isError() || !response.result.isObject()) {
        return;
      }

      auto& result = response.result.as<Object>();
      result.set("resultType", String("complete"));

      if (request.method == "tools/list" ||
          request.method == "resources/list" ||
          request.method == "resources/read" ||
          request.method == "prompts/list") {
        result.set("ttlMs", Number(5000));
        result.set("cacheScope", String("private"));
      }

      Object metadata;
      if (result.contains("_meta") && result.get("_meta").isObject()) {
        metadata = result.get("_meta").as<Object>();
      }
      metadata.set("io.modelcontextprotocol/serverInfo", Object(Object::Entries {
        {"name", String(ctx.options.cliDisplayName)},
        {"version", String(runtime::VERSION_FULL_STRING)}
      }));
      result.set("_meta", metadata);
    }

    static std::optional<oro::runtime::String> handleJsonRpcPayload(
      Context& ctx,
      const oro::runtime::String& payload,
      bool& mcp2025Initialized,
      oro::runtime::String& mcp2025ProtocolVersion,
      bool& modernEra
    ) {
      try {
        auto any = runtime::JSON::parse(payload);
        Error error;
        auto request = parseJsonRpcRequest(any, error);
        if (!request.has_value()) {
          auto response = Response::failure(Null(), error);
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        if (!request->params.isNull() && !request->params.isObject()) {
          auto response = Response::failure(
            request->id,
            Error(ErrorCode::InvalidParams, "MCP request params must be an object")
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        const auto protocolVersion = getRequestProtocolVersion(*request);
        const bool currentRequest = protocolVersion == runtime::mcp::kProtocolVersion ||
          (modernEra && protocolVersion.empty() && request->isNotification());

        if (!protocolVersion.empty() && protocolVersion != runtime::mcp::kProtocolVersion) {
          Object data(Object::Entries {
            {"supported", mcpSupportedVersions()},
            {"requested", String(protocolVersion)}
          });
          auto response = Response::failure(
            request->id,
            Error(ErrorCode::UnsupportedProtocolVersion, "unsupported MCP protocol version", data)
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        if ((mcp2025Initialized && currentRequest) ||
            (modernEra && !currentRequest)) {
          Object data(Object::Entries {
            {"supported", mcpSupportedVersions()},
            {"requested", String(
              currentRequest
                ? runtime::mcp::kProtocolVersion
                : mcp2025ProtocolVersion
            )}
          });
          auto response = Response::failure(
            request->id,
            Error(
              ErrorCode::UnsupportedProtocolVersion,
              "MCP connection cannot change protocol eras",
              data
            )
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        if (currentRequest) {
          modernEra = true;
        }

        if (currentRequest && !request->isNotification() && !hasCurrentClientCapabilities(*request)) {
          Object data(Object::Entries {
            {"requiredCapabilities", Object(Object::Entries {})}
          });
          auto response = Response::failure(
            request->id,
            Error(
              ErrorCode::MissingRequiredClientCapability,
              "request metadata must include clientCapabilities",
              data
            )
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        if (request->method == "server/discover") {
          if (!currentRequest) {
            Object data(Object::Entries {
              {"supported", mcpSupportedVersions()},
              {"requested", String(protocolVersion)}
            });
            auto response = Response::failure(
              request->id,
              Error(
                ErrorCode::UnsupportedProtocolVersion,
                "server/discover requires MCP 2026-07-28 request metadata",
                data
              )
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          modernEra = true;
        } else if (currentRequest && request->method == "initialize") {
          auto response = Response::failure(
            request->id,
            Error(ErrorCode::MethodNotFound, "initialize is not available in the modern MCP protocol")
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        } else if (request->method == "initialize") {
          if (request->isNotification()) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidRequest, "initialize requires a request id")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          if (mcp2025Initialized) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidRequest, "MCP connection is already initialized")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          if (!request->params.isObject()) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidParams, "initialize params must be an object")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }

          const auto& params = request->params.as<Object>();
          if (!params.contains("protocolVersion") ||
              !params.get("protocolVersion").isString() ||
              params.get("protocolVersion").as<String>().value().empty() ||
              !params.contains("capabilities") ||
              !params.get("capabilities").isObject() ||
              !params.contains("clientInfo") ||
              !params.get("clientInfo").isObject()) {
            auto response = Response::failure(
              request->id,
              Error(
                ErrorCode::InvalidParams,
                "initialize requires protocolVersion, capabilities, and clientInfo"
              )
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }

          const auto& clientInfo = params.get("clientInfo").as<Object>();
          if (!clientInfo.contains("name") ||
              !clientInfo.get("name").isString() ||
              clientInfo.get("name").as<String>().value().empty() ||
              !clientInfo.contains("version") ||
              !clientInfo.get("version").isString() ||
              clientInfo.get("version").as<String>().value().empty()) {
            auto response = Response::failure(
              request->id,
              Error(
                ErrorCode::InvalidParams,
                "initialize clientInfo requires name and version"
              )
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }

          const auto requestedVersion = params.get("protocolVersion").as<String>().value();
          mcp2025ProtocolVersion = runtime::mcp::isMcp2025ProtocolVersion(requestedVersion)
            ? requestedVersion
            : oro::runtime::String(runtime::mcp::kMcp2025ProtocolVersion);
        } else if (!currentRequest && request->method != "initialize" && !mcp2025Initialized) {
          auto response = Response::failure(
            request->id,
            Error(ErrorCode::InvalidRequest, "MCP session is not initialized")
          );
          return runtime::JSON::stringify(Any(response.toJSON()));
        }

        if (currentRequest && request->method == "subscriptions/listen") {
          if (request->isNotification()) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidRequest, "subscriptions/listen requires a request id")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          if (!request->params.isObject()) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidParams, "subscriptions/listen params must be an object")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          const auto& params = request->params.as<Object>();
          if (!params.contains("notifications") || !params.get("notifications").isObject()) {
            auto response = Response::failure(
              request->id,
              Error(ErrorCode::InvalidParams, "subscriptions/listen requires a notifications filter")
            );
            return runtime::JSON::stringify(Any(response.toJSON()));
          }
          const auto& notifications = params.get("notifications").as<Object>();
          for (const auto* filter : {"toolsListChanged", "promptsListChanged", "resourcesListChanged"}) {
            if (notifications.contains(filter) && !notifications.get(filter).isBoolean()) {
              auto response = Response::failure(
                request->id,
                Error(
                  ErrorCode::InvalidParams,
                  oro::runtime::String(filter) + " must be a boolean"
                )
              );
              return runtime::JSON::stringify(Any(response.toJSON()));
            }
          }
          if (notifications.contains("resourceSubscriptions")) {
            if (!notifications.get("resourceSubscriptions").isArray()) {
              auto response = Response::failure(
                request->id,
                Error(ErrorCode::InvalidParams, "resourceSubscriptions must be an array of resource URIs")
              );
              return runtime::JSON::stringify(Any(response.toJSON()));
            }
            for (const auto& uri : notifications.get("resourceSubscriptions").as<Array>().value()) {
              if (!uri.isString()) {
                auto response = Response::failure(
                  request->id,
                  Error(ErrorCode::InvalidParams, "resourceSubscriptions entries must be strings")
                );
                return runtime::JSON::stringify(Any(response.toJSON()));
              }
            }
          }

          Object acknowledgement(Object::Entries {
            {"jsonrpc", String(runtime::mcp::kJsonRpcVersion)},
            {"method", String("notifications/subscriptions/acknowledged")},
            {"params", Object(Object::Entries {
              {"notifications", Object(Object::Entries {})},
              {"_meta", Object(Object::Entries {
                {"io.modelcontextprotocol/subscriptionId", request->id}
              })}
            })}
          });
          return runtime::JSON::stringify(Any(acknowledgement));
        }

        if (request->isNotification()) {
          handleRequest(ctx, *request, mcp2025ProtocolVersion);
          return std::nullopt;
        }

        auto response = handleRequest(ctx, *request, mcp2025ProtocolVersion);
        if (!response.has_value()) {
          return std::nullopt;
        }
        if (request->method == "initialize" && !response->isError()) {
          mcp2025Initialized = true;
        }
        if (currentRequest) {
          prepareCurrentResponse(ctx, *request, *response);
        }
        return runtime::JSON::stringify(Any(response->toJSON()));
      } catch (const runtime::JSON::Error& e) {
        auto response = Response::failure(Null(), Error(ErrorCode::ParseError, e.what()));
        return runtime::JSON::stringify(Any(response.toJSON()));
      } catch (const std::exception& e) {
        auto response = Response::failure(Null(), Error(ErrorCode::InternalError, e.what()));
        return runtime::JSON::stringify(Any(response.toJSON()));
      }
    }

    struct StdioMessage {
      oro::runtime::String payload;
      bool usesContentLength = false;
    };

    static std::optional<StdioMessage> readStdioMessage(std::istream& in) {
      oro::runtime::String line;

      auto parseHeaderLine = [](const oro::runtime::String& header, std::unordered_map<oro::runtime::String, oro::runtime::String>& out) {
        const auto pos = header.find(':');
        if (pos == oro::runtime::String::npos) return;
        auto key = lowerCopy(trimCopy(header.substr(0, pos)));
        auto value = trimCopy(header.substr(pos + 1));
        if (key.empty()) return;
        out.insert_or_assign(std::move(key), std::move(value));
      };

      while (std::getline(in, line)) {
        line = trimCopy(std::move(line));
        if (line.empty()) continue;

        const char first = line.empty() ? '\0' : line.front();
        if (first == '{' || first == '[') {
          return StdioMessage {line, false};
        }

        std::unordered_map<oro::runtime::String, oro::runtime::String> headers;
        parseHeaderLine(line, headers);

        while (std::getline(in, line)) {
          line = trimCopy(std::move(line));
          if (line.empty()) break;
          parseHeaderLine(line, headers);
        }

        const auto it = headers.find("content-length");
        if (it == headers.end()) {
          continue;
        }

        long long lengthValue = 0;
        try {
          lengthValue = std::stoll(it->second);
        } catch (...) {
          continue;
        }

        if (lengthValue <= 0) {
          continue;
        }

        if (static_cast<size_t>(lengthValue) > kMaxStdioMessageBytes) {
          size_t remaining = static_cast<size_t>(lengthValue);
          char buffer[4096];
          while (remaining > 0 && in.good()) {
            const size_t chunk = std::min(remaining, sizeof(buffer));
            in.read(buffer, static_cast<std::streamsize>(chunk));
            const auto got = static_cast<size_t>(in.gcount());
            if (got == 0) break;
            remaining -= got;
          }
          if (remaining > 0) {
            return std::nullopt;
          }
          continue;
        }

        StdioMessage msg;
        msg.usesContentLength = true;
        msg.payload.resize(static_cast<size_t>(lengthValue));
        in.read(msg.payload.data(), static_cast<std::streamsize>(lengthValue));
        if (static_cast<size_t>(in.gcount()) != msg.payload.size()) {
          return std::nullopt;
        }
        return msg;
      }

      return std::nullopt;
    }

    static void writeStdioMessage(const oro::runtime::String& payload, bool useContentLength) {
      if (useContentLength) {
        std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        std::cout.flush();
        return;
      }
      std::cout << payload << std::endl;
    }

    class HttpDelegate final : public runtime::mcp::HTTPServer::Delegate {
      public:
        HttpDelegate(runtime::mcp::HTTPServer* server, Context* ctx)
          : server(server),
            ctx(ctx)
        {}

        void onSessionStarted(const oro::runtime::String& sessionId) override {
          std::lock_guard<std::mutex> lock(this->stateMutex);
          this->sessions.insert_or_assign(sessionId, SessionState {});
        }

        void onSessionStopped(const oro::runtime::String& sessionId) override {
          std::lock_guard<std::mutex> lock(this->stateMutex);
          this->sessions.erase(sessionId);
        }

        bool getExpectedRequestHeaders(
          const oro::runtime::String& payload,
          Vector<runtime::mcp::ToolHeader>& headers,
          oro::runtime::String& error
        ) override {
          if (this->ctx == nullptr) {
            return true;
          }
          try {
            const auto request = nlohmann::json::parse(payload);
            if (request.value("method", "") != "tools/call") {
              return true;
            }
            const auto params = request.value("params", nlohmann::json::object());
            const auto name = params.value("name", "");
            for (const auto& tool : this->ctx->tools.tools) {
              if (tool.name == name) {
                const auto arguments = params.contains("arguments")
                  ? params["arguments"].dump()
                  : oro::runtime::String("{}");
                return tool.getExpectedHTTPHeaders(arguments, headers, error);
              }
            }
            return true;
          } catch (const std::exception& exception) {
            error = oro::runtime::String("Unable to validate MCP parameter headers: ") + exception.what();
            return false;
          }
        }

        std::optional<oro::runtime::String> onJsonRpcRequest(const oro::runtime::String& sessionId, const oro::runtime::String& payload) override {
          if (this->ctx == nullptr) {
            return std::nullopt;
          }
          SessionState state;
          {
            std::lock_guard<std::mutex> lock(this->stateMutex);
            const auto it = this->sessions.find(sessionId);
            if (it != this->sessions.end()) {
              state = it->second;
            }
          }

          auto response = handleJsonRpcPayload(
            *this->ctx,
            payload,
            state.mcp2025Initialized,
            state.mcp2025ProtocolVersion,
            state.modernEra
          );

          {
            std::lock_guard<std::mutex> lock(this->stateMutex);
            this->sessions.insert_or_assign(sessionId, state);
          }
          return response;
        }

        void onPing(const oro::runtime::String&) override {}

      private:
        struct SessionState {
          bool mcp2025Initialized = false;
          oro::runtime::String mcp2025ProtocolVersion;
          bool modernEra = false;
        };

        runtime::mcp::HTTPServer* server = nullptr;
        Context* ctx = nullptr;
        std::mutex stateMutex;
        std::unordered_map<oro::runtime::String, SessionState> sessions;
    };
  }

  int run(const Options& options) {
    Context ctx;
    ctx.options = options;

    std::error_code ec;
    ctx.workspaceRoot = fs::weakly_canonical(options.workspaceRoot, ec);
    if (ec) {
      std::cerr << "mcp: failed to resolve workspace: " << ec.message() << std::endl;
      return 1;
    }

    ctx.resources = buildResources(options, ctx.workspaceRoot);
    registerDefaultTools(ctx);

    if (!options.useHttp) {
      bool useContentLength = false;
      bool mcp2025Initialized = false;
      oro::runtime::String mcp2025ProtocolVersion;
      bool modernEra = false;
      while (true) {
        auto msg = readStdioMessage(std::cin);
        if (!msg.has_value()) {
          break;
        }

        useContentLength = useContentLength || msg->usesContentLength;
        auto response = handleJsonRpcPayload(
          ctx,
          msg->payload,
          mcp2025Initialized,
          mcp2025ProtocolVersion,
          modernEra
        );
        if (response.has_value()) {
          writeStdioMessage(*response, useContentLength);
        }
      }
      return 0;
    }

    runtime::mcp::HTTPServer server;
    runtime::mcp::HTTPServer::Config cfg;
    cfg.host = options.host;
    cfg.port = options.port;
    cfg.endpoint = options.endpoint;
    cfg.token = options.token;
    cfg.replaceSseStreamOnReconnect = options.replaceSseStreamOnReconnect;

    HttpDelegate delegate(&server, &ctx);
    if (!server.start(cfg, &delegate)) {
      std::cerr << "mcp: failed to start HTTP server on " << cfg.host << ":" << cfg.port
                << " (is the port already in use?)" << std::endl;
      return 1;
    }

    auto bound = server.getConfig();
    Object payload(Object::Entries {
      {"transport", String("http")},
      {"host", String(bound.host)},
      {"port", Number(bound.port)},
      {"endpoint", String(bound.endpoint)},
      {"token", String(bound.token)}
    });
    std::cout << runtime::JSON::stringify(Any(payload)) << std::endl;

    while (server.isRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
  }
}
