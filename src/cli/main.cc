#if !defined(_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#include <array>
#include <AppxPackaging.h>
#include <comdef.h>
#include <functional>
#include <shlwapi.h>
#include <strsafe.h>
#include <tchar.h>
#include <wrl.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Urlmon.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "msvcrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "libuv.lib")
#pragma comment(lib, "llama.lib")
#pragma comment(lib, "whisper.lib")
#if ORO_RUNTIME_HAVE_LIBIPFS
#ifdef _DEBUG
#pragma comment(lib, "libipfsd.lib")
#else
#pragma comment(lib, "libipfs.lib")
#endif
#endif
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cctype>
#include <iomanip>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <optional>
#include <system_error>
#include <sstream>
#include <future>
#include <regex>
#include <span>
#include <unordered_set>
#include <limits>
#include <map>

namespace fs = std::filesystem;

#if __has_include(<sodium.h>)
#include <sodium.h>
#define ORO_CLI_HAS_SODIUM 1
#else
#define ORO_CLI_HAS_SODIUM 0
#endif

#if __has_include(<cpp-httplib/httplib.h>)
#include <cpp-httplib/httplib.h>
#define ORO_CLI_HAS_CPPHTTPLIB 1
#else
#define ORO_CLI_HAS_CPPHTTPLIB 0
#endif

#ifndef CMD_RUNNER
#define CMD_RUNNER
#endif

#ifndef ORO_CLI
#define ORO_CLI 1
#endif

#include "../extension/extension.hh"
// #include "../extension/.hh"
#include "../runtime.hh"
#include "../runtime/semver.hh"
#include "../runtime/toml.hh"
#include "../runtime/config.hh"
#include "../runtime/tar.hh"
#include "../runtime/url.hh"
#include "../runtime/deps.hh"
#include "../cli.hh"

#include "templates.hh"
#include "mcp.hh"

#ifndef ORO_RUNTIME_BUILD_TIME
#define ORO_RUNTIME_BUILD_TIME 0
#endif

// Avoid collision with the global DEBUG macro from runtime config.hh
#ifdef DEBUG
#undef DEBUG
#endif

using namespace oro;
using namespace oro::cli;
using namespace oro::runtime;
using namespace oro::runtime::crypto;
using namespace oro::runtime::process;
using namespace oro::runtime::string;
using namespace std::chrono;

extern int LLAMA_BUILD_NUMBER;

using UserConfigFormat = oro::runtime::config::UserConfigFormat;
using ConfigKeyInfo = oro::runtime::config::ConfigKeyInfo;
using ConfigValueType = oro::runtime::config::ConfigValueType;

const auto DEFAULT_ORO_RC_FILENAME = String(".ororc");
const auto DEFAULT_ORO_ENV_FILENAME = String(".oro.env");
const auto DEFAULT_UPDATE_MANIFEST_FILENAME = String("manifest.json");

Process* buildAfterScriptProcess = nullptr;
filesystem::Watcher* sourcesWatcher = nullptr;
Thread* sourcesWatcherSupportThread = nullptr;

Path targetPath;
Path targetArgumentPath;
Path targetSourcePath;
String settingsSource = "";
Map<> settings;
Map<> rc;

bool flagDebugMode = true;      // Build mode (dev vs prod), not logger
bool flagVerboseMode = false;   // Verbose output toggle
bool flagQuietMode = false;     // Suppress standard logs (warn/error still surface)
bool flagJsonMode = false;      // Emit JSON logs

enum class CliLogLevel {
  Error = 0,
  WARN = 1,
  INFO = 2,
  VERBOSE = 3,
  DEBUG = 4,
  NONE = 5
};

static CliLogLevel gLogLevel = CliLogLevel::INFO;
static bool gLogJson = false;
static bool gMcpStdioMode = false;
static String gLogSubcommand = "";
static Vector<String> gLogArgv;
static String gLogFilePath = "";
static std::ofstream gLogFile;
static std::mutex gLogFileMutex;
static const steady_clock::time_point gLogStartMono = steady_clock::now();
static steady_clock::time_point gLogLastMono = gLogStartMono;
static const char* gCliDisplayName = "oroc";
static UserConfigFormat gEmbeddedConfigFormat = UserConfigFormat::Ini;
static Path gActiveConfigPath;

// Global update server configuration for HTTP/TCP/UDP bindings.
static fs::path gUpdateServerRootPath;
static String gUpdateServerManifestName;

// Oro Update Protocol (OUP) binary framing for TCP/UDP bindings.
static constexpr uint8_t OUP_VERSION = 1;
static constexpr uint8_t OUP_MSG_CHECK = 0x01;
static constexpr uint8_t OUP_MSG_RESPONSE = 0x02;
static constexpr uint8_t OUP_MSG_ERROR = 0x04;

// Forward declarations for logging helpers used by update-specific utilities
// defined earlier in this file.
static void logInfo (const String& s);
static void logWarn (const String& s);
static void logError (const String& s);
static void logVerbose (const String& s);
static void logDebug (const String& s);

// Forward declarations for helpers defined later in this file but used by
// update-specific utilities above.
inline String readFile (fs::path path);
inline String prettyJson (const String& input);

static std::map<String, String> collectDependencyVersions () {
  std::map<String, String> versions;

  // Current Oro Runtime and CLI versions.
  versions["oro"] = runtime::VERSION_STRING;
  versions["oroc"] = VERSION_STRING;

  // Core libuv.
  versions["uv"] = uv_version_string();

  // Llama build number encoded as 0.0.<build>.
  versions["llama"] = String("0.0.") + std::to_string(LLAMA_BUILD_NUMBER);

#if ORO_RUNTIME_HAS_WHISPER
  String whisperVersion;
  if (const char* value = whisper_version()) {
    whisperVersion = value;
  }

  if (!whisperVersion.empty()) {
    versions["whisper"] = whisperVersion;
  }
#endif

  // Optional native libraries mirrored from platform.primordials.
  String sqliteVersion;
  if (const char* sqliteValue = sqlite3_libversion()) {
    sqliteVersion = sqliteValue;
  }

  if (!sqliteVersion.empty()) {
    versions["sqlite"] = sqliteVersion;
  }

  const auto irohVersion = oro::runtime::iroh::Library::shared().version();
  if (!irohVersion.empty()) {
    versions["iroh"] = irohVersion;
  }

#if ORO_RUNTIME_HAS_LIBUSB
  String libusbVersion;
  if (const libusb_version* info = libusb_get_version()) {
    libusbVersion = std::to_string(info->major);
    libusbVersion += ".";
    libusbVersion += std::to_string(info->minor);
    libusbVersion += ".";
    libusbVersion += std::to_string(info->micro);
    if (info->nano != 0) {
      libusbVersion += ".";
      libusbVersion += std::to_string(info->nano);
    }
    if (info->rc != nullptr && info->rc[0] != '\0') {
      libusbVersion += "-";
      libusbVersion += info->rc;
    }
  }

  if (!libusbVersion.empty()) {
    versions["libusb"] = libusbVersion;
  }
#endif

#if ORO_RUNTIME_HAS_SODIUM
  String libsodiumVersion;
  if (const char* sodiumValue = sodium_version_string()) {
    libsodiumVersion = sodiumValue;
  }
  if (!libsodiumVersion.empty()) {
    versions["libsodium"] = libsodiumVersion;
  }
#endif

#if ORO_RUNTIME_HAS_MBEDTLS && defined(MBEDTLS_VERSION_C)
  String mbedtlsVersion;
  {
    char buffer[64] = {0};
    mbedtls_version_get_string_full(buffer);
    mbedtlsVersion = buffer;
  }

  if (!mbedtlsVersion.empty()) {
    versions["mbedtls"] = mbedtlsVersion;
  }
#endif

#if ORO_RUNTIME_HAS_CPPHTTPLIB
  String cppHttplibVersion = CPPHTTPLIB_VERSION;
  if (!cppHttplibVersion.empty()) {
    versions["cpp_httplib"] = cppHttplibVersion;
  }
#endif

#if ORO_RUNTIME_HAS_NLOHMANN_JSON
  String nlohmannJsonVersion = std::to_string(NLOHMANN_JSON_VERSION_MAJOR);
  nlohmannJsonVersion += ".";
  nlohmannJsonVersion += std::to_string(NLOHMANN_JSON_VERSION_MINOR);
  nlohmannJsonVersion += ".";
  nlohmannJsonVersion += std::to_string(NLOHMANN_JSON_VERSION_PATCH);

  if (!nlohmannJsonVersion.empty()) {
    versions["nlohmann_json"] = nlohmannJsonVersion;
  }
#endif

  return versions;
}

// Lightweight structural validator for update manifests following
// schemas/update-manifest.schema.json. This keeps CLI ergonomics strong
// without introducing a full JSON Schema engine.
static bool validateUpdateManifest (const JSON::Any& any, String& error) {
  if (!any.isObject()) {
    error = "manifest validation failed: manifest must be a JSON object";
    return false;
  }

  auto& obj = any.as<JSON::Object>();

  // schemaVersion (must be integer 1)
  if (!obj.contains("schemaVersion")) {
    error = "manifest validation failed: missing required field 'schemaVersion'";
    return false;
  }
  const auto& schemaVersionAny = obj.get("schemaVersion");
  if (!schemaVersionAny.isNumber()) {
    error = "manifest validation failed: 'schemaVersion' must be a number";
    return false;
  }
  const auto schemaVersion = schemaVersionAny.as<JSON::Number>().value();
  if (schemaVersion != 1.0) {
    error = "manifest validation failed: 'schemaVersion' must be 1";
    return false;
  }

  // appId (non-empty string)
  if (!obj.contains("appId") || !obj.get("appId").isString()) {
    error = "manifest validation failed: missing required string field 'appId'";
    return false;
  }
  if (obj.get("appId").str().size() == 0) {
    error = "manifest validation failed: 'appId' must be a non-empty string";
    return false;
  }

  // generatedAt (string; format is not enforced here)
  if (!obj.contains("generatedAt") || !obj.get("generatedAt").isString()) {
    error = "manifest validation failed: missing required string field 'generatedAt'";
    return false;
  }

  // channels: non-empty array of non-empty strings
  if (!obj.contains("channels") || !obj.get("channels").isArray()) {
    error = "manifest validation failed: missing required array field 'channels'";
    return false;
  }
  auto& channelsAny = obj.get("channels");
  auto& channels = channelsAny.as<JSON::Array>();
  if (channels.size() == 0) {
    error = "manifest validation failed: 'channels' must contain at least one entry";
    return false;
  }
  for (size_t i = 0; i < channels.size(); i++) {
    const auto& chAny = channels.get(static_cast<unsigned int>(i));
    if (!chAny.isString() || chAny.str().size() == 0) {
      error = "manifest validation failed: each entry in 'channels' must be a non-empty string";
      return false;
    }
  }

  // updates: array (may be empty), but when present entries must satisfy required shape.
  if (!obj.contains("updates") || !obj.get("updates").isArray()) {
    error = "manifest validation failed: missing required array field 'updates'";
    return false;
  }
  auto& updatesAny = obj.get("updates");
  auto& updates = updatesAny.as<JSON::Array>();

  for (size_t i = 0; i < updates.size(); i++) {
    const auto& updateAny = updates.get(static_cast<unsigned int>(i));
    if (!updateAny.isObject()) {
      error = "manifest validation failed: each entry in 'updates' must be an object";
      return false;
    }
    auto& updateObj = updateAny.as<JSON::Object>();

    // id
    if (!updateObj.contains("id") || !updateObj.get("id").isString() || updateObj.get("id").str().size() == 0) {
      error = "manifest validation failed: each update must include a non-empty string 'id'";
      return false;
    }

    // version
    if (!updateObj.contains("version") || !updateObj.get("version").isString() || updateObj.get("version").str().size() == 0) {
      error = "manifest validation failed: each update must include a non-empty string 'version'";
      return false;
    }

    // channel
    if (!updateObj.contains("channel") || !updateObj.get("channel").isString() || updateObj.get("channel").str().size() == 0) {
      error = "manifest validation failed: each update must include a non-empty string 'channel'";
      return false;
    }

    // critical
    if (!updateObj.contains("critical") || !updateObj.get("critical").isBoolean()) {
      error = "manifest validation failed: each update must include boolean field 'critical'";
      return false;
    }

    // targets: array (may be empty; entries must satisfy required target fields)
    if (!updateObj.contains("targets") || !updateObj.get("targets").isArray()) {
      error = "manifest validation failed: each update must include array field 'targets'";
      return false;
    }
    auto& targetsAny = updateObj.get("targets");
    auto& targets = targetsAny.as<JSON::Array>();

    for (size_t j = 0; j < targets.size(); j++) {
      const auto& targetAny = targets.get(static_cast<unsigned int>(j));
      if (!targetAny.isObject()) {
        error = "manifest validation failed: each entry in 'targets' must be an object";
        return false;
      }
      auto& targetObj = targetAny.as<JSON::Object>();

      auto requireStringField = [&](const char* fieldName) -> bool {
        if (!targetObj.contains(fieldName) || !targetObj.get(fieldName).isString() || targetObj.get(fieldName).str().size() == 0) {
          error = "manifest validation failed: each target must include non-empty string field '" + String(fieldName) + "'";
          return false;
        }
        return true;
      };

      if (!requireStringField("platform")) return false;
      if (!requireStringField("arch")) return false;
      if (!requireStringField("artifactUrl")) return false;
      if (!requireStringField("hashAlgorithm")) return false;
      if (!requireStringField("hash")) return false;

      if (!targetObj.contains("length") || !targetObj.get("length").isNumber()) {
        error = "manifest validation failed: each target must include numeric field 'length'";
        return false;
      }
      auto lengthVal = targetObj.get("length").as<JSON::Number>().value();
      if (lengthVal < 0) {
        error = "manifest validation failed: target 'length' must be non-negative";
        return false;
      }
    }
  }

  return true;
}

// Strict validator that builds on top of validateUpdateManifest and adds
// additional consistency rules. Intended for CLI use (update-validate --strict)
// rather than being enforced unconditionally.
static bool validateUpdateManifestStrict (const JSON::Any& any, String& error) {
  if (!validateUpdateManifest(any, error)) {
    return false;
  }

  auto& obj = any.as<JSON::Object>();

  // Build a set of declared channels.
  std::unordered_set<String> channelsSet;
  auto& channels = obj.get("channels").as<JSON::Array>();
  for (size_t i = 0; i < channels.size(); i++) {
    const auto& chAny = channels.get(static_cast<unsigned int>(i));
    if (chAny.isString()) {
      channelsSet.insert(chAny.str());
    }
  }

  auto& updates = obj.get("updates").as<JSON::Array>();
  for (size_t i = 0; i < updates.size(); i++) {
    const auto& updateAny = updates.get(static_cast<unsigned int>(i));
    if (!updateAny.isObject()) continue;
    auto& updateObj = updateAny.as<JSON::Object>();

    // Ensure update.channel is declared in top-level channels.
    if (updateObj.contains("channel") && updateObj.get("channel").isString()) {
      const String ch = updateObj.get("channel").str();
      if (channelsSet.find(ch) == channelsSet.end()) {
        error = "manifest validation failed (strict): update channel '" + ch + "' is not present in top-level 'channels'";
        return false;
      }
    }

    if (!updateObj.contains("targets") || !updateObj.get("targets").isArray()) {
      continue;
    }

    auto& targets = updateObj.get("targets").as<JSON::Array>();
    for (size_t j = 0; j < targets.size(); j++) {
      const auto& targetAny = targets.get(static_cast<unsigned int>(j));
      if (!targetAny.isObject()) continue;
      auto& targetObj = targetAny.as<JSON::Object>();

      if (!targetObj.contains("artifactUrl") || !targetObj.get("artifactUrl").isString()) {
        continue;
      }

      const String artifactUrl = targetObj.get("artifactUrl").str();
      for (char ch : artifactUrl) {
        if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') {
          error = "manifest validation failed (strict): 'artifactUrl' must not contain whitespace characters";
          return false;
        }
      }
    }
  }

  return true;
}

static inline std::array<unsigned char, 8> encodeUpdateHeader (uint8_t msgType, uint32_t length) {
  std::array<unsigned char, 8> h{};
  h[0] = OUP_VERSION;
  h[1] = msgType;
  // bytes 2-3 reserved; kept as zero for now
  h[2] = 0;
  h[3] = 0;
  h[4] = static_cast<unsigned char>((length >> 24) & 0xff);
  h[5] = static_cast<unsigned char>((length >> 16) & 0xff);
  h[6] = static_cast<unsigned char>((length >> 8) & 0xff);
  h[7] = static_cast<unsigned char>(length & 0xff);
  return h;
}

static inline bool decodeUpdateHeader (
  const unsigned char* data,
  size_t size,
  uint8_t& version,
  uint8_t& msgType,
  uint32_t& length
) {
  if (size < 8) return false;
  version = data[0];
  msgType = data[1];
  length =
    (static_cast<uint32_t>(data[4]) << 24) |
    (static_cast<uint32_t>(data[5]) << 16) |
    (static_cast<uint32_t>(data[6]) << 8)  |
    (static_cast<uint32_t>(data[7]));
  return true;
}

// Build a CHECK RESPONSE JSON object for a given appId using the global
// update server configuration.
static JSON::Object buildUpdateServerCheckResponse (const String& appId) {
  // Treat appId as a single safe path segment under the configured root.
  // Reject obvious path traversal or invalid characters up front and simply
  // report that no update is available for such IDs.
  for (const auto ch : appId) {
    if (ch == '/' || ch == '\\' || ch == ':') {
      return JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "hasUpdate", false }
      };
    }
  }

  fs::path manifestPath = gUpdateServerRootPath / appId / gUpdateServerManifestName;
  std::error_code ec;
  if (!fs::exists(manifestPath, ec) || !fs::is_regular_file(manifestPath, ec)) {
    return JSON::Object::Entries {
      { "schemaVersion", 1 },
      { "hasUpdate", false }
    };
  }

  const String manifestUrl = String("/") + appId + "/" + gUpdateServerManifestName;
  return JSON::Object::Entries {
    { "schemaVersion", 1 },
    { "hasUpdate", true },
    { "selectedUpdateId", "" },
    { "manifestInline", false },
    { "manifestUrl", manifestUrl }
  };
}

// Helper used by update-info when following manifest URLs returned by
// HTTP/TCP/UDP servers. It fetches a manifest over HTTP(S), optionally verifies
// it using libsodium, validates its structure, cross-checks the appId (when an
// expected one is provided), and then prints it to stdout.
static void fetchManifestFromUrlAndMaybeVerify (
  const String& manifestUrl,
  const String& keysPath,
  const String& publicKeyArg,
  const String& expectedAppId
) {
#if !ORO_CLI_HAS_CPPHTTPLIB
  (void) manifestUrl;
  (void) keysPath;
  (void) publicKeyArg;
  (void) expectedAppId;
  logError("update-info: HTTP client support is not available in this build (cpp-httplib missing)");
  exit(1);
#else
  const bool wantVerify = keysPath.size() > 0 || publicKeyArg.size() > 0;
  if (wantVerify && !ORO_CLI_HAS_SODIUM) {
    logError("update-info: manifest verification requires libsodium; rebuild with libsodium headers and libraries available.");
    exit(1);
  }

  URL url(manifestUrl);
  if (url.scheme.size() == 0 || url.hostname.size() == 0) {
    logError("update-info: manifestUrl from server must include a scheme and hostname");
    exit(1);
  }

  String schemeLower = toLowerCase(url.scheme);
  if (schemeLower != "http" && schemeLower != "https") {
    logError("update-info: manifestUrl from server must use http or https");
    exit(1);
  }

  int urlPort = 0;
  if (url.port.size() > 0) {
    try {
      urlPort = std::stoi(url.port);
    } catch (...) {
      urlPort = 0;
    }
  }
  if (urlPort <= 0) {
    urlPort = schemeLower == "https" ? 443 : 80;
  }

  String path = url.pathname.size() > 0 ? url.pathname : String("/");
  if (!url.search.empty()) {
    path += url.search;
  }

  httplib::Result res;
  if (schemeLower == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient client(url.hostname.c_str(), urlPort);
    client.enable_server_certificate_verification(true);
    client.set_follow_location(true);
    res = client.Get(path.c_str());
#else
    logError("update-info: HTTPS support is not enabled in this build");
    exit(1);
#endif
  } else {
    httplib::Client client(url.hostname.c_str(), urlPort);
    client.set_follow_location(true);
    res = client.Get(path.c_str());
  }

  if (!res) {
    logError("update-info: failed to fetch manifest from " + manifestUrl);
    exit(1);
  }

  if (res->status < 200 || res->status >= 300) {
    logError("update-info: HTTP " + std::to_string(res->status) + " while fetching manifest from " + manifestUrl);
    exit(1);
  }

  String manifestText(res->body);

  bool signatureFound = false;
  String signatureBody;

  if (wantVerify) {
    String manifestPath = url.pathname;
    String filename = manifestPath;
    String prefix;
    auto slashPos = manifestPath.rfind('/');
    if (slashPos != String::npos) {
      prefix = manifestPath.substr(0, slashPos + 1);
      filename = manifestPath.substr(slashPos + 1);
    } else {
      prefix = "/";
    }

    fs::path filenamePath(filename.c_str());
    String stem = filenamePath.stem().string();
    if (stem.size() == 0) {
      stem = filename;
    }

    String sigPath = prefix + stem + ".sig";

    httplib::Result sigRes;
    if (schemeLower == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
      httplib::SSLClient client(url.hostname.c_str(), urlPort);
      client.enable_server_certificate_verification(true);
      client.set_follow_location(true);
      sigRes = client.Get(sigPath.c_str());
#else
      logError("update-info: HTTPS support is not enabled in this build");
      exit(1);
#endif
    } else {
      httplib::Client client(url.hostname.c_str(), urlPort);
      client.set_follow_location(true);
      sigRes = client.Get(sigPath.c_str());
    }

    if (sigRes && sigRes->status >= 200 && sigRes->status < 300) {
      signatureFound = true;
      signatureBody = sigRes->body;
    }

#if !ORO_CLI_HAS_SODIUM
    (void) signatureFound;
    (void) signatureBody;
    logError("update-info: manifest verification requires libsodium; rebuild with libsodium headers and libraries available.");
    exit(1);
#else
    if (!signatureFound || signatureBody.size() == 0) {
      logError("update-info: cannot verify manifest without a signature file; the expected signature path was derived from manifestUrl");
      exit(1);
    }

    String manifestBytes = manifestText;

    String signatureText = signatureBody;
    JSON::Any sigAny;
    try {
      sigAny = JSON::parse(signatureText);
    } catch (const JSON::Error& error) {
      logError("update-info: manifest signature JSON is invalid: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-info: manifest signature JSON is invalid: " + String(error.what()));
      exit(1);
    }

    if (!sigAny.isObject()) {
      logError("update-info: manifest signature JSON must be an object");
      exit(1);
    }

    auto& sigObj = sigAny.as<JSON::Object>();
    if (sigObj.contains("algorithm") && sigObj.get("algorithm").str() != "ed25519") {
      logError("update-info: unsupported signature algorithm in manifest.sig; expected 'ed25519'");
      exit(1);
    }
    if (!sigObj.contains("signature")) {
      logError("update-info: manifest signature JSON missing 'signature' field");
      exit(1);
    }
    String sigHex = sigObj.get("signature").str();

    const String sigDecoded = oro::runtime::bytes::decodeHexString(sigHex);
    Vector<uint8_t> signature(sigDecoded.begin(), sigDecoded.end());

    if (signature.size() != crypto_sign_BYTES) {
      logError("update-info: manifest signature length does not match Ed25519 detached signature size");
      exit(1);
    }

    Vector<uint8_t> publicKey;
    if (keysPath.size() > 0) {
      const String jsonText = readFile(keysPath);
      if (jsonText.size() == 0) {
        logError("update-info: keys file is empty or unreadable: " + keysPath);
        exit(1);
      }

      try {
        auto any = JSON::parse(jsonText);
        if (!any.isObject()) {
          logError("update-info: keys file must contain a JSON object");
          exit(1);
        }

        auto& obj = any.as<JSON::Object>();
        String pkHex;
        if (obj.contains("publicKey")) {
          pkHex = obj.at("publicKey").str();
        } else if (obj.contains("key")) {
          pkHex = obj.at("key").str();
        }

        if (pkHex.size() == 0) {
          logError("update-info: keys file must include 'publicKey' or 'key'");
          exit(1);
        }

        const String decoded = oro::runtime::bytes::decodeHexString(pkHex);
        publicKey.assign(decoded.begin(), decoded.end());
      } catch (const JSON::Error& error) {
        logError("update-info: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      } catch (const std::exception& error) {
        logError("update-info: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      }
    } else {
      const String decoded = oro::runtime::bytes::decodeHexString(publicKeyArg);
      publicKey.assign(decoded.begin(), decoded.end());
    }

    if (publicKey.size() != crypto_sign_PUBLICKEYBYTES) {
      logError("update-info: public key must be an Ed25519 public key (hex-encoded)");
      exit(1);
    }

    if (sodium_init() < 0) {
      logError("update-info: unable to initialize libsodium for manifest verification");
      exit(1);
    }

    const int ok = crypto_sign_verify_detached(
      signature.data(),
      reinterpret_cast<const unsigned char*>(manifestBytes.data()),
      manifestBytes.size(),
      publicKey.data()
    );

    if (ok != 0) {
      logError("update-info: manifest signature verification FAILED");
      exit(1);
    }

    logInfo("update-info: manifest signature is VALID for the provided public key");
#endif
  }

  JSON::Any manifestAny;
  try {
    manifestAny = JSON::parse(manifestText);
  } catch (const JSON::Error& error) {
    logError("update-info: manifest at " + manifestUrl + " is not valid JSON: " + String(error.what()));
    exit(1);
  } catch (const std::exception& error) {
    logError("update-info: manifest at " + manifestUrl + " is not valid JSON: " + String(error.what()));
    exit(1);
  }

  if (!manifestAny.isObject()) {
    logError("update-info: manifest at " + manifestUrl + " must be a JSON object");
    exit(1);
  }

  String validationError;
  if (!validateUpdateManifest(manifestAny, validationError)) {
    logError("update-info: " + validationError);
    exit(1);
  }

  auto& manifestObj = manifestAny.as<JSON::Object>();
  String manifestAppId = manifestObj.contains("appId") ? manifestObj.get("appId").str() : "";

  if (expectedAppId.size() > 0 && manifestAppId.size() > 0 && manifestAppId != expectedAppId) {
    logError(
      "update-info: manifest appId '" + manifestAppId +
      "' does not match expected appId '" + expectedAppId + "'"
    );
    exit(1);
  }
  size_t updateCount = 0;
  if (manifestObj.contains("updates") && manifestObj.get("updates").isArray()) {
    updateCount = manifestObj.get("updates").as<JSON::Array>().size();
  }

  logInfo("update-info: fetched manifest for appId '" + manifestAppId + "' with " + std::to_string(updateCount) + " update(s)");

  std::cout << prettyJson(manifestAny.str()) << std::endl;
  exit(0);
#endif
}

// TCP server state for OUP CHECK/RESPONSE.
struct UpdateTcpClientState {
  uv_tcp_t handle;
  std::vector<unsigned char> buffer;
};

static void updateTcpAlloc (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void) handle;
  auto* base = new char[suggested_size];
  buf->base = base;
  buf->len = suggested_size;
}

struct UpdateTcpWriteContext {
  uv_write_t req;
  String data;
};

static void updateTcpOnWriteDone (uv_write_t* req, int /*status*/) {
  auto* ctx = static_cast<UpdateTcpWriteContext*>(req->data);
  delete ctx;
}

static void updateTcpCloseClient (uv_handle_t* h) {
  auto* client = static_cast<UpdateTcpClientState*>(uv_handle_get_data(h));
  if (client != nullptr) {
    delete client;
  }
}

static void updateTcpSendResponse (UpdateTcpClientState* client, uint8_t msgTypeOut, const String& body) {
  if (client == nullptr) return;
  const auto header = encodeUpdateHeader(msgTypeOut, static_cast<uint32_t>(body.size()));
  auto* ctx = new UpdateTcpWriteContext();
  ctx->data.reserve(header.size() + body.size());
  ctx->data.append(reinterpret_cast<const char*>(header.data()), header.size());
  ctx->data.append(body);

  uv_buf_t buf = uv_buf_init(ctx->data.data(), static_cast<unsigned int>(ctx->data.size()));
  ctx->req.data = ctx;
  uv_write(
    &ctx->req,
    reinterpret_cast<uv_stream_t*>(&client->handle),
    &buf,
    1,
    updateTcpOnWriteDone
  );
}

static void updateTcpOnRead (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  auto* client = static_cast<UpdateTcpClientState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(stream)));

  if (nread <= 0) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    if (client != nullptr) {
      uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), updateTcpCloseClient);
    }
    return;
  }

  if (!buf || !buf->base || client == nullptr) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    return;
  }

  client->buffer.insert(
    client->buffer.end(),
    reinterpret_cast<unsigned char*>(buf->base),
    reinterpret_cast<unsigned char*>(buf->base) + nread
  );
  delete[] buf->base;

  // Process as many complete messages as possible.
  while (true) {
    if (client->buffer.size() < 8) {
      break;
    }

    uint8_t version = 0;
    uint8_t msgType = 0;
    uint32_t length = 0;
    if (!decodeUpdateHeader(client->buffer.data(), client->buffer.size(), version, msgType, length)) {
      break;
    }

    // Guard against unreasonable payload sizes.
    if (length > (1024u * 1024u)) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "payload too large" }
      };
      updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
      client->buffer.clear();
      break;
    }

    if (client->buffer.size() < static_cast<size_t>(8 + length)) {
      break;
    }

    const unsigned char* payloadBegin = client->buffer.data() + 8;
    String payload(
      reinterpret_cast<const char*>(payloadBegin),
      static_cast<size_t>(length)
    );

    client->buffer.erase(
      client->buffer.begin(),
      client->buffer.begin() + static_cast<std::ptrdiff_t>(8 + length)
    );

    if (version != OUP_VERSION || msgType != OUP_MSG_CHECK) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "unsupported message type or version" }
      };
      updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
      continue;
    }

    try {
      JSON::Any any = JSON::parse(payload);
      if (!any.isObject()) {
        JSON::Object err = JSON::Object::Entries {
          { "schemaVersion", 1 },
          { "error", "CHECK payload must be a JSON object" }
        };
        updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
        continue;
      }

      auto& obj = any.as<JSON::Object>();
      if (!obj.contains("appId") || !obj.get("appId").isString()) {
        JSON::Object err = JSON::Object::Entries {
          { "schemaVersion", 1 },
          { "error", "CHECK payload must include string appId" }
        };
        updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
        continue;
      }

      const String appId = obj.get("appId").str();
      if (appId.size() == 0) {
        JSON::Object err = JSON::Object::Entries {
          { "schemaVersion", 1 },
          { "error", "CHECK payload must include non-empty appId" }
        };
        updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
        continue;
      }

      JSON::Object response = buildUpdateServerCheckResponse(appId);
      updateTcpSendResponse(client, OUP_MSG_RESPONSE, response.str());
    } catch (...) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "Invalid JSON in CHECK payload" }
      };
      updateTcpSendResponse(client, OUP_MSG_ERROR, err.str());
    }
  }
}

static void updateTcpOnConnection (uv_stream_t* server, int status) {
  if (status < 0) {
    return;
  }

  auto* client = new UpdateTcpClientState();
  client->buffer.clear();

  uv_loop_t* loop = uv_handle_get_loop(reinterpret_cast<uv_handle_t*>(server));
  if (uv_tcp_init(loop, &client->handle) != 0) {
    delete client;
    return;
  }

  uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&client->handle), client);

  if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->handle)) == 0) {
    uv_read_start(
      reinterpret_cast<uv_stream_t*>(&client->handle),
      updateTcpAlloc,
      updateTcpOnRead
    );
  } else {
    uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), updateTcpCloseClient);
  }
}

// UDP server state for OUP CHECK/RESPONSE.
struct UpdateUdpSendContext {
  uv_udp_send_t req;
  String data;
};

static void updateUdpAlloc (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void) handle;
  auto* base = new char[suggested_size];
  buf->base = base;
  buf->len = suggested_size;
}

static void updateUdpOnSendDone (uv_udp_send_t* req, int /*status*/) {
  auto* ctx = static_cast<UpdateUdpSendContext*>(req->data);
  delete ctx;
}

static void updateUdpSendResponse (
  uv_udp_t* handle,
  const sockaddr* addr,
  uint8_t msgTypeOut,
  const String& body
) {
  if (handle == nullptr || addr == nullptr) return;
  const auto header = encodeUpdateHeader(msgTypeOut, static_cast<uint32_t>(body.size()));
  auto* ctx = new UpdateUdpSendContext();
  ctx->data.reserve(header.size() + body.size());
  ctx->data.append(reinterpret_cast<const char*>(header.data()), header.size());
  ctx->data.append(body);

  uv_buf_t buf = uv_buf_init(ctx->data.data(), static_cast<unsigned int>(ctx->data.size()));
  ctx->req.data = ctx;

  uv_udp_send(
    &ctx->req,
    handle,
    &buf,
    1,
    addr,
    updateUdpOnSendDone
  );
}

static void updateUdpOnRecv (
  uv_udp_t* handle,
  ssize_t nread,
  const uv_buf_t* buf,
  const sockaddr* addr,
  unsigned /*flags*/
) {
  if (nread <= 0 || !buf || !buf->base) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    return;
  }

  const unsigned char* data = reinterpret_cast<const unsigned char*>(buf->base);
  size_t size = static_cast<size_t>(nread);

  uint8_t version = 0;
  uint8_t msgType = 0;
  uint32_t length = 0;
  if (!decodeUpdateHeader(data, size, version, msgType, length)) {
    delete[] buf->base;
    return;
  }

  if (size < static_cast<size_t>(8 + length) || length > (1024u * 1024u)) {
    delete[] buf->base;
    return;
  }

  String payload(
    reinterpret_cast<const char*>(data + 8),
    static_cast<size_t>(length)
  );
  delete[] buf->base;

  if (version != OUP_VERSION || msgType != OUP_MSG_CHECK) {
    JSON::Object err = JSON::Object::Entries {
      { "schemaVersion", 1 },
      { "error", "unsupported message type or version" }
    };
    updateUdpSendResponse(handle, addr, OUP_MSG_ERROR, err.str());
    return;
  }

  try {
    JSON::Any any = JSON::parse(payload);
    if (!any.isObject()) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "CHECK payload must be a JSON object" }
      };
      updateUdpSendResponse(handle, addr, OUP_MSG_ERROR, err.str());
      return;
    }

    auto& obj = any.as<JSON::Object>();
    if (!obj.contains("appId") || !obj.get("appId").isString()) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "CHECK payload must include string appId" }
      };
      updateUdpSendResponse(handle, addr, OUP_MSG_ERROR, err.str());
      return;
    }

    const String appId = obj.get("appId").str();
    if (appId.size() == 0) {
      JSON::Object err = JSON::Object::Entries {
        { "schemaVersion", 1 },
        { "error", "CHECK payload must include non-empty appId" }
      };
      updateUdpSendResponse(handle, addr, OUP_MSG_ERROR, err.str());
      return;
    }

    JSON::Object response = buildUpdateServerCheckResponse(appId);
    updateUdpSendResponse(handle, addr, OUP_MSG_RESPONSE, response.str());
  } catch (...) {
    JSON::Object err = JSON::Object::Entries {
      { "schemaVersion", 1 },
      { "error", "Invalid JSON in CHECK payload" }
    };
    updateUdpSendResponse(handle, addr, OUP_MSG_ERROR, err.str());
  }
}

// Lightweight TCP client state for the update-info command (OUP CHECK/RESPONSE).
struct UpdateInfoTcpState {
  uv_loop_t loop;
  uv_tcp_t handle;
  uv_connect_t connectReq;
  uv_write_t writeReq;
  uv_timer_t timer;
  std::vector<unsigned char> buffer;
  String requestPayload;
  String outbound;
  String responseBody;
  String error;
  uint64_t timeoutMs = 0;
  bool useTimer = false;
  bool done = false;
  bool success = false;
};

static void updateInfoTcpAlloc (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void) handle;
  auto* base = new char[suggested_size];
  buf->base = base;
  buf->len = suggested_size;
}

static void updateInfoTcpOnWrite (uv_write_t* req, int /*status*/) {
  (void) req;
}

static void updateInfoTcpStopTimer (UpdateInfoTcpState* state) {
  if (state == nullptr || !state->useTimer) return;
  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->timer))) {
    uv_timer_stop(&state->timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&state->timer), nullptr);
  }
}

static void updateInfoTcpOnTimeout (uv_timer_t* handle) {
  auto* state = static_cast<UpdateInfoTcpState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(handle)));
  if (state == nullptr || state->done) {
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(handle))) {
      uv_timer_stop(handle);
      uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    }
    return;
  }

  state->error = "TCP CHECK timed out after " + std::to_string(state->timeoutMs) + "ms";
  state->success = false;
  state->done = true;

  uv_read_stop(reinterpret_cast<uv_stream_t*>(&state->handle));
  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->handle))) {
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
  }

  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(handle))) {
    uv_timer_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
  }
}

static void updateInfoTcpOnRead (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  auto* state = static_cast<UpdateInfoTcpState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(stream)));

  if (nread <= 0) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    if (state != nullptr && !state->done) {
      state->error = "connection closed before response";
      state->success = false;
      state->done = true;
    }
    uv_read_stop(stream);
    uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
    updateInfoTcpStopTimer(state);
    return;
  }

  if (!buf || !buf->base || state == nullptr) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    return;
  }

  state->buffer.insert(
    state->buffer.end(),
    reinterpret_cast<unsigned char*>(buf->base),
    reinterpret_cast<unsigned char*>(buf->base) + nread
  );
  delete[] buf->base;

  while (true) {
    if (state->buffer.size() < 8) {
      break;
    }

    uint8_t version = 0;
    uint8_t msgType = 0;
    uint32_t length = 0;
    if (!decodeUpdateHeader(state->buffer.data(), state->buffer.size(), version, msgType, length)) {
      break;
    }

    if (length > (1024u * 1024u)) {
      state->error = "response payload too large";
      state->success = false;
      state->done = true;
      uv_read_stop(stream);
      uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
      return;
    }

    if (state->buffer.size() < static_cast<size_t>(8 + length)) {
      break;
    }

    const unsigned char* payloadBegin = state->buffer.data() + 8;
    String payload(
      reinterpret_cast<const char*>(payloadBegin),
      static_cast<size_t>(length)
    );

    state->buffer.erase(
      state->buffer.begin(),
      state->buffer.begin() + static_cast<std::ptrdiff_t>(8 + length)
    );

    if (version != OUP_VERSION) {
      state->error = "unsupported protocol version";
      state->success = false;
      state->done = true;
      uv_read_stop(stream);
      uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
      updateInfoTcpStopTimer(state);
      return;
    }

    if (msgType == OUP_MSG_RESPONSE) {
      state->responseBody = payload;
      state->success = true;
      state->done = true;
      uv_read_stop(stream);
      uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
      updateInfoTcpStopTimer(state);
      return;
    }

    if (msgType == OUP_MSG_ERROR) {
      state->error = payload;
      state->success = false;
      state->done = true;
      uv_read_stop(stream);
      uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
      updateInfoTcpStopTimer(state);
      return;
    }

    state->error = "unexpected message type in TCP response";
    state->success = false;
    state->done = true;
    uv_read_stop(stream);
    uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
    updateInfoTcpStopTimer(state);
    return;
  }
}

static void updateInfoTcpOnConnect (uv_connect_t* req, int status) {
  auto* state = static_cast<UpdateInfoTcpState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(req->handle)));

  if (status < 0 || state == nullptr) {
    if (state != nullptr) {
      state->error = "failed to connect to update server over TCP";
      state->success = false;
      state->done = true;
    }
    uv_close(reinterpret_cast<uv_handle_t*>(req->handle), nullptr);
    return;
  }

  const auto header = encodeUpdateHeader(
    OUP_MSG_CHECK,
    static_cast<uint32_t>(state->requestPayload.size())
  );

  state->outbound.clear();
  state->outbound.reserve(header.size() + state->requestPayload.size());
  state->outbound.append(reinterpret_cast<const char*>(header.data()), header.size());
  state->outbound.append(state->requestPayload);

  uv_buf_t buf = uv_buf_init(
    state->outbound.data(),
    static_cast<unsigned int>(state->outbound.size())
  );
  state->writeReq.data = state;

  if (uv_write(
        &state->writeReq,
        reinterpret_cast<uv_stream_t*>(&state->handle),
        &buf,
        1,
        updateInfoTcpOnWrite
      ) != 0) {
    state->error = "failed to send CHECK message over TCP";
    state->success = false;
    state->done = true;
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    return;
  }

  if (uv_read_start(
        reinterpret_cast<uv_stream_t*>(&state->handle),
        updateInfoTcpAlloc,
        updateInfoTcpOnRead
      ) != 0) {
    state->error = "failed to start reading TCP response";
    state->success = false;
    state->done = true;
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    return;
  }
}

static bool runUpdateInfoTcp (
  const String& host,
  int port,
  const String& payload,
  String& responseBody,
  String& error,
  uint64_t timeoutMs
) {
  auto* state = new UpdateInfoTcpState();
  state->requestPayload = payload;

  if (uv_loop_init(&state->loop) != 0) {
    error = "update-info: failed to initialize TCP event loop";
    delete state;
    return false;
  }

  if (uv_tcp_init(&state->loop, &state->handle) != 0) {
    error = "update-info: failed to initialize TCP client handle";
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&state->handle), state);

  state->timeoutMs = timeoutMs;
  state->useTimer = timeoutMs > 0;
  if (state->useTimer) {
    if (uv_timer_init(&state->loop, &state->timer) != 0) {
      error = "update-info: failed to initialize TCP timeout timer";
      uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
      uv_loop_close(&state->loop);
      delete state;
      return false;
    }
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&state->timer), state);
    if (uv_timer_start(&state->timer, updateInfoTcpOnTimeout, timeoutMs, 0) != 0) {
      error = "update-info: failed to start TCP timeout timer";
      uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
      uv_close(reinterpret_cast<uv_handle_t*>(&state->timer), nullptr);
      uv_loop_close(&state->loop);
      delete state;
      return false;
    }
  }

  sockaddr_in dest;
  if (uv_ip4_addr(host.c_str(), port, &dest) != 0) {
    error = "update-info: TCP mode currently supports IPv4 addresses only";
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  int r = uv_tcp_connect(
    &state->connectReq,
    &state->handle,
    reinterpret_cast<const struct sockaddr*>(&dest),
    updateInfoTcpOnConnect
  );
  if (r != 0) {
    error = "update-info: failed to initiate TCP connection";
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  uv_run(&state->loop, UV_RUN_DEFAULT);
  uv_loop_close(&state->loop);

  const bool ok = state->success;
  responseBody = state->responseBody;
  if (!ok && state->error.size() > 0) {
    error = state->error;
  }

  delete state;
  return ok;
}

// Lightweight UDP client state for the update-info command (OUP CHECK/RESPONSE).
struct UpdateInfoUdpState {
  uv_loop_t loop;
  uv_udp_t handle;
  uv_timer_t timer;
  String responseBody;
  String error;
  uint64_t timeoutMs = 0;
  bool useTimer = false;
  bool done = false;
  bool success = false;
};

struct UpdateInfoUdpSendContext {
  uv_udp_send_t req;
  String data;
};

static void updateInfoUdpOnSendDone (uv_udp_send_t* req, int /*status*/) {
  auto* ctx = static_cast<UpdateInfoUdpSendContext*>(req->data);
  delete ctx;
}

static void updateInfoUdpStopTimer (UpdateInfoUdpState* state) {
  if (state == nullptr || !state->useTimer) return;
  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->timer))) {
    uv_timer_stop(&state->timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&state->timer), nullptr);
  }
}

static void updateInfoUdpOnTimeout (uv_timer_t* handle) {
  auto* state = static_cast<UpdateInfoUdpState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(handle)));
  if (state == nullptr || state->done) {
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(handle))) {
      uv_timer_stop(handle);
      uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    }
    return;
  }

  state->error = "UDP CHECK timed out after " + std::to_string(state->timeoutMs) + "ms";
  state->success = false;
  state->done = true;

  uv_udp_recv_stop(&state->handle);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->handle))) {
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
  }

  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(handle))) {
    uv_timer_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
  }
}

static void updateInfoUdpOnRecv (
  uv_udp_t* handle,
  ssize_t nread,
  const uv_buf_t* buf,
  const sockaddr* addr,
  unsigned /*flags*/
) {
  (void) addr;

  auto* state = static_cast<UpdateInfoUdpState*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(handle)));

  if (nread <= 0 || !buf || !buf->base) {
    if (buf && buf->base) {
      delete[] buf->base;
    }
    if (state != nullptr && !state->done) {
      state->error = "no UDP response received";
      state->success = false;
      state->done = true;
    }
    uv_udp_recv_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    updateInfoUdpStopTimer(state);
    return;
  }

  const unsigned char* data = reinterpret_cast<const unsigned char*>(buf->base);
  size_t size = static_cast<size_t>(nread);

  uint8_t version = 0;
  uint8_t msgType = 0;
  uint32_t length = 0;
  if (!decodeUpdateHeader(data, size, version, msgType, length)) {
    delete[] buf->base;
    if (state != nullptr) {
      state->error = "invalid UDP response header";
      state->success = false;
      state->done = true;
    }
    uv_udp_recv_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    updateInfoUdpStopTimer(state);
    return;
  }

  if (size < static_cast<size_t>(8 + length) || length > (1024u * 1024u)) {
    delete[] buf->base;
    if (state != nullptr) {
      state->error = "UDP response payload is truncated or too large";
      state->success = false;
      state->done = true;
    }
    uv_udp_recv_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    updateInfoUdpStopTimer(state);
    return;
  }

  String payload(
    reinterpret_cast<const char*>(data + 8),
    static_cast<size_t>(length)
  );
  delete[] buf->base;

  if (version != OUP_VERSION) {
    if (state != nullptr) {
      state->error = "unsupported protocol version";
      state->success = false;
      state->done = true;
    }
    uv_udp_recv_stop(handle);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    updateInfoUdpStopTimer(state);
    return;
  }

  if (state != nullptr) {
    if (msgType == OUP_MSG_RESPONSE) {
      state->responseBody = payload;
      state->success = true;
      state->done = true;
    } else if (msgType == OUP_MSG_ERROR) {
      state->error = payload;
      state->success = false;
      state->done = true;
    } else {
      state->error = "unexpected message type in UDP response";
      state->success = false;
      state->done = true;
    }
  }

  uv_udp_recv_stop(handle);
  uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
  updateInfoUdpStopTimer(state);
}

static bool runUpdateInfoUdp (
  const String& host,
  int port,
  const String& payload,
  String& responseBody,
  String& error,
  uint64_t timeoutMs
) {
  auto* state = new UpdateInfoUdpState();

  if (uv_loop_init(&state->loop) != 0) {
    error = "update-info: failed to initialize UDP event loop";
    delete state;
    return false;
  }

  if (uv_udp_init(&state->loop, &state->handle) != 0) {
    error = "update-info: failed to initialize UDP client handle";
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&state->handle), state);

  state->timeoutMs = timeoutMs;
  state->useTimer = timeoutMs > 0;
  if (state->useTimer) {
    if (uv_timer_init(&state->loop, &state->timer) != 0) {
      error = "update-info: failed to initialize UDP timeout timer";
      uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
      uv_loop_close(&state->loop);
      delete state;
      return false;
    }
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&state->timer), state);
    if (uv_timer_start(&state->timer, updateInfoUdpOnTimeout, timeoutMs, 0) != 0) {
      error = "update-info: failed to start UDP timeout timer";
      uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
      uv_close(reinterpret_cast<uv_handle_t*>(&state->timer), nullptr);
      uv_loop_close(&state->loop);
      delete state;
      return false;
    }
  }

  sockaddr_in addr;
  if (uv_ip4_addr(host.c_str(), port, &addr) != 0) {
    error = "update-info: UDP mode currently supports IPv4 addresses only";
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  const auto header = encodeUpdateHeader(OUP_MSG_CHECK, static_cast<uint32_t>(payload.size()));
  auto* ctx = new UpdateInfoUdpSendContext();
  ctx->data.reserve(header.size() + payload.size());
  ctx->data.append(reinterpret_cast<const char*>(header.data()), header.size());
  ctx->data.append(payload);

  uv_buf_t buf = uv_buf_init(
    ctx->data.data(),
    static_cast<unsigned int>(ctx->data.size())
  );
  ctx->req.data = ctx;

  if (uv_udp_recv_start(&state->handle, updateUdpAlloc, updateInfoUdpOnRecv) != 0) {
    error = "update-info: failed to start UDP receive";
    delete ctx;
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    uv_loop_close(&state->loop);
    delete state;
    return false;
  }

  if (uv_udp_send(
        &ctx->req,
        &state->handle,
        &buf,
        1,
        reinterpret_cast<const struct sockaddr*>(&addr),
        updateInfoUdpOnSendDone
      ) != 0) {
    error = "update-info: failed to send UDP CHECK message";
    uv_udp_recv_stop(&state->handle);
    uv_close(reinterpret_cast<uv_handle_t*>(&state->handle), nullptr);
    uv_loop_close(&state->loop);
    delete state;
    delete ctx;
    return false;
  }

  uv_run(&state->loop, UV_RUN_DEFAULT);
  uv_loop_close(&state->loop);

  const bool ok = state->success;
  responseBody = state->responseBody;
  if (!ok && state->error.size() > 0) {
    error = state->error;
  }

  delete state;
  return ok;
}

static void logWithLevel (CliLogLevel level, const String& s);
static inline void logInfo (const String& s);
static inline void logWarn (const String& s);
static inline void logError (const String& s);
static inline void logVerbose (const String& s);
static inline void logDebug (const String& s);

static inline String formatDuration (long long ms) {
  std::ostringstream ss;
  if (ms >= 60000) {
    auto minutes = ms / 60000;
    auto remaining = ms % 60000;
    auto seconds = remaining / 1000.0;
    ss << minutes << "m";
    if (seconds > 0) {
      ss << std::fixed << std::setprecision(seconds >= 10.0 ? 0 : 1) << seconds << "s";
    }
  } else if (ms >= 1000) {
    double seconds = static_cast<double>(ms) / 1000.0;
    ss << std::fixed << std::setprecision(seconds >= 10.0 ? 1 : 2) << seconds << "s";
  } else {
    ss << ms << "ms";
  }
  return ss.str();
}

static inline Vector<String> splitLines (const String& text) {
  Vector<String> lines;
  size_t startIndex = 0;
  while (startIndex <= text.size()) {
    auto pos = text.find('\n', startIndex);
    if (pos == String::npos) {
      lines.push_back(text.substr(startIndex));
      break;
    }
    lines.push_back(text.substr(startIndex, pos - startIndex));
    startIndex = pos + 1;
    if (startIndex == text.size()) {
      lines.emplace_back("");
      break;
    }
  }
  if (lines.empty()) {
    lines.emplace_back("");
  }
  return lines;
}

static inline bool isTruthyEnvFlag (const String& value) {
  if (value.empty()) {
    return false;
  }
  String lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

static inline String normalizePathForLog (const Path& path) {
  std::error_code ec;
  auto absolutePath = fs::absolute(path, ec);
  if (ec) {
    return path.lexically_normal().string();
  }
  return absolutePath.lexically_normal().string();
}

static inline bool dotfileExists (const Path& path) {
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

static inline void updateLogLevelFromEnv () {
  // Highest wins (DEBUG > VERBOSE > INFO). Quiet handled via flagQuietMode.
  const bool envDebug =
    env::get("ORO_DEBUG").size() > 0 ||
    env::get("DEBUG").size() > 0;
  const bool envVerbose =
    env::get("ORO_VERBOSE").size() > 0 ||
    env::get("VERBOSE").size() > 0;
  if (envDebug) gLogLevel = CliLogLevel::DEBUG;
  else if (envVerbose) gLogLevel = CliLogLevel::VERBOSE;
  else gLogLevel = CliLogLevel::INFO;
}

static inline void updateLogJsonFromEnv () {
  gLogJson = env::get("ORO_LOG_JSON").size() > 0;
}

static inline void updateLogFileFromEnv () {
  auto p = env::get("ORO_LOG_FILE");
  if (p.size() > 0) gLogFilePath = p;
}

static inline void ensureLogFileOpen () {
  if (gLogFilePath.size() == 0) return;
  if (gLogFile.is_open()) return;
  try {
    fs::path p = fs::path(gLogFilePath).make_preferred();
    auto dir = p.parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
      fs::create_directories(dir);
    }
    gLogFile.open(p, std::ios::out | std::ios::app);
  } catch (...) {
    // ignore file open errors to avoid recursive logging
  }
}

static inline bool useColor () {
#ifdef _WIN32
  if (
    env::get("ORO_LOG_NO_COLOR").size() > 0
  ) return false;
  static bool attempted = false;
  static bool enabled = false;
  if (!attempted) {
    attempted = true;
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD modeErr = 0, modeOut = 0;
    if (hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &modeErr)) {
      enabled |= SetConsoleMode(hErr, modeErr | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &modeOut)) {
      enabled |= SetConsoleMode(hOut, modeOut | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }
  return enabled;
#else
  static const bool noColor =
    env::get("ORO_LOG_NO_COLOR").size() > 0;
  if (noColor) return false;
  // enable colors only when attached to a TTY
  return isatty(2);
#endif
}

static inline const char* levelLabel (CliLogLevel level) {
  switch (level) {
    case CliLogLevel::Error: return "ERROR";
    case CliLogLevel::WARN: return "WARN";
    case CliLogLevel::INFO: return "INFO";
    case CliLogLevel::VERBOSE: return "VERBOSE";
    case CliLogLevel::DEBUG: return "DEBUG";
    default: return "";
  }
}

static inline bool levelEnabled (CliLogLevel msgLevel) {
  if (gLogLevel == CliLogLevel::NONE) return false;
  // Quiet mode reduces output to WARN and ERROR only (still shows severe issues)
  const int effectiveLevel = flagQuietMode
    ? static_cast<int>(CliLogLevel::WARN)
    : static_cast<int>(gLogLevel);
  return static_cast<int>(msgLevel) <= effectiveLevel;
}

static void logWithLevel (CliLogLevel level, const String& s) {
  if (!levelEnabled(level)) return;

  const auto nowMono = steady_clock::now();
  const auto nowSystem = system_clock::now();
  const auto deltaMs = duration_cast<milliseconds>(nowMono - gLogLastMono).count();
  const auto elapsedMs = duration_cast<milliseconds>(nowMono - gLogStartMono).count();
  gLogLastMono = nowMono;

  const bool color = useColor();
  const char* label = levelLabel(level);
  const char* resetColor = color ? "\033[0m" : "";
  const char* dimColor = color ? "\033[2m" : "";
  const char* accentColor = color ? "\033[36m" : "";
  const char* timeColor = color ? "\033[90m" : "";
  const char* levelColor = "";
  if (color) {
    switch (level) {
      case CliLogLevel::Error: levelColor = "\033[31m"; break;
      case CliLogLevel::WARN: levelColor = "\033[33m"; break;
      case CliLogLevel::INFO: levelColor = "\033[36m"; break;
      case CliLogLevel::VERBOSE: levelColor = "\033[2m"; break;
      case CliLogLevel::DEBUG: levelColor = "\033[35m"; break;
      default: levelColor = ""; break;
    }
  }

  std::ostream& os = (level == CliLogLevel::Error || level == CliLogLevel::WARN) ? std::cerr : std::cout;

  // JSON logging mode or log-file mirroring: build JSON entry
  const bool needJson = gLogJson || gLogFilePath.size() > 0;
  if (needJson) {
    auto tsMs = duration_cast<milliseconds>(nowSystem.time_since_epoch()).count();
    auto jsonEscape = [](const String& in) {
      String out;
      out.reserve(in.size() + 8);
      for (unsigned char c : in) {
        switch (c) {
          case '"': out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break;
          case '\t': out += "\\t"; break;
          default:
            if (c < 0x20) {
              char buf[7];
              std::snprintf(buf, sizeof(buf), "\\u%04x", c);
              out += buf;
            } else {
              out.push_back(static_cast<char>(c));
            }
        }
      }
      return out;
    };

    auto toLowerCopy = [](String v) {
      std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch){ return std::tolower(ch); });
      return v;
    };

    String lvl = toLowerCopy(String(label));
    // ISO-8601 UTC timestamp with milliseconds
    char timebuf[64] = {0};
    std::time_t sec = static_cast<std::time_t>(tsMs / 1000);
    std::tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &sec);
#else
    gmtime_r(&sec, &tm);
#endif
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &tm);
    int msComp = static_cast<int>(tsMs % 1000);
    char iso[80] = {0};
    std::snprintf(iso, sizeof(iso), "%s.%03dZ", timebuf, msComp);

    // Build argv JSON array
    String argvJson = "[";
    for (size_t i = 0; i < gLogArgv.size(); i++) {
      if (i > 0) argvJson += ",";
      argvJson += String("\"") + jsonEscape(gLogArgv[i]) + String("\"");
    }
    argvJson += "]";

    // pid
    long pid = 0;
#if defined(_WIN32)
    pid = static_cast<long>(GetCurrentProcessId());
#else
    pid = static_cast<long>(getpid());
#endif

  // cwd (may be unavailable)
  String cwd;
  try {
    cwd = fs::current_path().string();
  } catch (...) {
    cwd = "";
  }

    // Compose JSON line
    std::ostringstream js;
    js << "{\"level\":\"" << lvl
       << "\",\"msg\":\"" << jsonEscape(s)
       << "\",\"delta_ms\":" << deltaMs
       << ",\"elapsed_ms\":" << elapsedMs
       << ",\"ts\":" << tsMs
       << ",\"time\":\"" << iso << "\""
       << ",\"pid\":" << pid
       << ",\"subcommand\":\"" << jsonEscape(gLogSubcommand) << "\""
       << ",\"argv\":" << argvJson
       << ",\"cwd\":\"" << jsonEscape(cwd) << "\""
       << "}";

    const String line = js.str();

    if (gLogJson) {
      // Always stdout for JSON logs
      std::cout << line << std::endl;
    }

    if (gLogFilePath.size() > 0) {
      std::lock_guard<std::mutex> lock(gLogFileMutex);
      ensureLogFileOpen();
      if (gLogFile.is_open()) {
        gLogFile << line << std::endl;
      }
    }

    if (gLogJson) {
      return;
    }
  }

  char timeOnly[32] = {0};
  char timestamp[48] = {0};
  std::time_t sec = system_clock::to_time_t(nowSystem);
  std::tm tm;
#if defined(_WIN32)
  localtime_s(&tm, &sec);
#else
  localtime_r(&sec, &tm);
#endif
  std::strftime(timeOnly, sizeof(timeOnly), "%H:%M:%S", &tm);
  auto msComponent = duration_cast<milliseconds>(nowSystem.time_since_epoch()).count() % 1000;
  std::snprintf(timestamp, sizeof(timestamp), "%s.%03lld", timeOnly, static_cast<long long>(msComponent));

  String labelPadded = label ? String(label) : String("");
  if (labelPadded.size() < 7) {
    labelPadded.append(7 - labelPadded.size(), ' ');
  }

  String prefixPlain = String(timestamp) + " [" + labelPadded + "]";
  if (gLogSubcommand.size() > 0) {
    prefixPlain += " (" + gLogSubcommand + ")";
  }
  prefixPlain += " ";

  String prefixColored;
  prefixColored.reserve(prefixPlain.size() + 16);
  if (color) {
    prefixColored += timeColor;
  }
  prefixColored += timestamp;
  if (color) {
    prefixColored += resetColor;
  }
  prefixColored += " [";
  if (color && levelColor[0] != '\0') {
    prefixColored += levelColor;
  }
  prefixColored += labelPadded;
  if (color && levelColor[0] != '\0') {
    prefixColored += resetColor;
  }
  prefixColored += "]";
  if (gLogSubcommand.size() > 0) {
    prefixColored += " (";
    if (color) {
      prefixColored += accentColor;
    }
    prefixColored += gLogSubcommand;
    if (color) {
      prefixColored += resetColor;
    }
    prefixColored += ")";
  }
  prefixColored += " ";

  String metrics = " +" + std::to_string(deltaMs) + "ms (" + formatDuration(elapsedMs) + " total)";
  if (color) {
    metrics = String(dimColor) + metrics + resetColor;
  }

  auto lines = splitLines(s);
  const String continuationPrefix = String(prefixPlain.size(), ' ') + "> ";

  if (!lines.empty()) {
    os << prefixColored;
    if (lines.front().size() > 0) {
      os << lines.front();
      os << metrics;
    } else {
      os << metrics;
    }
    os << std::endl;
  }

  for (size_t i = 1; i < lines.size(); i++) {
    os << continuationPrefix << lines[i] << std::endl;
  }
}
Map<> defaultTemplateAttrs;

#if ORO_RUNTIME_PLATFORM_APPLE
Atomic<bool> checkLogStore = true;
#endif

void log (const String s) {
  if (s.size() == 0) return;
  // Infer level from conventional prefixes to avoid changing all call sites
  if (s.starts_with("ERROR:") || s.starts_with("Error:") || s.starts_with("error:")) {
    return logWithLevel(CliLogLevel::Error, s);
  }
  if (s.starts_with("WARNING:") || s.starts_with("Warn:") || s.starts_with("warn:")) {
    return logWithLevel(CliLogLevel::WARN, s);
  }
  return logWithLevel(CliLogLevel::INFO, s);
}

static inline void logInfo (const String& s) { logWithLevel(CliLogLevel::INFO, s); }
static inline void logWarn (const String& s) { logWithLevel(CliLogLevel::WARN, s); }
static inline void logError (const String& s) { logWithLevel(CliLogLevel::Error, s); }
static inline void logVerbose (const String& s) { logWithLevel(CliLogLevel::VERBOSE, s); }
static inline void logDebug (const String& s) { logWithLevel(CliLogLevel::DEBUG, s); }

static inline void logCapturedCommandOutput (const String& output) {
  const auto trimmedOutput = trim(output);
  if (trimmedOutput.empty()) {
    return;
  }

  if (levelEnabled(CliLogLevel::VERBOSE)) {
    logVerbose(trimmedOutput);
  }
}

static inline String compilerOutputHint () {
  if (levelEnabled(CliLogLevel::VERBOSE)) {
    return " Review compiler output above for details.";
  }

  return " Re-run with '-V' or set ORO_VERBOSE=1 to view compiler output.";
}

inline String helpHint (const String& subcommand) {
  const String cliName(gCliDisplayName);
  String display = subcommand;
  if (display.starts_with("update-")) {
    display = String("update ") + display.substr(7, display.size() - 7);
  }
  if (subcommand.size() == 0) {
    return String(" See '") + cliName + String(" --help' (common errors)");
  }
  return String(" See '") + cliName + String(" ") + display + String(" --help' (common errors)");
}

inline bool isExecAllowed (const Map<>& settings) {
  const auto allowEnv = env::get("ORO_ALLOW_EXEC");
  if (allowEnv == "1" || allowEnv == "true") return true;
  if (settings.contains("allow_exec")) {
    const auto& v = settings.at("allow_exec");
    return v == "true" || v == "1";
  }
  return false;
}

inline bool ensureExecAllowed (const Map<>& settings, const String& subcommand) {
  if (isExecAllowed(settings)) return true;
  logError("external command execution is disabled. Set ORO_ALLOW_EXEC=1 or [build] allow_exec=true." + helpHint(subcommand));
  return false;
}

// Detect a host IPv4 address suitable for mobile targets (Android/iOS simulators/devices)
// Tries platform-appropriate commands with fallbacks.
inline String detectDevHostIP () {
  // Windows: PowerShell
  if (platform.win) {
    auto r = exec("PowerShell -Command ((Get-NetIPAddress -AddressFamily IPV4).IPAddress ^| Select-Object -first 1)");
    if (r.exitCode == 0 && trim(r.output).size() > 0) return trim(r.output);
    return String("localhost");
  }

  // macOS: prefer ipconfig on primary interfaces
  if (platform.mac) {
    auto r = exec("ipconfig getifaddr en0 || ipconfig getifaddr en1 || ipconfig getifaddr lo0");
    if (r.exitCode == 0 && trim(r.output).size() > 0) return trim(r.output);
  }

  // Linux/Unix: try ip route, fallback to ifconfig/awk
  {
    auto r = exec("ip route get 1.1.1.1 2>/dev/null | awk '{print $7; exit}'");
    if (r.exitCode == 0 && trim(r.output).size() > 0) return trim(r.output);
  }
  {
    auto r = exec("ip -4 addr show scope global 2>/dev/null | grep -oE 'inet [0-9\\.]+' | awk '{print $2}' | head -n1");
    if (r.exitCode == 0 && trim(r.output).size() > 0) return trim(r.output);
  }
  {
    auto r = exec("ifconfig | grep -w 'inet' | awk '!match($2, \"^127.\") {print $2; exit}' | tr -d '\n'");
    if (r.exitCode == 0 && trim(r.output).size() > 0) return trim(r.output);
  }

  return String("localhost");
}

inline String gradleJdkHint () {
  if (platform.mac) {
    return String(" Try 'brew install gradle openjdk'.");
  } else if (platform.win) {
    return String(" Try 'choco install gradle openjdk'.");
  } else if (platform.linux) {
    return String(" Install a JDK and Gradle (e.g., 'sudo apt-get install default-jdk gradle').");
  }
  return String("");
}

inline int runSystemOrFail (const String& command, const String& onError, const String& subcommandHint = "") {
  int rc = std::system(command.c_str());
  if (rc != 0) {
    logError(onError + (subcommandHint.size() > 0 ? helpHint(subcommandHint) : String("")));
    return rc;
  }
  return 0;
}

inline bool isSafeGpgKeyId (const String& keyId) {
  for (const auto ch : keyId) {
    const bool isAlphaNum =
      (ch >= '0' && ch <= '9') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= 'a' && ch <= 'z');
    const bool isAllowedPunct =
      ch == '@' ||
      ch == '.' ||
      ch == '_' ||
      ch == '-';
    if (!(isAlphaNum || isAllowedPunct)) {
      return false;
    }
  }
  return true;
}

inline String shellQuote (const String& value) {
  String quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (const auto ch : value) {
    if (ch == '\'') {
      quoted.append("'\\''");
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

inline void signArtifactWithGpg (const Path& artifact, const String& keyId, bool debugEnv, bool verboseEnv) {
  const auto artifactPath = artifact.string();

  StringStream command;
  command
    << "gpg"
    << " --batch"
    << " --yes"
    << " --armor"
    << " --detach-sign";

  if (keyId.size() > 0) {
    if (!isSafeGpgKeyId(keyId)) {
      logError(
        "invalid GPG key id for signing; only [A-Za-z0-9@._-] are allowed in '--sign-key' or 'build_sign_key'."
      );
      exit(1);
    }
    command << " -u " << shellQuote(keyId);
  }

  command
    << " -o \"" << artifactPath << ".asc\""
    << " \"" << artifactPath << "\"";

  const auto cmdString = command.str();

  if (debugEnv || verboseEnv) {
    logVerbose(cmdString);
  }

  auto result = exec(cmdString.c_str());
  if (result.exitCode != 0) {
    logError("GPG signing failed for '" + artifactPath + "'. Ensure 'gpg' is installed and the signing key is available.");
    if (debugEnv) {
      logDebug(result.output);
    }
    exit(result.exitCode);
  }
}

template <typename K, typename V>
inline Map<K, V>& extendMap (Map<K, V>& dst, const Map<K, V>& src) {
  for (const auto& tuple : src) {
    dst[tuple.first] = tuple.second;
  }
  return dst;
}

inline String readFile (fs::path path) {
  if (fs::is_directory(path)) {
  logWarn("trying to read a directory as a file: " + path.string());
    return "";
  }

  std::ifstream stream(path.c_str());
  String content;
  auto buffer = std::istreambuf_iterator<char>(stream);
  auto end = std::istreambuf_iterator<char>();
  content.assign(buffer, end);
  stream.close();
  return content;
}

inline bool isSupportedSourceExtension (const String& extension) {
  static const std::unordered_set<String> exts = {
    ".html",
    ".htm",
    ".js",
    ".mjs"
  };

  return exts.find(extension) != exts.end();
}

inline String escapeIniValue (const String& value) {
  String escaped;
  escaped.reserve(value.size());

  for (const auto ch : value) {
    if (ch == '"') {
      escaped += "\\\"";
    } else {
      escaped += ch;
    }
  }

  return escaped;
}

inline String escapeTomlString (const String& value) {
  String escaped;
  escaped.reserve(value.size());

  for (const auto ch : value) {
    switch (ch) {
      case '\\': escaped.append("\\\\"); break;
      case '"':  escaped.append("\\\""); break;
      case '\n': escaped.append("\\n"); break;
      case '\r': escaped.append("\\r"); break;
      case '\t': escaped.append("\\t"); break;
      default:   escaped.push_back(ch); break;
    }
  }

  return escaped;
}

inline String quoteTomlString (const String& value) {
  return "\"" + escapeTomlString(value) + "\"";
}

inline String quoteIniString (const String& value) {
  return "\"" + escapeIniValue(value) + "\"";
}

inline bool isValidTomlBareKey (const String& key) {
  if (key.size() == 0) {
    return false;
  }
  for (const auto ch : key) {
    const bool isAlphaNum =
      (ch >= '0' && ch <= '9') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= 'a' && ch <= 'z');
    if (!(isAlphaNum || ch == '_' || ch == '-')) {
      return false;
    }
  }
  return true;
}

inline String mergeEnvironmentAssignments (
  const String& source,
  const UserConfigFormat format,
  const Vector<std::pair<String, String>>& assignments
) {
  if (assignments.empty()) {
    return source;
  }

  Map<> overrides;
  for (const auto& assignment : assignments) {
    overrides[assignment.first] = assignment.second;
  }

  auto writeOverrides = [&] (StringStream& output) {
    for (const auto& assignment : overrides) {
      const auto key = format == UserConfigFormat::Toml &&
          !isValidTomlBareKey(assignment.first)
        ? quoteTomlString(assignment.first)
        : assignment.first;
      const auto value = format == UserConfigFormat::Toml
        ? quoteTomlString(assignment.second)
        : quoteIniString(assignment.second);
      output << key << " = " << value << "\n";
    }
  };

  auto assignmentKey = [&] (const String& line) {
    const auto separator = line.find('=');
    if (separator == String::npos) {
      return String("");
    }

    auto key = trim(line.substr(0, separator));
    if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
      key = key.substr(1, key.size() - 2);
    }
    return key;
  };

  StringStream input(source);
  StringStream output;
  String line;
  bool foundEnvironmentTable = false;
  bool inEnvironmentTable = false;
  bool wroteOverrides = false;

  while (std::getline(input, line)) {
    const auto cleanLine = trim(line);
    const bool isTable = cleanLine.size() >= 2 &&
      cleanLine.front() == '[' && cleanLine.back() == ']';

    if (isTable) {
      if (inEnvironmentTable && !wroteOverrides) {
        writeOverrides(output);
        wroteOverrides = true;
      }

      inEnvironmentTable = cleanLine == "[env]";
      if (inEnvironmentTable) {
        foundEnvironmentTable = true;
      }
      output << line << "\n";
      continue;
    }

    if (inEnvironmentTable && overrides.count(assignmentKey(line)) > 0) {
      continue;
    }

    output << line << "\n";
  }

  if (inEnvironmentTable && !wroteOverrides) {
    writeOverrides(output);
  }

  if (!foundEnvironmentTable) {
    output << "\n[env]\n";
    writeOverrides(output);
  }

  return output.str();
}

inline UserConfigFormat detectConfigFormatForPath (const Path& path) {
  auto extension = toLowerCase(path.extension().string());
  if (extension == ".toml") {
    return UserConfigFormat::Toml;
  }
  return UserConfigFormat::Ini;
}

inline bool fileExists (const Path& path) {
  std::error_code ec;
  return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

inline String joinIniArguments (const Vector<String>& args) {
  if (args.empty()) {
    return "";
  }

  StringStream stream;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << "'" << args[i] << "'";
  }
  return stream.str();
}

inline String prettyJson (const String& input) {
  StringStream out;
  int indent = 0;
  bool inString = false;
  bool escape = false;
  const String indentUnit = "  ";

  for (size_t i = 0; i < input.size(); ++i) {
    const char ch = input[i];

    if (inString) {
      out << ch;
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        inString = false;
      }
      continue;
    }

    switch (ch) {
      case '{':
      case '[':
        out << ch;
        out << "\n";
        indent++;
        for (int j = 0; j < indent; ++j) {
          out << indentUnit;
        }
        break;
      case '}':
      case ']':
        out << "\n";
        if (indent > 0) {
          indent--;
        }
        for (int j = 0; j < indent; ++j) {
          out << indentUnit;
        }
        out << ch;
        break;
      case ',':
        out << ch;
        out << "\n";
        for (int j = 0; j < indent; ++j) {
          out << indentUnit;
        }
        break;
      case ':':
        out << ": ";
        break;
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        // skip insignificant whitespace outside strings
        break;
      case '"':
        out << ch;
        inString = true;
        escape = false;
        break;
      default:
        out << ch;
        break;
    }
  }

  return out.str();
}

inline void replaceArraySyntax (String& line, const String& key) {
  const auto token = key + "[]";
  const auto position = line.find(token);
  if (position == String::npos) {
    return;
  }
  const auto equals = line.find('=', position);
  if (equals == String::npos) {
    return;
  }
  auto value = trim(line.substr(equals + 1));
  line = line.substr(0, position) + key + " = [" + value + "]";
}

inline String convertIniTemplateToToml (const String& source) {
  StringStream output;
  std::istringstream input(source);
  String line;

  while (std::getline(input, line)) {
    auto firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != String::npos && line[firstNonSpace] == ';') {
      line[firstNonSpace] = '#';
    }

    replaceArraySyntax(line, "sources");
    replaceArraySyntax(line, "headers");
    replaceArraySyntax(line, "allowed");

    output << line;
    if (!input.eof()) {
      output << "\n";
    }
  }

  return output.str();
}

inline String sanitizeBuildName (const String& value) {
  String sanitized;
  sanitized.reserve(value.size());

  for (const auto ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      sanitized += std::tolower(static_cast<unsigned char>(ch));
    } else if (ch == '-' || ch == '_') {
      sanitized += ch;
    } else {
      sanitized += '-';
    }
  }

  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }

  if (sanitized.empty()) {
    sanitized = "app";
  }

  if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
    sanitized = "app-" + sanitized;
  }

  return sanitized;
}

inline String sanitizeBundleIdentifier (const String& name) {
  String sanitized;
  sanitized.reserve(name.size());

  for (const auto ch : name) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      sanitized += std::tolower(static_cast<unsigned char>(ch));
    } else if (ch == '-' || ch == '_') {
      sanitized += '-';
    }
  }

  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }

  if (sanitized.empty()) {
    sanitized = "app";
  }

  if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
    sanitized = "app-" + sanitized;
  }

  return "com.inferred." + sanitized;
}

inline String pathToPosix (const fs::path& value) {
  return value.generic_string();
}

inline String ensureRelativeWebPath (const String& path) {
  if (path.starts_with("./") || path.starts_with("../")) {
    return path;
  }
  return String("./") + path;
}

inline void assignTargetFromArgument (const String& candidate, bool allowMissing) {
  if (candidate.size() == 0) {
    return;
  }

  auto absolute = fs::absolute(candidate).lexically_normal();
  targetArgumentPath = absolute;
  targetSourcePath.clear();

  std::error_code ec;

  if (!fs::exists(absolute, ec)) {
    if (allowMissing) {
      targetPath = absolute;
      return;
    }

    logError("Path not found: " + absolute.string());
    exit(1);
  }

  if (fs::is_regular_file(absolute, ec)) {
    targetSourcePath = absolute;
    targetPath = absolute.parent_path();
    if (targetPath.empty()) {
      targetPath = absolute.parent_path();
    }
    return;
  }

  if (fs::is_directory(absolute, ec)) {
    targetPath = absolute;
    return;
  }

  auto extension = toLowerCase(absolute.extension().string());

  if (isSupportedSourceExtension(extension)) {
    targetSourcePath = absolute;
    targetPath = absolute.parent_path();
  } else {
    logError("Unsupported path type: " + absolute.string());
    exit(1);
  }
}

inline String inferHtmlScaffold (
  const String& title,
  const String& scriptPath,
  const bool isModule,
  const String& cssPath
) {
  StringStream html;
  html << "<!doctype html>\n";
  html << "<html lang=\"en\">\n";
  html << "<head>\n";
  html << "  <meta charset=\"utf-8\">\n";
  html << "  <title>" << title << "</title>\n";
  if (cssPath.size() > 0) {
    html << "  <link rel=\"stylesheet\" href=\"" << cssPath << "\">\n";
  }
  html << "</head>\n";
  html << "<body>\n";
  html << "  <script";
  if (isModule) {
    html << " type=\"module\"";
  }
  html << " src=\"" << scriptPath << "\"></script>\n";
  html << "</body>\n";
  html << "</html>\n";
  return html.str();
}

struct InferredConfigResult {
  bool success;
  String ini;
  Map<> overrides;
  String entryDisplay;
};

inline InferredConfigResult inferConfigFromPath (const Path& root, const Path& source) {
  InferredConfigResult result;
  result.success = false;

  auto projectRoot = root;
  if (projectRoot.empty()) {
    projectRoot = fs::current_path();
  }

  auto base = source.empty()
    ? projectRoot.filename().string()
    : source.stem().string();

  if (base.size() == 0) {
    base = "app";
  }

  const auto buildName = sanitizeBuildName(base);
  const auto bundleIdentifier = sanitizeBundleIdentifier(buildName);
  const auto title = base;

  String copyValue = ".";
  String defaultIndex;
  Map<> overrides;
  String entryDisplay;

  auto resolveRelative = [&projectRoot](const Path& target) -> String {
    auto relative = fs::relative(target, projectRoot);
    return pathToPosix(relative);
  };

  auto chooseFirstMatch = [&](const std::vector<String>& extensions) -> std::optional<Path> {
    for (const auto& ext : extensions) {
      auto candidate = projectRoot / ("index" + ext);
      std::error_code ec;
      if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
        return candidate;
      }
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(projectRoot, ec), end;

    for (; it != end && !ec; ++it) {
      if (it->is_directory()) {
        continue;
      }

      auto extension = toLowerCase(it->path().extension().string());
      if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end()) {
        return it->path();
      }
    }

    if (ec) {
      logWarn("Unable to fully traverse directory while inferring config: " + ec.message());
    }

    return std::nullopt;
  };

  auto buildIni = [&](
    const String& inferredCopy,
    const String& inferredIndex,
    const StringStream& extraSections
  ) -> String {
    StringStream ini;
    ini << "[build]\n";
    ini << "name = \"" << escapeIniValue(buildName) << "\"\n";
    ini << "copy = \"" << escapeIniValue(inferredCopy) << "\"\n";
    ini << "output = \"build\"\n\n";
    ini << "[meta]\n";
    ini << "bundle_identifier = \"" << escapeIniValue(bundleIdentifier) << "\"\n";
    ini << "version = \"1.0.0\"\n";
    ini << "title = \"" << escapeIniValue(title) << "\"\n\n";
    ini << "[webview]\n";
    ini << "default_index = \"" << escapeIniValue(inferredIndex) << "\"\n";
    ini << extraSections.str();
    return ini.str();
  };

  if (!source.empty()) {
    auto extension = toLowerCase(source.extension().string());

    if (!isSupportedSourceExtension(extension)) {
      logError("Unsupported source file extension: " + source.extension().string());
      return result;
    }

    std::error_code ec;
    auto relative = fs::relative(source, projectRoot, ec);
    if (ec) {
      logError("Unable to resolve entry relative to project root: " + ec.message());
      return result;
    }

    for (const auto& part : relative) {
      if (part == "..") {
        logError("Entry file must reside within the project directory");
        return result;
      }
    }

    if (relative.empty()) {
      relative = source.filename();
    }

    copyValue = pathToPosix(relative);

    if (extension == ".html" || extension == ".htm") {
      defaultIndex = String("/") + copyValue;
      entryDisplay = copyValue;
    } else {
      const bool isModule = (extension == ".mjs" || extension == ".js");
      auto scriptPath = ensureRelativeWebPath(copyValue);
      auto html = inferHtmlScaffold(title, scriptPath, isModule, "");
      defaultIndex = "/index.html";
      overrides["oroc_inferred_entry_path"] = "index.html";
      overrides["oroc_inferred_entry_content"] = html;
      entryDisplay = overrides["oroc_inferred_entry_path"];
    }

    StringStream extra;
    result.ini = buildIni(copyValue, defaultIndex, extra);
    result.success = true;
    result.overrides = overrides;
    result.entryDisplay = entryDisplay;
    return result;
  }

  // Implicit quick-build case: no 'src/' directory in the project root.
  // When present, prefer index.html/index.js/index.css in the root and
  // ignore other files.
  {
    std::error_code srcEc;
    auto srcDir = projectRoot / "src";
    const bool hasSrcDir = fs::exists(srcDir, srcEc) && fs::is_directory(srcDir, srcEc);

    if (!hasSrcDir) {
      auto indexHtmlPath = projectRoot / "index.html";
      auto indexJsPath = projectRoot / "index.js";
      auto indexCssPath = projectRoot / "index.css";

      std::error_code ecHtml;
      std::error_code ecJs;
      std::error_code ecCss;

      const bool hasIndexHtml = fs::exists(indexHtmlPath, ecHtml) && fs::is_regular_file(indexHtmlPath, ecHtml);
      const bool hasIndexJs = fs::exists(indexJsPath, ecJs) && fs::is_regular_file(indexJsPath, ecJs);
      const bool hasIndexCss = fs::exists(indexCssPath, ecCss) && fs::is_regular_file(indexCssPath, ecCss);

      if (!hasIndexHtml && !hasIndexJs) {
        logError("Unable to infer entry point – expected 'index.html' or 'index.js' in '" + projectRoot.string() + "'.");
        return result;
      }

      StringStream extra;

      // Build a minimal [build] copy mapping that only includes the
      // index.* files present in the project root.
      Vector<String> copyEntries;
      if (hasIndexHtml) {
        copyEntries.push_back("index.html=index.html");
      }
      if (hasIndexJs) {
        copyEntries.push_back("index.js=index.js");
      }
      if (hasIndexCss) {
        copyEntries.push_back("index.css=index.css");
      }

      copyValue = join(copyEntries, ";");

      if (hasIndexHtml) {
        // Real HTML entry point in the project root.
        defaultIndex = "/index.html";
        entryDisplay = "index.html";
      } else {
        // No index.html – synthesize one that loads index.js (as a module)
        // and optionally index.css when present.
        const bool isModule = true;
        auto scriptPath = ensureRelativeWebPath("index.js");
        String cssHref;
        if (hasIndexCss) {
          cssHref = ensureRelativeWebPath("index.css");
        }
        const auto htmlContent = inferHtmlScaffold(title, scriptPath, isModule, cssHref);

        defaultIndex = "/index.html";
        overrides["oroc_inferred_entry_path"] = "index.html";
        overrides["oroc_inferred_entry_content"] = htmlContent;
        overrides["oroc_inferred_entry_source"] = "index.js";
        entryDisplay = overrides["oroc_inferred_entry_path"];
      }

      result.ini = buildIni(copyValue, defaultIndex, extra);
      result.success = true;
      result.overrides = overrides;
      result.entryDisplay = entryDisplay;
      return result;
    }
  }

  auto html = chooseFirstMatch({ ".html", ".htm" });

  if (html.has_value()) {
    auto relativeHtml = resolveRelative(html.value());
    fs::path relativeHtmlPath(relativeHtml);
    auto inferredRoot = relativeHtmlPath.parent_path();

    if (!inferredRoot.empty() && inferredRoot != ".") {
      copyValue = pathToPosix(inferredRoot);
    }

    defaultIndex = String("/") + relativeHtml;
    entryDisplay = relativeHtml;
  } else {
    auto script = chooseFirstMatch({ ".js", ".mjs" });

    if (!script.has_value()) {
      logError("Unable to infer entry point – expected an HTML or JavaScript file inside '" + projectRoot.string() + "'.");
      return result;
    }

    auto relativeScript = resolveRelative(script.value());
    fs::path relativeScriptPath(relativeScript);
    auto inferredRoot = relativeScriptPath.parent_path();

    if (!inferredRoot.empty() && inferredRoot != ".") {
      copyValue = pathToPosix(inferredRoot);
    }

    // Look for a sibling CSS file (same basename, .css) next to the script.
    String cssRelative;
    {
      auto cssCandidate = script->parent_path() / (script->stem().string() + ".css");
      std::error_code cssEc;
      if (fs::exists(cssCandidate, cssEc) && fs::is_regular_file(cssCandidate, cssEc)) {
        cssRelative = resolveRelative(cssCandidate);
      }
    }

    const auto extension = toLowerCase(script->extension().string());
    const bool isModule = (extension == ".mjs" || extension == ".js");

    auto scriptPath = ensureRelativeWebPath(relativeScript);
    String cssHref;
    if (cssRelative.size() > 0) {
      cssHref = ensureRelativeWebPath(cssRelative);
    }

    auto htmlContent = inferHtmlScaffold(title, scriptPath, isModule, cssHref);

    overrides["oroc_inferred_entry_path"] = "index.html";
    overrides["oroc_inferred_entry_content"] = htmlContent;
    overrides["oroc_inferred_entry_source"] = relativeScript;

    defaultIndex = "/index.html";
    entryDisplay = overrides["oroc_inferred_entry_path"];
  }

  StringStream extra;
  result.ini = buildIni(copyValue, defaultIndex, extra);
  result.success = true;
  result.overrides = overrides;
  result.entryDisplay = entryDisplay;
  return result;
}

inline void writeFile (fs::path path, String s) {
  std::ofstream stream(path.string());
  stream << s;
  stream.close();
}

inline void appendFile (fs::path path, String s) {
  std::ofstream stream;
  stream.open(path.string(), std::ios_base::app);
  stream << s;
  stream.close();
}

inline bool updateVersionInTomlConfig (const fs::path& path, const String& newVersion) {
  auto content = readFile(path);
  if (content.size() == 0) {
    logError("failed to read configuration file for version update: " + path.string());
    return false;
  }

  Vector<String> lines;
  {
    String current;
    for (char c : content) {
      if (c == '\n') {
        lines.push_back(current);
        current.clear();
      } else if (c != '\r') {
        current.push_back(c);
      }
    }
    if (!current.empty()) {
      lines.push_back(current);
    }
  }

  bool sawMeta = false;
  bool updated = false;
  int metaLineIndex = -1;

  auto isSectionHeader = [](const String& line) -> bool {
    auto trimmed = trim(line);
    return trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']';
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    auto& line = lines[i];
    auto trimmed = trim(line);

    if (trimmed == "[meta]") {
      sawMeta = true;
      metaLineIndex = static_cast<int>(i);
      continue;
    }

    if (!sawMeta) {
      continue;
    }

    if (isSectionHeader(line)) {
      if (!updated) {
        String indent;
        for (char c : lines[static_cast<size_t>(metaLineIndex)]) {
          if (c == ' ' || c == '\t') {
            indent.push_back(c);
          } else {
            break;
          }
        }
        String versionLine = indent + "version = \"" + newVersion + "\"";
        lines.insert(lines.begin() + static_cast<long>(i), versionLine);
        updated = true;
      }
      break;
    }

    if (trimmed.size() == 0 || trimmed[0] == '#') {
      continue;
    }

    auto hashPos = line.find('#');
    String beforeComment = hashPos == String::npos ? line : line.substr(0, hashPos);

    size_t pos = 0;
    while (pos < beforeComment.size() && std::isspace(static_cast<unsigned char>(beforeComment[pos]))) {
      pos++;
    }

    const String key = "version";
    if (beforeComment.compare(pos, key.size(), key) != 0) {
      continue;
    }

    size_t afterKey = pos + key.size();
    if (afterKey < beforeComment.size() &&
        !std::isspace(static_cast<unsigned char>(beforeComment[afterKey])) &&
        beforeComment[afterKey] != '=') {
      continue;
    }

    String indent(beforeComment.begin(), beforeComment.begin() + static_cast<long>(pos));
    String newLine = indent + "version = \"" + newVersion + "\"";

    if (hashPos != String::npos) {
      newLine += line.substr(hashPos);
    }

    line = newLine;
    updated = true;
    break;
  }

  if (sawMeta && !updated) {
    String indent;
    String versionLine = indent + "version = \"" + newVersion + "\"";
    auto insertPos = metaLineIndex + 1;
    lines.insert(lines.begin() + insertPos, versionLine);
    updated = true;
  }

  if (!sawMeta) {
    lines.push_back("");
    lines.push_back("[meta]");
    lines.push_back("version = \"" + newVersion + "\"");
    updated = true;
  }

  if (!updated) {
    logError("failed to update version in configuration file '" + path.string() + "'");
    return false;
  }

  StringStream out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size()) {
      out << "\n";
    }
  }

  writeFile(path, out.str());
  return true;
}

inline bool updateVersionInIniConfig (const fs::path& path, const String& newVersion) {
  auto content = readFile(path);
  if (content.size() == 0) {
    logError("failed to read configuration file for version update: " + path.string());
    return false;
  }

  Vector<String> lines;
  {
    String current;
    for (char c : content) {
      if (c == '\n') {
        lines.push_back(current);
        current.clear();
      } else if (c != '\r') {
        current.push_back(c);
      }
    }
    if (!current.empty()) {
      lines.push_back(current);
    }
  }

  bool sawMeta = false;
  bool updated = false;
  int metaLineIndex = -1;

  auto isSectionHeader = [](const String& line) -> bool {
    auto trimmed = trim(line);
    return trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']';
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    auto& line = lines[i];
    auto trimmed = trim(line);

    if (trimmed == "[meta]") {
      sawMeta = true;
      metaLineIndex = static_cast<int>(i);
      continue;
    }

    if (!sawMeta) {
      continue;
    }

    if (isSectionHeader(line)) {
      if (!updated) {
        String indent;
        for (char c : lines[static_cast<size_t>(metaLineIndex)]) {
          if (c == ' ' || c == '\t') {
            indent.push_back(c);
          } else {
            break;
          }
        }
        String versionLine = indent + "version = \"" + newVersion + "\"";
        lines.insert(lines.begin() + static_cast<long>(i), versionLine);
        updated = true;
      }
      break;
    }

    if (trimmed.size() == 0 || trimmed[0] == ';' || trimmed[0] == '#') {
      continue;
    }

    auto commentPos = line.find_first_of(";#");
    String beforeComment = commentPos == String::npos ? line : line.substr(0, commentPos);

    size_t pos = 0;
    while (pos < beforeComment.size() && std::isspace(static_cast<unsigned char>(beforeComment[pos]))) {
      pos++;
    }

    const String key = "version";
    if (beforeComment.compare(pos, key.size(), key) != 0) {
      continue;
    }

    size_t afterKey = pos + key.size();
    if (afterKey < beforeComment.size() &&
        !std::isspace(static_cast<unsigned char>(beforeComment[afterKey])) &&
        beforeComment[afterKey] != '=') {
      continue;
    }

    String indent(beforeComment.begin(), beforeComment.begin() + static_cast<long>(pos));
    String newLine = indent + "version = \"" + newVersion + "\"";

    if (commentPos != String::npos) {
      newLine += line.substr(commentPos);
    }

    line = newLine;
    updated = true;
    break;
  }

  if (sawMeta && !updated) {
    String indent;
    String versionLine = indent + "version = \"" + newVersion + "\"";
    auto insertPos = metaLineIndex + 1;
    lines.insert(lines.begin() + insertPos, versionLine);
    updated = true;
  }

  if (!sawMeta) {
    lines.push_back("");
    lines.push_back("[meta]");
    lines.push_back("version = \"" + newVersion + "\"");
    updated = true;
  }

  if (!updated) {
    logError("failed to update version in configuration file '" + path.string() + "'");
    return false;
  }

  StringStream out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size()) {
      out << "\n";
    }
  }

  writeFile(path, out.str());
  return true;
}

inline bool updateVersionInConfigFile (
  const fs::path& path,
  UserConfigFormat format,
  const String& newVersion
) {
  if (path.empty()) {
    logError("cannot update version without an explicit configuration file path");
    return false;
  }

  switch (format) {
    case UserConfigFormat::Toml:
      return updateVersionInTomlConfig(path, newVersion);
    case UserConfigFormat::Ini:
      return updateVersionInIniConfig(path, newVersion);
    default:
      logError("unsupported configuration format for version update");
      return false;
  }
}

bool equal (const String& s1, const String& s2) {
  return s1.compare(s2) == 0;
}

struct CliHelpDoc {
  String command;
  String displayCommand;
  String summary;
  Vector<String> aliases;
  const char* helpTemplate;
};

struct CliHelpMatch {
  const CliHelpDoc* doc;
  String renderedHelp;
  Vector<String> snippets;
  int score;
  bool exactAliasMatch;
};

static inline String normalizeHelpSearchText (const String& value) {
  String normalized;
  normalized.reserve(value.size());
  bool previousWasSeparator = true;

  for (const auto ch : value) {
    const auto codepoint = static_cast<unsigned char>(ch);
    if (std::isalnum(codepoint)) {
      normalized += static_cast<char>(std::tolower(codepoint));
      previousWasSeparator = false;
    } else if (!previousWasSeparator) {
      normalized += ' ';
      previousWasSeparator = true;
    }
  }

  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }

  return normalized;
}

static const Vector<CliHelpDoc>& getCliHelpDocs () {
  static const Vector<CliHelpDoc> docs = {
    {
      "oroc",
      "oroc",
      "Command line interface for Oro Runtime projects.",
      { "cli", "commands", "subcommands", "tooling" },
      gHelpText
    },
    {
      "help",
      "help",
      "Discover commands, options, and workflow entry points from the CLI help index.",
      { "search", "find", "discover", "discovery", "topics", "query" },
      gHelpTextHelp
    },
    {
      "build",
      "build",
      "Build project.",
      { "compile", "package", "release", "ios", "android", "desktop" },
      gHelpTextBuild
    },
    {
      "list-devices",
      "list-devices",
      "Get the list of connected devices.",
      { "list devices", "devices", "device", "adb", "udid", "ecid" },
      gHelpTextListDevices
    },
    {
      "init",
      "init",
      "Create a new project.",
      { "scaffold", "create project", "starter", "bootstrap" },
      gHelpTextInit
    },
    {
      "install-app",
      "install-app",
      "Install app to the device.",
      { "install app", "deploy", "device install" },
      gHelpTextInstallApp
    },
    {
      "print-build-dir",
      "print-build-dir",
      "Print build path to stdout.",
      { "print build dir", "build output", "artifact path" },
      gHelpTextPrintBuildDir
    },
    {
      "run",
      "run",
      "Run application.",
      { "launch", "execute", "tests", "headless" },
      gHelpTextRun
    },
    {
      "config",
      "config",
      "Inspect configuration values.",
      { "configuration", "settings", "oro toml", "oro ini", "defaults" },
      gHelpTextConfig
    },
    {
      "env",
      "env",
      "Print environment variables relevant to the Oro CLI and build configuration.",
      { "environment", "variables", "toolchain env" },
      gHelpTextEnv
    },
    {
      "mcp",
      "mcp",
      "Run a Model Context Protocol (MCP) server for agent tooling.",
      { "model context protocol", "agent", "agents", "json rpc", "stdio" },
      gHelpTextMcp
    },
    {
      "setup",
      "setup",
      "Setup build tools for host or target platform.",
      { "toolchain", "sdk", "dependencies", "bootstrap" },
      gHelpTextSetup
    },
    {
      "version",
      "version",
      "Inspect or bump the project version defined in your configuration file.",
      { "semver", "release version", "bump" },
      gHelpTextVersion
    },
    {
      "versions",
      "versions",
      "Print Oro CLI/runtime and dependency versions.",
      { "dependency versions", "dependencies", "deps", "libraries" },
      gHelpTextVersions
    },
    {
      "update",
      "update",
      "Update tooling for manifests, signatures, and bundles.",
      { "updates", "manifest", "signatures", "bundles", "oup" },
      gHelpTextUpdate
    },
    {
      "update-init",
      "update init",
      "Scaffold a minimal update manifest JSON file.",
      { "update-init", "manifest init", "manifest scaffold" },
      gHelpTextUpdateInit
    },
    {
      "update-server",
      "update server",
      "Run an update server that speaks the Oro Application Update Protocol.",
      { "update-server", "oup server", "update service", "http", "tcp", "udp" },
      gHelpTextUpdateServer
    },
    {
      "update-info",
      "update info",
      "Query update servers or static manifests over HTTP/TCP/UDP.",
      { "update-info", "check updates", "manifest info", "manifest url" },
      gHelpTextUpdateInfo
    },
    {
      "update-keygen",
      "update keygen",
      "Generate an Ed25519 keypair for signing update manifests.",
      { "update-keygen", "keys", "signing key", "ed25519" },
      gHelpTextUpdateKeygen
    },
    {
      "update-sign",
      "update sign",
      "Sign an update manifest and emit a detached manifest.sig file.",
      { "update-sign", "signature", "sign manifest" },
      gHelpTextUpdateSign
    },
    {
      "update-verify",
      "update verify",
      "Verify an update manifest signature with a public key.",
      { "update-verify", "verify signature", "public key" },
      gHelpTextUpdateVerify
    },
    {
      "update-bundle",
      "update bundle",
      "Build a tar archive from a directory suitable as an update artifact.",
      { "update-bundle", "bundle artifact", "tar archive" },
      gHelpTextUpdateBundle
    },
    {
      "update-extract",
      "update extract",
      "Extract an update bundle tar archive into a directory.",
      { "update-extract", "unpack bundle", "extract artifact" },
      gHelpTextUpdateExtract
    },
    {
      "update-validate",
      "update validate",
      "Validate a manifest.json file against the update manifest schema shape.",
      { "update-validate", "validate manifest", "schema" },
      gHelpTextUpdateValidate
    }
  };

  return docs;
}

static const CliHelpDoc* findCliHelpDoc (const String& command) {
  for (const auto& doc : getCliHelpDocs()) {
    if (equal(doc.command, command)) {
      return &doc;
    }
  }

  return nullptr;
}

static const CliHelpDoc* findCliHelpDocByQuery (const String& query) {
  const auto normalizedQuery = normalizeHelpSearchText(query);
  if (normalizedQuery.size() == 0) {
    return nullptr;
  }

  for (const auto& doc : getCliHelpDocs()) {
    if (normalizeHelpSearchText(doc.command) == normalizedQuery) {
      return &doc;
    }

    if (normalizeHelpSearchText(doc.displayCommand) == normalizedQuery) {
      return &doc;
    }
  }

  return nullptr;
}

static inline String renderCliHelpText (const CliHelpDoc& doc) {
  return tmpl(doc.helpTemplate, defaultTemplateAttrs);
}

static inline String renderCliHelpText (const String& command) {
  const auto* doc = findCliHelpDoc(command);
  if (doc == nullptr) {
    return "";
  }

  return renderCliHelpText(*doc);
}

static inline String helpCommandHintForDoc (const CliHelpDoc& doc) {
  if (equal(doc.command, "oroc")) {
    return String(gCliDisplayName) + " --help";
  }

  return String(gCliDisplayName) + " " + doc.displayCommand + " --help";
}

static Vector<String> collectCliHelpSnippets (
  const String& renderedHelp,
  const String& normalizedQuery,
  const Vector<String>& tokens,
  size_t limit = 3
) {
  Vector<String> snippets;
  std::unordered_set<String> seen;

  for (const auto& rawLine : splitLines(renderedHelp)) {
    const auto line = trim(rawLine);
    if (line.size() == 0) {
      continue;
    }

    if (line.rfind(String(gCliDisplayName) + " v", 0) == 0) {
      continue;
    }

    const auto normalizedLine = normalizeHelpSearchText(line);
    if (normalizedLine.size() == 0) {
      continue;
    }

    bool matches = normalizedQuery.size() > 0 && normalizedLine.find(normalizedQuery) != String::npos;
    if (!matches) {
      for (const auto& token : tokens) {
        if (token.size() > 0 && normalizedLine.find(token) != String::npos) {
          matches = true;
          break;
        }
      }
    }

    if (!matches) {
      continue;
    }

    if (!seen.insert(normalizedLine).second) {
      continue;
    }

    snippets.push_back(line);
    if (snippets.size() >= limit) {
      break;
    }
  }

  return snippets;
}

static CliHelpMatch buildCliHelpMatch (
  const CliHelpDoc& doc,
  const String& renderedHelp,
  const String& normalizedQuery,
  const Vector<String>& tokens,
  int score,
  bool exactAliasMatch
) {
  return CliHelpMatch {
    &doc,
    renderedHelp,
    collectCliHelpSnippets(renderedHelp, normalizedQuery, tokens),
    score,
    exactAliasMatch
  };
}

static Vector<CliHelpMatch> listCliHelpCatalog () {
  Vector<CliHelpMatch> matches;
  matches.reserve(getCliHelpDocs().size());

  for (const auto& doc : getCliHelpDocs()) {
    matches.push_back(buildCliHelpMatch(doc, renderCliHelpText(doc), "", {}, 0, false));
  }

  return matches;
}

static Vector<CliHelpMatch> searchCliHelp (const String& query) {
  Vector<CliHelpMatch> matches;
  const auto normalizedQuery = normalizeHelpSearchText(query);
  if (normalizedQuery.size() == 0) {
    return matches;
  }

  const auto tokens = split(normalizedQuery, ' ');

  for (const auto& doc : getCliHelpDocs()) {
    const auto renderedHelp = renderCliHelpText(doc);
    const auto normalizedCommand = normalizeHelpSearchText(doc.command);
    const auto normalizedDisplay = normalizeHelpSearchText(doc.displayCommand);
    const auto normalizedSummary = normalizeHelpSearchText(doc.summary);

    String aliasBuffer = normalizedCommand + " " + normalizedDisplay;
    for (const auto& alias : doc.aliases) {
      const auto normalizedAlias = normalizeHelpSearchText(alias);
      if (normalizedAlias.size() == 0) {
        continue;
      }

      if (aliasBuffer.size() > 0) {
        aliasBuffer += " ";
      }
      aliasBuffer += normalizedAlias;
    }

    const auto normalizedHelp = normalizeHelpSearchText(renderedHelp);
    const String combined =
      normalizedCommand + " " +
      normalizedDisplay + " " +
      aliasBuffer + " " +
      normalizedSummary + " " +
      normalizedHelp;

    int matchedTokenCount = 0;
    int score = 0;

    if (normalizedCommand.find(normalizedQuery) != String::npos) {
      score += 720;
    }

    if (normalizedDisplay.find(normalizedQuery) != String::npos) {
      score += 760;
    }

    if (aliasBuffer.find(normalizedQuery) != String::npos) {
      score += 620;
    }

    if (normalizedSummary.find(normalizedQuery) != String::npos) {
      score += 360;
    }

    if (normalizedHelp.find(normalizedQuery) != String::npos) {
      score += 150;
    }

    for (const auto& token : tokens) {
      if (token.size() == 0) {
        continue;
      }

      const bool matchesCommand = normalizedCommand.find(token) != String::npos;
      const bool matchesDisplay = normalizedDisplay.find(token) != String::npos;
      const bool matchesAliases = aliasBuffer.find(token) != String::npos;
      const bool matchesSummary = normalizedSummary.find(token) != String::npos;
      const bool matchesHelp = normalizedHelp.find(token) != String::npos;

      if (matchesCommand || matchesDisplay || matchesAliases || matchesSummary || matchesHelp) {
        matchedTokenCount++;

        if (matchesCommand || matchesDisplay) {
          score += 130;
        } else if (matchesAliases) {
          score += 100;
        } else if (matchesSummary) {
          score += 70;
        } else if (matchesHelp) {
          score += 30;
        }
      } else {
        score -= 20;
      }
    }

    if (matchedTokenCount == 0 && combined.find(normalizedQuery) == String::npos) {
      continue;
    }

    if (!tokens.empty() && matchedTokenCount == static_cast<int>(tokens.size())) {
      score += 120;
    }

    if (equal(doc.command, "oroc") || equal(doc.command, "help")) {
      score -= 80;
    }

    if (score <= 0) {
      continue;
    }

    matches.push_back(buildCliHelpMatch(doc, renderedHelp, normalizedQuery, tokens, score, false));
  }

  std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }

    return a.doc->displayCommand < b.doc->displayCommand;
  });

  return matches;
}

static void printCliHelpSearchResults (const String& query, const Vector<CliHelpMatch>& matches) {
  if (matches.empty()) {
    std::cout
      << "No help results matched '" << query << "'." << std::endl
      << std::endl
      << "Try '" << gCliDisplayName << " help build', '" << gCliDisplayName
      << " help config', or '" << gCliDisplayName << " --help'." << std::endl;
    return;
  }

  std::cout
    << "Search query: " << query << std::endl
    << std::endl
    << "Top matches:" << std::endl;

  const auto count = std::min<size_t>(matches.size(), 8);
  for (size_t i = 0; i < count; i++) {
    const auto& match = matches[i];
    std::cout << "  " << match.doc->displayCommand << std::endl;
    std::cout << "    " << match.doc->summary << std::endl;

    for (const auto& snippet : match.snippets) {
      if (snippet == match.doc->summary) {
        continue;
      }

      std::cout << "    match: " << snippet << std::endl;
    }

    std::cout << "    see: " << helpCommandHintForDoc(*match.doc) << std::endl;

    if (i + 1 < count) {
      std::cout << std::endl;
    }
  }

  std::cout
    << std::endl
    << "Use '" << gCliDisplayName << " help <command>' or '"
    << gCliDisplayName << " <command> --help' for the full page."
    << std::endl;
}

static void printCliHelpSearchJson (
  const String& query,
  const Vector<CliHelpMatch>& matches,
  bool exactMatch,
  const String& rootHelpText = ""
) {
  JSON::Array resultsJson;

  for (const auto& match : matches) {
    JSON::Array keywordsJson;
    for (const auto& alias : match.doc->aliases) {
      keywordsJson.push(alias);
    }

    JSON::Array snippetsJson;
    for (const auto& snippet : match.snippets) {
      snippetsJson.push(snippet);
    }

    JSON::Object::Entries resultEntries {
      { "command", match.doc->displayCommand },
      { "internalCommand", match.doc->command },
      { "summary", match.doc->summary },
      { "keywords", keywordsJson },
      { "usageHint", helpCommandHintForDoc(*match.doc) },
      { "snippets", snippetsJson },
      { "score", match.score },
      { "exact", match.exactAliasMatch }
    };

    if (match.exactAliasMatch) {
      resultEntries["help"] = match.renderedHelp;
    }

    resultsJson.push(JSON::Object(resultEntries));
  }

  JSON::Object::Entries entries {
    { "query", query },
    { "normalizedQuery", normalizeHelpSearchText(query) },
    { "exactMatch", exactMatch },
    { "count", static_cast<int>(matches.size()) },
    { "results", resultsJson }
  };

  if (rootHelpText.size() > 0) {
    entries["help"] = rootHelpText;
  }

  JSON::Object object(entries);
  std::cout << object.str() << std::endl;
}

extern "C" {
  const unsigned char* oro_runtime_init_get_user_config_bytes () {
    return reinterpret_cast<const unsigned char*>(settingsSource.c_str());
  }

  unsigned int oro_runtime_init_get_user_config_bytes_size () {
    return settingsSource.size();
  }

  bool oro_runtime_init_is_debug_enabled () {
  #if defined(ORO_RUNTIME_BUILD_DEBUG)
    return ORO_RUNTIME_BUILD_DEBUG == 1;
  #else
    return false;
  #endif
  }

  const char* oro_runtime_init_get_dev_host () {
    return settings["host"].c_str();
  }

  int oro_runtime_init_get_dev_port () {
    if (settings.contains("port")) {
      return std::stoi(settings["port"].c_str());
    }

    return 0;
  }

  int oro_runtime_init_get_user_config_format () {
    return static_cast<int>(gEmbeddedConfigFormat);
  }
}

void printHelp (const String& command) {
  const auto helpText = renderCliHelpText(command);
  if (helpText.size() > 0) {
    std::cout << helpText << std::endl;
  }
}

struct RuntimeHomeLayout {
  Path preferred;
};

static inline Path expandUserPath (const String& value, const String& fallback = "") {
  if (value.size() > 0) {
    return Path(value);
  }

  if (fallback.size() > 0) {
    return Path(fallback);
  }

  return Path(".");
}

static RuntimeHomeLayout determineRuntimeHomeLayout () {
  RuntimeHomeLayout layout;
  const auto HOME = env::get("HOME").size() > 0 ? env::get("HOME") : env::get("USERPROFILE");

  if (platform.win) {
    auto localAppData = env::get("LOCALAPPDATA");
    auto base = expandUserPath(localAppData.size() > 0 ? localAppData : HOME);
    layout.preferred = base / "Programs" / "oro";
  } else if (platform.mac) {
    auto base = expandUserPath(HOME);
    layout.preferred = base / "Library" / "Application Support" / "Oro";
  } else {
    auto xdgDataHome = env::get("XDG_DATA_HOME");
    auto dataBase = xdgDataHome.size() > 0
      ? expandUserPath(xdgDataHome)
      : expandUserPath(HOME) / ".local" / "share";
    layout.preferred = dataBase / "oro";
  }

  return layout;
}

static inline String ensureTrailingSeparator (String value) {
  if (value.size() == 0) {
    return value;
  }

  auto tail = value.back();
  if (tail == '/' || tail == '\\') {
    return value;
  }

  value += platform.win ? "\\" : "/";
  return value;
}

static inline String normalizeRuntimeHomePath (const Path& path) {
  if (path.empty()) {
    return "";
  }

  std::error_code absError;
  auto resolved = fs::absolute(path, absError);
  if (absError) {
    resolved = path;
  }

  resolved.make_preferred();
  return ensureTrailingSeparator(resolved.string());
}

String getHomeHome (bool verbose) {
  static String runtimeHome = "";
  static bool initialized = false;
  static const bool debugEnabled =
    env::get("ORO_DEBUG").size() > 0 ||
    env::get("DEBUG").size() > 0;

  if (!initialized) {
    const auto layout = determineRuntimeHomeLayout();
    auto ORO_HOME = env::get("ORO_HOME");
    Path resolvedPath;

    auto ensureDirectory = [](const Path& path, const String& label) {
      if (path.empty()) {
        return;
      }

      std::error_code ec;
      fs::create_directories(path, ec);
      if (ec) {
        logWarn("failed to ensure " + label + " '" + path.string() + "': " + ec.message());
      }
    };

    if (ORO_HOME.size() > 0) {
      resolvedPath = Path(ORO_HOME);
    } else {
      resolvedPath = layout.preferred;
    }

    if (!resolvedPath.empty()) {
      ensureDirectory(resolvedPath, "ORO_HOME");
      runtimeHome = normalizeRuntimeHomePath(resolvedPath);
    }

    initialized = true;
  }

  if (runtimeHome.size() > 0) {
    #ifdef _WIN32
    env::set((String("ORO_HOME=") + runtimeHome).c_str());
    #else
    setenv("ORO_HOME", runtimeHome.c_str(), 1);
    #endif

    if (verbose && debugEnabled) {
      logWarn("'ORO_HOME' is set to '" + runtimeHome + "'");
    }
  }

  return runtimeHome;
}

String getHomeHome () {
  return getHomeHome(true);
}

String getAndroidHome () {
  static auto androidHome = env::get("ANDROID_HOME");
  if (androidHome.size() > 0) {
    return androidHome;
  }

  if (!platform.win) {
    auto cmd = String(
      "dirname $(dirname $(readlink $(which sdkmanager 2>/dev/null) 2>/dev/null) 2>/dev/null) 2>/dev/null"
    );

    auto r = exec(cmd);

    if (r.exitCode == 0) {
      androidHome = trim(r.output);
    }
  }

  if (androidHome.size() == 0) {
    if (platform.mac) {
      androidHome = env::get("HOME") + "/Library/Android/sdk";
    } else if (platform.unix) {
      androidHome = env::get("HOME") + "/android";
    } else if (platform.win) {
      // TODO
    }
  }

  if (androidHome.size() > 0) {
    #ifdef _WIN32
    env::set((String("ANDROID_HOME=") + androidHome).c_str());
    #else
    setenv("ANDROID_HOME", androidHome.c_str(), 1);
    #endif

    static const bool debugEnabled =
      env::get("ORO_DEBUG").size() > 0 ||
      env::get("DEBUG").size() > 0;

    if (debugEnabled) {
      logWarn("'ANDROID_HOME' is set to '" + androidHome + "'");
    }
  }

  return androidHome;
}

inline String prefixFile (String s) {
  static String oroHome = getHomeHome();
  return oroHome + s + " ";
}

inline String prefixFile () {
  static String oroHome = getHomeHome();
  return oroHome;
}

// Path-safe variants without trailing whitespace.
inline Path prefixPath () {
  static String oroHome = getHomeHome();
  return Path(oroHome);
}

inline Path prefixPath (const String& s) {
  static String oroHome = getHomeHome();
  return Path(oroHome) / s;
}

static const std::array<const char*, 1> gRuntimeLibraryNames = {
  "oro-runtime"
};

inline Path resolveRuntimeStaticArchive(const String& relativeDir, const String& suffix = ".a") {
  for (const auto* name : gRuntimeLibraryNames) {
    auto candidate = prefixPath(relativeDir + "/lib" + String(name) + suffix);
    if (fs::exists(candidate)) {
      return candidate;
    }
  }
  return prefixPath(relativeDir + "/lib" + String(gRuntimeLibraryNames.front()) + suffix);
}

inline String runtimeLinkFlag() {
  return "-l" + String(gRuntimeLibraryNames.front());
}

inline bool isRuntimeSharedLibrary(const fs::path& path) {
  const auto stem = path.stem().string();
  for (const auto* name : gRuntimeLibraryNames) {
    if (stem == "lib" + String(name)) {
      return true;
    }
  }
  return false;
}

static Process::PID appPid = 0;
static SharedPointer<Process> appProcess = nullptr;
static std::atomic<int> appStatus = -1;
static std::mutex appMutex;
static uv_loop_t *eventLoop = nullptr;

static uv_udp_t logsocket;
static int lastLogSequence = 0;

#if ORO_RUNTIME_PLATFORM_APPLE
static NSDate* lastLogTime = [NSDate now];
unsigned short createLogSocket() {
  std::promise<int> p;
  std::future<int> future = p.get_future();
  int port = 0;

  auto t = Thread([](std::promise<int>&& p) {
    eventLoop = uv_default_loop();

    uv_udp_init(eventLoop, &logsocket);
    struct sockaddr_in addr;
    int port;

    uv_ip4_addr("0.0.0.0", 0, &addr);
    uv_udp_bind(&logsocket, (const struct sockaddr*)&addr, UV_UDP_REUSEADDR);

    uv_udp_recv_start(
        &logsocket,
        [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
          *buf = uv_buf_init(new char[suggested_size], suggested_size);
        },
        [](uv_udp_t* req, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
          if (nread > 0) {
            std::string data(buf->base, nread);
            data = trim(data); // Assuming trim is defined elsewhere
            if (data[0] != '+') return;

            @autoreleasepool {
              NSError* err = nil;
              auto logs = [OSLogStore storeWithScope: OSLogStoreSystem error: &err]; // get snapshot

              if (err) {
                logError("Failed to open OSLogStore");
                return;
              }

              NSDate* adjustedLogTime = [lastLogTime dateByAddingTimeInterval: -1]; // adjust by subtracting 1 second
              auto position = [logs positionWithDate: adjustedLogTime];
              auto bid = settings["meta_bundle_identifier"];
              auto query = String("(category == 'oro.runtime') AND (subsystem == '" + bid + "')");
              auto predicate = [NSPredicate predicateWithFormat: [NSString stringWithUTF8String: query.c_str()]];
              auto enumerator = [logs entriesEnumeratorWithOptions: 0 position: position predicate: predicate error: &err];

              if (err) {
                logError("Failed to open OSLogStore");
                return;
              }

              id logEntry;

              while ((logEntry = [enumerator nextObject]) != nil) {
                OSLogEntryLog* entry = (OSLogEntryLog*)logEntry;
                String message = entry.composedMessage.UTF8String;
                String body;
                int seq = 0;
                Vector<String> parts = split(message, "::::");
                if (parts.size() < 2) continue;

                try {
                  seq = std::stoi(parts[0]);
                  body = parts[1];
                  if (body.size() == 0) continue;
                } catch (...) {
                  continue;
                }

                if (seq <= lastLogSequence && lastLogSequence > 0) continue;
                lastLogSequence = seq;

                std::cout << body << std::endl;
              }
            }
          }

          if (buf->base) delete[] buf->base;
        }
    );

    int len = sizeof(addr);

    if (uv_udp_getsockname(&logsocket, (struct sockaddr *)&addr, &len) == 0) {
      auto port = ntohs(addr.sin_port);
      p.set_value(port);
    }

    uv_run(eventLoop, UV_RUN_DEFAULT);
  }, std::move(p));

  port = future.get();
  t.detach();

  return port;
}
#endif

void handleBuildPhaseForUser (
  const Map<> settings,
  const String& targetPlatform,
  const Path pathResourcesRelativeToUserBuild,
  const Path& cwd,
  bool performAfterLifeCycle
) {
  do {
    char prefix[4096] = {0};
    std::memcpy(
      prefix,
      pathResourcesRelativeToUserBuild.string().c_str(),
      pathResourcesRelativeToUserBuild.string().size()
    );

    // @TODO(jwerle): use `env::set()` if #148 is closed
#if _WIN32
    String prefix_ = "PREFIX=";
    prefix_ += prefix;
    env::set(prefix_.c_str());
#else
    setenv("PREFIX", prefix, 1);
#endif
  } while (0);

  const bool shouldPassBuildArgs = settings.contains("build_script_forward_arguments") && settings.at("build_script_forward_arguments") == "true";
  String scriptArgs = shouldPassBuildArgs ? (" " + pathResourcesRelativeToUserBuild.string()) : "";

  if (settings.contains("build_script") && settings.at("build_script").size() > 0) {
    auto buildScript = settings.at("build_script");

    // Windows CreateProcess() won't work if the script has an extension other than exe (say .cmd or .bat)
    // cmd.exe can handle this translation
    if (platform.win) {
      scriptArgs = " /c \"" + buildScript + scriptArgs + "\"";
      buildScript = "cmd.exe";
    }

    auto scriptPath = (cwd / targetPath).string();
    logInfo("running build script (cmd='" + buildScript + "', args='" + scriptArgs + "', pwd='" + scriptPath + "')");

    auto process = new Process(
      buildScript,
      scriptArgs,
      scriptPath,
      [](String const &out) { io::write(out, false); },
      [](String const &out) { io::write(out, true); }
    );

    process->open();
    process->wait();

    if (process->status != 0) {
      // TODO(trevnorris): Force non-windows to exit the process.
      logError("build failed, exiting with code " + std::to_string(process->status));
      exit(process->status);
    }

    logVerbose("ran user build command");
  }

  // runs async, does not block
  if (performAfterLifeCycle && settings.contains("build_script_after") && settings.at("build_script_after").size() > 0) {
    auto buildScript = settings.at("build_script_after");

    // Windows CreateProcess() won't work if the script has an extension other than exe (say .cmd or .bat)
    // cmd.exe can handle this translation
    if (platform.win) {
      scriptArgs = " /c \"" + buildScript + scriptArgs + "\"";
      buildScript = "cmd.exe";
    }

    if (buildAfterScriptProcess != nullptr) {
      buildAfterScriptProcess->kill();
      buildAfterScriptProcess->wait();
      delete buildAfterScriptProcess;
      buildAfterScriptProcess = nullptr;
    }

    buildAfterScriptProcess = new Process(
      buildScript,
      scriptArgs,
      (cwd / targetPath).string(),
      [](String const &out) { io::write(out, false); },
      [](String const &out) { io::write(out, true); },
      [](const auto status) {
        const auto exitCode = std::atoi(status.c_str());
        if (exitCode != 0) {
          logError("build after script failed with code: " + status);
        }
      }
    );

    buildAfterScriptProcess->open();
  }
}

Vector<Path> handleBuildPhaseForCopyMappedFiles (
  const Map<> settings,
  const String& targetPlatform,
  const Path pathResourcesRelativeToUserBuild,
  bool includeBuildCopyFiles = true
) {
  Vector<Path> copyMapFiles;

  if (includeBuildCopyFiles && settings.contains("build_copy")) {
    String buildCopyValue = settings.at("build_copy");
    const Path pathInput = buildCopyValue.size() > 0
      ? Path(buildCopyValue).make_preferred()
      : Path("src");

    auto paths = split(pathInput.string(), ';');
    for (const auto& p : paths) {
      auto mapping = split(p, '=');
      auto src = targetPath / trim(mapping[0]);
      auto dst = mapping.size() == 2
        ? pathResourcesRelativeToUserBuild / trim(mapping[1])
        : pathResourcesRelativeToUserBuild;

      src = src.make_preferred();
      dst = dst.make_preferred();

      if (!fs::exists(fs::status(src))) {
        logWarn("[build] copy entry '" + src.string() +  "' does not exist");
        continue;
      }

      if (!fs::exists(fs::status(dst.parent_path()))) {
        fs::create_directories(dst.parent_path());
      }

      std::error_code relError;
      auto srcAbs = fs::absolute(src, relError);
      auto dstAbs = fs::absolute(dst, relError);

      if (!relError) {
        std::error_code ec;
        auto rel = fs::relative(dstAbs, srcAbs, ec);
        if (!ec) {
          bool dstInsideSrc = false;
          if (rel.empty() || rel == ".") {
            dstInsideSrc = true;
          } else {
            dstInsideSrc = true;
            for (const auto& part : rel) {
              if (part == "..") {
                dstInsideSrc = false;
                break;
              }
            }
          }

          if (dstInsideSrc) {
            logWarn(
              "[build] copy entry '" +
              srcAbs.string() +
              "' has destination inside source ('" +
              dstAbs.string() +
              "'); skipping to avoid recursive copy"
            );
            continue;
          }
        }
      }

      fs::copy(
        src,
        dst,
        fs::copy_options::update_existing | fs::copy_options::recursive | fs::copy_options::copy_symlinks
      );
    }
  }

  for (const auto& tuple : settings) {
    if (!tuple.first.starts_with("build_copy-map_")) {
      continue;
    }

    auto key = replace(tuple.first, "build_copy-map_", "");
    auto value = tuple.second;

    auto src = Path { key };
    auto dst = tuple.second.size() > 0
      ? pathResourcesRelativeToUserBuild / value
      : pathResourcesRelativeToUserBuild;

    src = src.make_preferred();
    dst = dst.make_preferred();

    if (src.is_relative()) {
      src = targetPath / src;
    }

    src = fs::absolute(src);
    dst = fs::absolute(dst);

    if (!fs::exists(fs::status(src))) {
      logWarn("[build.copy-map] entry '" + convertWStringToString(src.string()) +  "' does not exist");
      continue;
    }

    // ensure 'dst' parent directories exist
    if (!fs::exists(fs::status(dst.parent_path()))) {
      fs::create_directories(dst.parent_path());
    }

    auto mappedSourceFile = fs::absolute(
      pathResourcesRelativeToUserBuild /
      fs::relative(src, targetPath)
    );

    if (
      !mappedSourceFile.string().ends_with(".") &&
      pathResourcesRelativeToUserBuild.compare(mappedSourceFile) != 0
     ) {
      if (fs::exists(fs::status(mappedSourceFile))) {
        fs::remove_all(mappedSourceFile);
      }
    }

    if (flagVerboseMode) {
      const auto relativeSource = convertWStringToString(fs::relative(src, targetPath).native());
      const auto relativeDestination = convertWStringToString(fs::relative(dst, targetPath).native());
      debug(
        "copy %s ~> %s",
        relativeSource.c_str(),
        relativeDestination.c_str()
      );
    }

    // For copy-map entries, always copy the resolved file contents,
    // not the symlink itself, so resources in the bundle are real files.
    fs::copy(
      src,
      dst,
      fs::copy_options::update_existing | fs::copy_options::recursive
    );
  }

  // copy map file for all platforms
  if (settings.contains("build_copy_map")) {
    auto copyMapFile = Path(settings.at("build_copy_map")).make_preferred();

    if (copyMapFile.is_relative()) {
      copyMapFile = targetPath / copyMapFile;
    }

    copyMapFiles.push_back(copyMapFile);
  }

  // copy map file for target platform
  if (
    targetPlatform.starts_with("ios") &&
    settings.contains("build_ios_copy_map")
   ) {
    auto copyMapFile = Path(settings.at("build_ios_copy_map")).make_preferred();

    if (copyMapFile.is_relative()) {
      copyMapFile = targetPath / copyMapFile;
    }

    copyMapFiles.push_back(copyMapFile);
  }

  if (
    targetPlatform.starts_with("android") &&
    settings.contains("build_android_copy_map")
   ) {
    auto copyMapFile = Path(settings.at("build_android_copy_map")).make_preferred();

    if (copyMapFile.is_relative()) {
      copyMapFile = targetPath / copyMapFile;
    }

    copyMapFiles.push_back(copyMapFile);
  }

  if (settings.contains("build_" + platform.os +  "_copy_map")) {
    auto copyMapFile = Path(settings.at("build_" + platform.os +  "_copy_map")).make_preferred();

    if (copyMapFile.is_relative()) {
      copyMapFile = targetPath / copyMapFile;
    }

    copyMapFiles.push_back(copyMapFile);
  }

  for (const auto& copyMapFile : copyMapFiles) {
    if (!fs::exists(fs::status(copyMapFile)) || !fs::is_regular_file(copyMapFile)) {
      logWarn("file specified in [build] copy_map does not exist");
    } else {
      auto copyMapSource = tmpl(trim(readFile(copyMapFile)), settings);
      Map<> copyMap;

      auto format = detectConfigFormatForPath(copyMapFile);
      if (format == UserConfigFormat::Toml) {
        try {
          auto document = TOML::parse(copyMapSource);
          if (!document.isTable()) {
            logWarn("copy_map TOML file '" + copyMapFile.string() + "' must contain only top-level key/value pairs");
          } else {
            const auto& table = document.asTable();
            for (const auto& entry : table) {
              if (!entry.second.is(TOML::Type::String)) {
                continue;
              }
              copyMap.insert_or_assign(entry.first, entry.second.asString());
            }
          }
        } catch (const std::exception& error) {
          logError(
            "failed to parse copy_map TOML file '" +
            copyMapFile.string() +
            "': " +
            String(error.what())
          );
          continue;
        }
      } else {
        copyMap = INI::parse(copyMapSource);
      }

      auto copyMapFileDirectory = fs::absolute(copyMapFile.parent_path());

      for (const auto& tuple : copyMap) {
        auto key = tuple.first;
        auto& value = tuple.second;

        if (key.starts_with("win_")) {
          if (!platform.win) continue;
          key = key.substr(4, key.size() - 4);
        }

        if (key.starts_with("mac_")) {
          if (!platform.mac) continue;
          key = key.substr(4, key.size() - 4);
        }

        if (key.starts_with("ios_")) {
          if (!platform.mac || !targetPlatform.starts_with("ios")) continue;
          key = key.substr(4, key.size() - 4);
        }

        if (key.starts_with("linux_")) {
          if (!platform.linux) continue;
          key = key.substr(6, key.size() - 6);
        }

        if (key.starts_with("android_")) {
          if (!targetPlatform.starts_with("android")) continue;
          key = key.substr(8, key.size() - 8);
        }

        if (key.starts_with("debug_")) {
          if (!flagDebugMode) continue;
          key = key.substr(6, key.size() - 6);
        }

        if (key.starts_with("prod_")) {
          if (flagDebugMode) continue;
          key = key.substr(5, key.size() - 5);
        }

        if (key.starts_with("production_")) {
          if (flagDebugMode) continue;
          key = key.substr(11, key.size() - 11);
        }

        auto src = Path { key };
        auto dst = tuple.second.size() > 0
          ? pathResourcesRelativeToUserBuild / value
          : pathResourcesRelativeToUserBuild;

        src = src.make_preferred();
        dst = dst.make_preferred();

        if (src.is_relative()) {
          src = copyMapFileDirectory / src;
        }

        src = fs::absolute(src);
        dst = fs::absolute(dst);

        if (!fs::exists(fs::status(src))) {
          logWarn("[build] copy_map entry '" + src.string() +  "' does not exist");
          continue;
        }

        if (!fs::exists(fs::status(dst.parent_path()))) {
          fs::create_directories(dst.parent_path());
        }

        auto mappedSourceFile = (
          pathResourcesRelativeToUserBuild /
          fs::relative(src, copyMapFileDirectory)
        );

        if (
          !mappedSourceFile.string().ends_with(".") &&
          pathResourcesRelativeToUserBuild.compare(mappedSourceFile) != 0
         ) {
          if (fs::exists(fs::status(mappedSourceFile))) {
            fs::remove_all(mappedSourceFile);
          }
        }

        // For copy-map entries loaded from a file, copy the target
        // file or directory contents instead of preserving symlinks.
        fs::copy(
          src,
          dst,
          fs::copy_options::update_existing | fs::copy_options::recursive
        );
      }
    }
  }

  if (
    settings.contains("oroc_inferred_entry_path") &&
    settings.contains("oroc_inferred_entry_content")
  ) {
    auto entryPath = pathResourcesRelativeToUserBuild /
      Path(settings.at("oroc_inferred_entry_path")).make_preferred();

    if (!fs::exists(entryPath.parent_path())) {
      fs::create_directories(entryPath.parent_path());
    }

    writeFile(entryPath, settings.at("oroc_inferred_entry_content"));
  }

  return copyMapFiles;
}

void signalHandler (int signum) {
#if !ORO_RUNTIME_PLATFORM_WINDOWS
  if (signum == SIGUSR1) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    checkLogStore = true;
  #endif
    return;
  }
#endif

#if !ORO_RUNTIME_PLATFORM_WINDOWS
  if (appPid > 0) {
    // Forward the received signal to the launched app process first
    kill(appPid, signum);
    // If we launched without a managed Process handle (e.g., macOS NSWorkspace),
    // escalate signals directly to ensure teardown.
    if (!appProcess) {
      kill(appPid, SIGTERM);
      kill(appPid, SIGINT);
      kill(appPid, SIGKILL);
    }
  }
#endif

  if (signum == SIGINT || signum == SIGTERM) {
    if (appStatus == -1) {
    logInfo("App result (signal): " + std::to_string(signum));
    }

    if (appProcess != nullptr) {
      // Forcefully terminate the entire app process group to guarantee teardown
      // (Process::kill sends TERM -> INT -> KILL to the child's process group)
      appProcess->kill();
      appProcess->wait();
      appProcess = nullptr;
    }

    appPid = 0;

    if (sourcesWatcherSupportThread != nullptr) {
      if (sourcesWatcherSupportThread->joinable()) {
        sourcesWatcherSupportThread->join();
      }
      delete sourcesWatcherSupportThread;
      sourcesWatcherSupportThread = nullptr;
    }

    if (buildAfterScriptProcess != nullptr) {
      buildAfterScriptProcess->kill();
      buildAfterScriptProcess->wait();
      delete buildAfterScriptProcess;
      buildAfterScriptProcess = nullptr;
    }

    if (appStatus == -1) {
      appStatus = signum;
    }

  if (signum == SIGTERM || signum == SIGINT) {
  #if ORO_RUNTIME_PLATFORM_LINUX
      while (wait(NULL) != -1 || errno == EINTR) {}
  #endif
      signal(signum, SIG_DFL);
      raise(signum);
    }

  #if ORO_RUNTIME_PLATFORM_LINUX
    if (gtk_main_level() > 0) {
      g_main_context_invoke(
        nullptr,
        +[](gpointer userData) -> gboolean {
          msleep(1000);
          gtk_main_quit();
          return true;
        },
        nullptr
      );
    }
  #endif
  }
}

void checkIosSimulatorDeviceAvailability (const String& device) {
  const String cliName(gCliDisplayName);
  if (device.size() == 0) {
    logError("[ios] simulator_device option is empty. See '" + cliName + " run --help' (common errors) and '" + cliName + " list-devices --platform=ios'");
    exit(1);
  }
  auto const rDevices = exec("xcrun simctl list devices available | grep -e \"  \"");
  auto isDeviceFound = rDevices.output.find(device) != String::npos;

  if (!isDeviceFound) {
    logError("[ios] simulator_device option is invalid: " + device + ". See '" + cliName + " run --help' (common errors) and '" + cliName + " list-devices --platform=ios'");
    logWarn("available devices:\n" + rDevices.output);
    logWarn("please update your oro.toml or oro.ini with a valid device or install Simulator runtime (https://developer.apple.com/documentation/xcode/installing-additional-simulator-runtimes)");
    exit(1);
  }
}

int runApp (const Path& path, const String& args, bool headless) {
  auto cmd = path.string();

  if (!fs::exists(path)) {
    logError("Executable not found at " + cmd + helpHint("build"));
    std::cout << "Try running `" << gCliDisplayName << " build` first." << std::endl;
    return 1;
  }

  auto runner = trim(String(CONVERT_TO_STRING(CMD_RUNNER)));
  auto prefix = runner.size() > 0 ? runner + String(" ") : runner;
  String headlessCommand = "";

  if (headless) {
    auto headlessRunner = settings["headless_runner"];
    auto headlessRunnerFlags = settings["headless_runner_flags"];

    if (headlessRunner.size() == 0) {
      headlessRunner = settings["headless_" + platform.os + "_runner"];
    }

    if (headlessRunnerFlags.size() == 0) {
      headlessRunnerFlags = settings["headless_" + platform.os + "_runner_flags"];
    }

    if (platform.linux) {
      // use xvfb for linux as a default
      if (headlessRunner.size() == 0) {
        headlessRunner = "xvfb-run";
        int status = std::system((headlessRunner + " --help >/dev/null").c_str());
        if (WEXITSTATUS(status) != 0) {
          headlessRunner = "";
          logWarn("'xvfb-run' not found; install it (sudo apt-get install xvfb) or set [build] headless_runner[_flags].");
        }
      }

      if (headlessRunnerFlags.size() == 0) {
        // use sane defaults if 'xvfb-run' is used
        if (headlessRunner == "xvfb-run") {
          headlessRunnerFlags = " -a --server-args='-screen 0 1920x1080x24' ";
        }
      }
    }

    if (headlessRunner != "false") {
      headlessCommand = headlessRunner + headlessRunnerFlags;
    }
  }

  #if defined(__APPLE__)
    // Headless launches stay attached to the CLI so output, signals, and the
    // application exit status are supervised by the child process path.
    if (platform.mac && !headless) {
      auto sharedWorkspace = [NSWorkspace sharedWorkspace];
      auto configuration = [NSWorkspaceOpenConfiguration configuration];
      auto stringPath = path.string();
      auto slice = stringPath.substr(0, stringPath.rfind(".app") + 4);

      auto url = [NSURL
        fileURLWithPath: [NSString stringWithUTF8String: slice.c_str()]
      ];

      auto bundle = [NSBundle bundleWithURL: url];
      auto env = [[NSMutableDictionary alloc] init];

      auto pidString = [NSString stringWithFormat: @"%d", getpid()];
      env[@"ORO_CLI_PID"] = pidString;

      for (auto const &envKey : parseStringList(settings["build_env"])) {
        auto cleanKey = trim(envKey);

        cleanKey.erase(0, cleanKey.find_first_not_of(","));
        cleanKey.erase(cleanKey.find_last_not_of(",") + 1);

        auto envValue = env::get(cleanKey.c_str());
        auto key = [NSString stringWithUTF8String: cleanKey.c_str()];
        auto value = [NSString stringWithUTF8String: envValue.c_str()];

        env[key] = value;
      }

      for (auto const &tuple : settings) {
        if (tuple.first.starts_with("env_")) {
          const auto key = @(tuple.first.substr(4).c_str());
          const auto value = @(tuple.second.c_str());
          env[key] = value;
        }
      }

      auto splitArgs = split(args, ' ');
      auto arguments = [[NSMutableArray alloc] init];

      for (auto arg : splitArgs) {
        [arguments addObject: [NSString stringWithUTF8String: arg.c_str()]];
      }

      [arguments addObject: @"--from-oroc"];

      auto port = std::to_string(createLogSocket());
      env[@"ORO_LOG_SOCKET"] = @(port.c_str());

      auto parentLogSocket = env::get("ORO_PARENT_LOG_SOCKET");
      if (parentLogSocket.size() > 0) {
        env[@"ORO_PARENT_LOG_SOCKET"] = @(parentLogSocket.c_str());
      }

      configuration.createsNewApplicationInstance = YES;
      configuration.promptsUserIfNeeded = YES;
      configuration.environment = env;
      configuration.arguments = arguments;
      configuration.activates = headless ? NO : YES;

      if (!bundle) {
        logError("Unable to find the application bundle");
        return 1;
      }

      logInfo(String("Running App: " + String(bundle.bundlePath.UTF8String)));

      appMutex.lock();

      [sharedWorkspace
        openApplicationAtURL: bundle.bundleURL
               configuration: configuration
           completionHandler: ^(NSRunningApplication* app, NSError* error) {
        if (error) {
          appMutex.unlock();
          appStatus = 1;
          debug(
            "ERROR: NSWorkspace: (code=%lu, domain=%@) %@",
            error.code,
            error.domain,
            error.localizedDescription
          );
        } else {
          appPid = app.processIdentifier;
        }
      }];

      std::lock_guard<std::mutex> lock(appMutex);

      if (appStatus != -1) {
        logInfo("App result: " + std::to_string(appStatus.load()));
        return appStatus.load();
      }

      return 0;
    }
  #endif

  logInfo(String("Running App: " + headlessCommand + prefix + cmd +  args + " --from-oroc"));

  #if defined(__linux__)
    // unlink lock file that may existk
    static const auto bundleIdentifier = settings["meta_bundle_identifier"];
    static const auto TMPDIR = env::get("TMPDIR", "/tmp");
    static const auto appInstanceLock = fs::path(TMPDIR) / (bundleIdentifier + ".lock");
    unlink(appInstanceLock.c_str());
  #endif

  appProcess = std::make_shared<Process>(
    headlessCommand + prefix + cmd,
    args + " --from-oroc",
    fs::current_path().string(),
    [](const auto& output) { std::cout << output << std::endl; },
    [](const auto& output) { std::cerr << output << std::endl; },
    [](const auto& output) { signalHandler(std::atoi(output.c_str()));  }
  );

  auto p = appProcess;
  appPid = p->open();
  const auto status = p->wait();

  if (appStatus == -1) {
    appStatus = status;
    logInfo("App result: " + std::to_string(appStatus.load()));
  }

  p = nullptr;

  return appStatus;
}

int runApp (const Path& path, const String& args) {
  return runApp(path, args, false);
}

void runIOSSimulator (const Path& path, Map<>& settings) {
  #ifndef _WIN32
  String uuid;
  bool booted = false;

  if (settings.count("ios_simulator_uuid") > 0) {
    uuid = settings["ios_simulator_uuid"];
  } else {
    checkIosSimulatorDeviceAvailability(settings["ios_simulator_device"]);

    String deviceType;
    StringStream listDeviceTypesCommand;
    listDeviceTypesCommand
      << "xcrun"
      << " simctl"
      << " list devicetypes";

    auto rListDeviceTypes = exec(listDeviceTypesCommand.str().c_str());
      if (rListDeviceTypes.exitCode != 0) {
      logError("failed to list device types using \"" + listDeviceTypesCommand.str() + "\". Ensure Xcode command line tools are installed (xcode-select --install). See README (Troubleshooting)");
      if (rListDeviceTypes.output.size() > 0) {
        logDebug(rListDeviceTypes.output);
      }
      exit(rListDeviceTypes.exitCode);
    }

    std::regex reDeviceType(settings["ios_simulator_device"] + "\\s\\((com.apple.CoreSimulator.SimDeviceType.(?:.+))\\)");
    std::smatch match;

    if (std::regex_search(rListDeviceTypes.output, match, reDeviceType)) {
      deviceType = match.str(1);
      logVerbose("simulator device type: " + deviceType);
    } else {
      auto rListDevices = exec("xcrun simctl list devicetypes");
      logError(
        "failed to find device type: " + settings["ios_simulator_device"] + ". "
        "Please provide correct device name for the \"ios_simulator_device\". "
        "The list of available devices:\n" + rListDevices.output
      );
      if (rListDevices.output.size() > 0) {
        logDebug(rListDevices.output);
      }
      exit(rListDevices.exitCode);
    }

    StringStream listDevicesCommand;

    listDevicesCommand
      << "xcrun"
      << " simctl"
      << " list devices available";

    auto rListDevices = exec(listDevicesCommand.str().c_str());

    if (rListDevices.exitCode != 0) {
      logError("failed to list available devices using \"" + listDevicesCommand.str() + "\". Ensure Xcode command line tools are installed (xcode-select --install). See README (Troubleshooting)");

      if (rListDevices.output.size() > 0) {
        logDebug(rListDevices.output);
      }
      exit(rListDevices.exitCode);
    }

    auto iosSimulatorDeviceSuffix = settings["ios_simulator_device"];
    std::replace(iosSimulatorDeviceSuffix.begin(), iosSimulatorDeviceSuffix.end(), ' ', '_');
    std::regex reSocketSDKDevice("SocketSimulator_" + iosSimulatorDeviceSuffix + "\\s\\((.+)\\)\\s\\((.+)\\)");

    if (std::regex_search(rListDevices.output, match, reSocketSDKDevice)) {
      uuid = match.str(1);
      booted = match.str(2).find("Booted") != String::npos;

      logVerbose("found Socket simulator VM for " + settings["ios_simulator_device"] + " with uuid: " + uuid);
      if (booted) {
        logVerbose("Socket simulator VM is booted");
      } else {
        logVerbose("Socket simulator VM is not booted");
      }
    } else {
      logInfo("creating a new iOS simulator VM for " + settings["ios_simulator_device"]);

      StringStream listRuntimesCommand;
      listRuntimesCommand
        << "xcrun"
        << " simctl"
        << " list runtimes available";
      auto rListRuntimes = exec(listRuntimesCommand.str().c_str());
      if (rListRuntimes.exitCode != 0) {
        logError("failed to list available runtimes using \"" + listRuntimesCommand.str() + "\". Ensure Xcode command line tools are installed (xcode-select --install). See README (Troubleshooting)");
        if (rListRuntimes.output.size() > 0) {
          logDebug(rListRuntimes.output);
        }
        exit(rListRuntimes.exitCode);
      }
      auto const runtimes = split(rListRuntimes.output, '\n');
      String runtime;
      // TODO: improve iOS version detection
      for (auto it = runtimes.rbegin(); it != runtimes.rend(); ++it) {
        if (it->find("iOS") != String::npos) {
          runtime = trim(*it);
          logVerbose("found runtime: " + runtime);
          break;
        }
      }

      std::regex reRuntime(R"(com.apple.CoreSimulator.SimRuntime.iOS(?:.*))");
      std::smatch matchRuntime;
      String runtimeId;

      if (std::regex_search(runtime, matchRuntime, reRuntime)) {
        runtimeId = matchRuntime.str(0);
      }

      StringStream createSimulatorCommand;
      createSimulatorCommand
        << "xcrun simctl"
        << " create SocketSimulator_" + iosSimulatorDeviceSuffix
        << " " << deviceType
        << " " << runtimeId;

      auto rCreateSimulator = exec(createSimulatorCommand.str().c_str());
      if (rCreateSimulator.exitCode != 0) {
        logError("unable to create simulator VM");
        if (rCreateSimulator.output.size() > 0) {
          logDebug(rCreateSimulator.output);
        }
        exit(rCreateSimulator.exitCode);
      }
      uuid = rCreateSimulator.output;
    }
  }

  if (!booted) {
      logInfo("booting VM " + uuid);
    StringStream bootSimulatorCommand;
    bootSimulatorCommand
      << "xcrun"
      << " simctl boot " << uuid;
    auto rBootSimulator = exec(bootSimulatorCommand.str().c_str());
    if (rBootSimulator.exitCode != 0) {
      logError("unable to boot simulator VM with command: " + bootSimulatorCommand.str());
      if (rBootSimulator.output.size() > 0) {
        logDebug(rBootSimulator.output);
      }
      exit(rBootSimulator.exitCode);
    }
  }

  logVerbose("run simulator");

  auto pathToXCode = Path("/Applications") / "Xcode.app" / "Contents";
  auto pathToSimulator = pathToXCode / "Developer" / "Applications" / "Simulator.app";

  StringStream simulatorCommand;
  simulatorCommand
    << "open "
    << pathToSimulator.string()
    << " --args -CurrentDeviceUDID " << uuid;

  auto rOpenSimulator = exec(simulatorCommand.str().c_str());
  if (rOpenSimulator.exitCode != 0) {
    logError("unable to run simulator");
    if (rOpenSimulator.output.size() > 0) {
      logDebug(rOpenSimulator.output);
    }
    exit(rOpenSimulator.exitCode);
  }

  StringStream installAppCommand;

  installAppCommand
    << "xcrun"
    << " simctl install booted "
    << path.string();
  // log(installAppCommand.str());
  logVerbose("installed booted VM into simulator");

  auto rInstallApp = exec(installAppCommand.str().c_str());
  if (rInstallApp.exitCode != 0) {
    logError("unable to install the app into simulator VM with command: " + installAppCommand.str());
    if (rInstallApp.output.size() > 0) {
      logDebug(rInstallApp.output);
    }
    exit(rInstallApp.exitCode);
  }

  StringStream launchAppCommand;
  launchAppCommand
    << "xcrun"
    << " simctl launch --console --terminate-running-process booted"
    << " " + settings["meta_bundle_identifier"];

  env::set("SIMCTL_CHILD_ORO_CLI_PID", std::to_string(getpid()));
  logInfo("launching the app in simulator");

  exit(std::system(launchAppCommand.str().c_str()));
  #endif
}

struct AndroidCliState {
  StringStream adb;
  String androidHome;
  // android, android-emulator
  String targetPlatform;
  // Android SDK package identifier, for example android-37.0.
  String platform;
  StringStream avdmanager;
  String avdName;
  bool oroAvdExists = false;
  bool emulatorRunning = false;
  StringStream emulator;
  Process* androidEmulatorProcess;
  StringStream adbShellStart;
  StringStream adbInstall;
  Path appPath;
  Path apkPath;
  int androidTaskSleepTime = 200;
  int androidTaskTimeout = 120000;

  // should be moved to a general state struct
  String devNull;
  bool verbose = false;
  String quote = "";
  String slash = "";
};

struct Paths {
  Path pathBin;
  Path pathPackage;
  Path pathResourcesRelativeToUserBuild;
  Path platformSpecificOutputPath;
};

// Android build / run functions
bool getAdbPath (AndroidCliState &state) {
  // check that stream is empty, otherwise don't rebuild
  if (state.adb.tellp() == 0) {
    if (!platform.win) {
      state.adb << state.androidHome << "/platform-tools/";
    } else {
      state.adb << state.androidHome << "\\platform-tools\\";
    }

    state.adb << "adb" << (platform.win ? ".exe" : "");
  }

  if (!fs::exists(state.adb.str())) {
    logWarn("Failed to locate adb at " + state.adb.str());
    return false;
  }

  if (state.verbose) logVerbose((state.adb.str() + (" --version ") + state.devNull));
  if (std::system((state.adb.str() + (" --version ") + state.devNull).c_str()) != 0) {
    logWarn("Failed to run adb at " + state.adb.str());
    return false;
  }


  // run adb from androidHome to prevent file lock issues in app build folder on windows
  auto cwd = fs::current_path();
  fs::current_path(state.androidHome);
  auto deviceQuery = exec(state.adb.str() + " devices");
  state.emulatorRunning = (deviceQuery.output.find("emulator") != String::npos);
  fs::current_path(cwd);

  if (env::get("ORO_ANDROID_TIMEOUT").size() > 0) {
    const auto value = env::get("ORO_ANDROID_TIMEOUT");
    state.androidTaskTimeout = std::stoi(value);
    logVerbose("Using ORO_ANDROID_TIMEOUT=" + value);
  }

  return true;
}

bool setupAndroidAvd (AndroidCliState& state) {
  const auto systemImageArch = replace(platform.arch, "arm64", "arm64-v8a");
  String package = state.quote + "system-images;" + state.platform + ";google_apis;" + systemImageArch + state.quote;
  const String cliName(gCliDisplayName);
  state.avdName = "OROAVD_API_" + replace(state.platform, "\\.", "_") + "_" + systemImageArch;

  if (env::get("ANDROID_SDK_MANAGER").size() > 0) {
    state.avdmanager
      << state.androidHome
      << state.slash
      << replace(env::get("ANDROID_SDK_MANAGER"), "sdkmanager", "avdmanager");
  } else {
    state.avdmanager
      << state.androidHome
      << state.slash
      << "cmdline-tools"
      << state.slash
      << "latest"
      << state.slash
      << "bin"
      << state.slash
      << "avdmanager"
      << (platform.win ? ".bat" : "");
  }

  if (!fs::exists(state.avdmanager.str())) {
    logError("failed to locate Android Virtual Device (avdmanager): " + state.avdmanager.str() + ". Run '" + cliName + " setup --platform=android' and ensure ANDROID_HOME is set. See README (Troubleshooting)");
    return false;
  }


  auto avdListResult = exec((state.avdmanager.str() + " list" + state.devNull).c_str());
  if (avdListResult.exitCode != 0) {
    logError("failed to run Android Virtual Device (avdmanager). Run '" + cliName + " setup --platform=android' and ensure SDK tools are installed. See README (Troubleshooting)");
    return false;
  }
  state.oroAvdExists = avdListResult.output.find(state.avdName) != String::npos;

  const auto emulatorPath = (
    state.androidHome +
    state.slash +
    "emulator" +
    state.slash +
    "emulator" +
    (platform.win ? ".exe" : "")
  );

  if (!state.oroAvdExists || !fs::exists(emulatorPath)) {
    StringStream sdkmanager;
    sdkmanager << state.androidHome << state.slash;
    if (env::get("ANDROID_SDK_MANAGER").size() > 0) {
      sdkmanager << env::get("ANDROID_SDK_MANAGER");
    } else {
      sdkmanager
        << "cmdline-tools"
        << state.slash
        << "latest"
        << state.slash
        << "bin"
        << state.slash
        << "sdkmanager"
        << (platform.win ? ".bat" : "");
    }

    if (!fs::exists(sdkmanager.str())) {
      logError("failed to locate Android SDK Manager: " + sdkmanager.str() + ". Run '" + cliName + " setup --platform=android'.");
      return false;
    }

    String installCommand = (
      state.quote +
      sdkmanager.str() +
      state.quote +
      " " +
      state.quote +
      "emulator" +
      state.quote +
      " " +
      package
    );
    logInfo("Installing Android emulator package for " + state.platform + " (" + systemImageArch + ")...");
    if (std::system(installCommand.c_str()) != 0) {
      logError("failed to install the Android emulator package. Accept Android SDK licenses and retry: " + installCommand);
      return false;
    }
  }

  state.avdmanager
    << " create avd "
    << "--device 30 " // use pixel 6 pro, better for promos than --device 5 (desktop large)
    << "--force "
    << "--name " << state.avdName << " "
    << ("--abi google_apis/" + systemImageArch) << " "
    << "--package " << package;

  if (!state.oroAvdExists) {
    auto createResult = exec(state.avdmanager.str());
    if (state.verbose) {
      logVerbose(state.avdmanager.str());
    }
    if (createResult.exitCode != 0) {
      logError("Failed to create " + state.avdName + ": " + createResult.output);
      return false;
    }
  }

  return true;
}

bool locateAndroidEmulator (AndroidCliState& state) {
  state.emulator << state.androidHome << state.slash << "emulator" << state.slash << "emulator" << (platform.win ? ".exe" : "");

  if (state.verbose) logVerbose(state.emulator.str());
  if (!fs::exists(state.emulator.str())) {
    const String cliName(gCliDisplayName);
    logError("failed to locate Android emulator. Run '" + cliName + " setup --platform=android' and ensure ANDROID_HOME/platform-tools are installed. See README (Troubleshooting)");
    return false;
  }

  return true;
}

void setupAndroidStartCommands (AndroidCliState& state, bool flagDebugMode) {
  state.adbInstall << state.adb.str() << " ";
  state.adbInstall << "install ";
  state.adbShellStart << state.adb.str() << " ";

  state.apkPath = state.appPath / "build" / "outputs" / "apk" / (flagDebugMode ? "dev" : "live") / "debug" / (String("app-") + (flagDebugMode ? "dev" : "live") + String("-debug.apk"));
  state.adbInstall << state.apkPath.string();
}

bool startAndroidEmulator (AndroidCliState& state) {
  // start emulator in the background
  int emulatorStartWaited = 0;
  StringStream emulatorOutput;

  logInfo("Starting emulator...");
  state.androidEmulatorProcess = new Process(
    state.emulator.str(),
    " @" + state.avdName + " -gpu swiftshader_indirect",
    state.androidHome,
    [&state, &emulatorOutput](String const& out) {
      if (state.verbose) {
        std::cout << out << std::endl;
      } else {
        emulatorOutput << out << std::endl;
      }
    },
    [&state, &emulatorOutput](String const& out) {
      if (state.verbose) {
        std::cerr << out << std::endl;
      } else {
        emulatorOutput << out << std::endl;
      }
    }
  );
  state.androidEmulatorProcess->open();

  logInfo("Waiting for Android Emulator to boot...");
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    emulatorStartWaited += state.androidTaskSleepTime;

    if (std::system((state.adb.str() + " shell getprop sys.boot_completed" + state.devNull).c_str()) == 0) {
      logInfo("OK.");
      return true;
    } else if (state.androidEmulatorProcess->closed) {
      logWarn("Emulator exited with code " + std::to_string(state.androidEmulatorProcess->status));
      break;
    } else {
      if (emulatorStartWaited >= state.androidTaskTimeout) {
        logError("Emulator start timed out.");
        break;
      }
    }
  }

  if (state.androidEmulatorProcess->status != 0 && !state.verbose) {
    std::cerr << emulatorOutput.str();
  }

  return false;
}

/// <summary>
/// Handles emulator start state by reattempting install until successful.
/// Times out after 120s by default.
/// </summary>
/// <param name="state"></param>
/// <returns>True if the app was installed, otherwise false.</returns>
bool installAndroidApp (AndroidCliState& state) {
  ExecOutput adbInstallOutput;
  int adbInstallWaited = 0;

  if (!fs::exists(state.apkPath)) {
    logError("APK doesn't exist: " + state.apkPath.string());
    return false;
  }

  if (state.verbose) {
    logVerbose(state.adbInstall.str());
  }

  while (true) {
    // handle emulator boot issue: cmd: Can't find service: package, no reliable way of detecting when emulator is ready without a blocking logcat call
    // Note that there are several different errors that can occur here based on the state of the emulator, just keep trying to install with a timeout
    adbInstallOutput = exec(state.adbInstall.str() + " 2>&1");
    if (adbInstallOutput.exitCode != 0) {
      if (state.targetPlatform == "android") {
        // not waiting for emulator to boot, break immediately
        break;
      }
      if (adbInstallWaited >= state.androidTaskTimeout) {
        logError("Wait for ADB Install timed out.");
        break;
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(state.androidTaskSleepTime));
        adbInstallWaited += state.androidTaskSleepTime;
      }
    } else {
      break;
    }
  }

  String target = state.targetPlatform == "android" ? "device" : "Emulator";

  if (adbInstallOutput.exitCode != 0) {
    if (!state.verbose) {
      // log command for user debug purposes
      logDebug(state.adbInstall.str());
    }

    logDebug(adbInstallOutput.output);
    logError("failed to install APK on Android " + target + " (adb)");
    std::cout << "Ensure your " << target << " serial number appears (without UNAUTHORIZED) when running" << std::endl;
    std::cout << state.adb.str() << " devices" << std::endl;
    return false;
  }

  return true;
}

bool startAndroidApp (AndroidCliState& state) {
  ExecOutput adbInstallOutput;
  int adbStartWaited = 0;

  auto mainActivity = settings["android_main_activity"];
  if (mainActivity.size() == 0) {
    mainActivity = String(DEFAULT_ANDROID_MAIN_ACTIVITY_NAME);
  }

  state.adbShellStart << "shell am start -n " << settings["android_bundle_identifier"] << "/" << settings["android_bundle_identifier"] << mainActivity << " 2>&1";
  if (state.verbose) logVerbose(state.adbShellStart.str());
  ExecOutput adbShellStartOutput;
  while (true) {
    adbShellStartOutput = exec(state.adbShellStart.str());
    if (adbShellStartOutput.output.find("Error type 3") != String::npos) {
      // ignore this timing related startup error
      std::this_thread::sleep_for(std::chrono::milliseconds(state.androidTaskSleepTime));
      adbStartWaited += state.androidTaskSleepTime;
    } else {
      logInfo("App started.");
      break;
    }
    if (adbStartWaited >= state.androidTaskTimeout) {
      logError("Wait for shell start timed out.");
      break;
    }
  }

  if (adbShellStartOutput.exitCode != 0) {
    logError("Failed to run app on emulator.");
    return false;
  }

  return true;
}

bool initAndStartAndroidEmulator (AndroidCliState& state) {
  if (!setupAndroidAvd(state)) {
    return false;
  }

  if (!locateAndroidEmulator(state)) {
    return false;
  }

      if (!startAndroidEmulator(state)) {
        return false;
      }

  return true;
}

bool initAndStartAndroidApp (AndroidCliState& state, bool flagDebugMode) {
  setupAndroidStartCommands(state, flagDebugMode);

  logInfo("Installing app...");
  if (!installAndroidApp(state)) {
    return false;
  }

  logInfo("Starting app...");
  if (!startAndroidApp(state)) {
    return false;
  }

  return true;
}

static String getCxxFlags () {
  auto flags = env::get("CXXFLAGS");
  return flags.size() > 0 ? " " + flags : "";
}

static bool macCompilerSupportsOpenMP () {
  static std::optional<bool> cached;

  if (cached.has_value()) {
    return *cached;
  }

  try {
    auto compiler = env::get("CXX");
    if (compiler.empty()) {
      compiler = "/usr/bin/clang++";
    }

    const auto token = std::to_string(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()
    );
    const auto sourcePath = fs::temp_directory_path() / ("oroc-openmp-check-" + token + ".cc");
    const auto outputPath = fs::temp_directory_path() / ("oroc-openmp-check-" + token);

    {
      std::ofstream source(sourcePath);
      source << "int main() { return 0; }\n";
    }

    const auto command =
      compiler +
      " -fopenmp \"" + sourcePath.string() +
      "\" -o \"" + outputPath.string() +
      "\" >/dev/null 2>&1";

    cached = exec(command.c_str()).exitCode == 0;

    std::error_code ec;
    fs::remove(sourcePath, ec);
    fs::remove(outputPath, ec);
  } catch (const fs::filesystem_error&) {
    cached = false;
  }

  return *cached;
}

inline String getCfgUtilPath () {
  const bool hasCfgUtilInPath = exec("command -v cfgutil").exitCode == 0;
  if (hasCfgUtilInPath) {
    return "cfgutil";
  }
  const bool hasCfgUtil = fs::exists("/Applications/Apple Configurator.app/Contents/MacOS/cfgutil");
  if (hasCfgUtil) {
    logWarn("We highly recommend to install Automation Tools from the Apple Configurator main menu.");
    return "/Applications/Apple\\ Configurator.app/Contents/MacOS/cfgutil";
  }
  logError("Please install Apple Configurator from https://apps.apple.com/us/app/apple-configurator/id1037126344");
  exit(1);
}

void initializeEnv (Path targetPath, bool allowMirror = false) {
  (void) allowMirror;
  static const auto envFileName = []() {
    auto preferred = env::get("ORO_ENV_FILENAME");
    if (preferred.size() == 0) {
      preferred = DEFAULT_ORO_ENV_FILENAME;
    }
    return preferred;
  }();

  const auto pathToRead = targetPath / envFileName;
  if (!dotfileExists(pathToRead)) {
    return;
  }

  auto parsedEnv = INI::parse(readFile(pathToRead));
  for (const auto& tuple : parsedEnv) {
    auto key = tuple.first;
    auto value = tuple.second;
    auto valueAsPath = Path(value).make_preferred();

    // convert env value to normalized path if it exists
    if (fs::exists(fs::status(valueAsPath))) {
      value = valueAsPath.string();
    }

    if (!env::has(key)) {
      env::set(key, value);
    }
  }
}

void initializeRC (Path targetPath, bool allowMirror = false) {
  (void) allowMirror;
  static const auto RC_OVERRIDE = []() {
    auto overridePath = env::get("ORO_RC");
    return overridePath;
  }();
  static const auto PREFERRED_RC_NAME = []() {
    auto preferred = env::get("ORO_RC_FILENAME");
    if (preferred.size() == 0) {
      preferred = DEFAULT_ORO_RC_FILENAME;
    }
    return preferred;
  }();

  Path pathToRead;
  if (RC_OVERRIDE.size() > 0) {
    pathToRead = Path(RC_OVERRIDE);
    if (!dotfileExists(pathToRead)) {
      logWarn("rc override '" + pathToRead.string() + "' not found; skipping");
      return;
    }
  } else {
    pathToRead = targetPath / PREFERRED_RC_NAME;
    if (!dotfileExists(pathToRead)) {
      return;
    }
  }

  extendMap(rc, INI::parse(tmpl(readFile(pathToRead), Map<> {
    {"platform.arch", platform.arch},
    {"platform.arch.short", replace(platform.arch, "x86_64", "x64")},
    {"platform.os", platform.os},
    {"platform.os.short", replace(platform.os, "win32", "win")}
  })));

  for (const auto& tuple : rc) {
    auto key = tuple.first;
    auto value = tuple.second;
    auto valueAsPath = Path(value).make_preferred();

    // convert env value to normalized path if it exists
    if (fs::exists(fs::status(valueAsPath))) {
      value = valueAsPath.string();
    }

    if (key.starts_with("settings_")) {
      const auto k = key.substr(9, key.size() - 9);
      settings[k] = value;
    }

    // auto set environment variables
    if (key.starts_with("env_")) {
      settings[key] = value;
      key = key.substr(4, key.size() - 4);
      env::set(key, value);
    }
  }
}

bool isSetupCompleteAndroid () {
  auto androidHome = getAndroidHome();
  if (androidHome.size() == 0) {
    return false;
  }

  if (!fs::exists(androidHome)) {
    return false;
  }

  Path sdkManager = androidHome + "/" + env::get("ANDROID_SDK_MANAGER");

  if (!fs::exists(sdkManager)) {
    return false;
  }

  if (!fs::exists(env::get("JAVA_HOME"))) {
    return false;
  }

  return true;
}

bool isSetupCompleteWindows () {
  if (env::get("CXX").size() == 0) {
    return false;
  }

  return fs::exists(env::get("CXX"));
}

bool isSetupComplete (String platform) {
  std::map<String, bool(*)()> funcs;

  funcs["android"] = isSetupCompleteAndroid;
  funcs["windows"] = isSetupCompleteWindows;

  if (funcs.count(platform) == 0) return true;

  return funcs[platform]();
}

void run (const String& targetPlatform, Map<>& settings, const Paths& paths, const bool& flagDebugMode, const bool& flagRunHeadless, const String& argvForward, AndroidCliState& androidState) {
  if (targetPlatform == "ios-simulator") {
    String app = (settings["build_name"] + ".app");
    auto pathToApp = paths.platformSpecificOutputPath / app;
    runIOSSimulator(pathToApp, settings);
  } else if (targetPlatform == "android" || targetPlatform == "android-emulator") {
    if (!getAdbPath(androidState)) {
      exit(1);
    }

    if (targetPlatform == "android-emulator" && !androidState.emulatorRunning) {
      if (!initAndStartAndroidEmulator(androidState)) {
        exit(1);
      }
    }

    if (!initAndStartAndroidApp(androidState, flagDebugMode)) {
      exit(1);
    }

    exit(0);
  } else {
    auto executable = Path(settings["build_name"] + (platform.win ? ".exe" : ""));
    auto pathToExecutable = (paths.pathBin / executable).string();
    auto exitCode = runApp(pathToExecutable, argvForward, flagRunHeadless);
    return exit(exitCode);
  }

  // Unreachable fallback; keep a clear error for safety.
  logError("App failed to run, please contact support.");
  exit(1);
}

struct CommandLineOption {
  std::vector<String> aliases;
  bool isOptional;
  bool shouldHaveValue;
};

using CommandLineOptions = Vector<CommandLineOption>;

struct optionsAndEnv {
  Map<> optionsWithValue;
  std::unordered_set<String> optionsWithoutValue;
  Vector<String> envs;
};

static bool commandDeclaresOptionAlias (
  const CommandLineOptions& availableOptions,
  const String& alias
) {
  for (const auto& option : availableOptions) {
    for (const auto& candidate : option.aliases) {
      if (equal(candidate, alias)) {
        return true;
      }
    }
  }

  return false;
}

static String resolveCommandOutputFormat (
  const String& commandName,
  const Map<>& optionsWithValue,
  const std::unordered_set<String>& optionsWithoutValue,
  const std::initializer_list<String>& supportedFormats,
  const String& defaultFormat
) {
  auto requested = defaultFormat;
  const auto formatIt = optionsWithValue.find("--format");
  const bool hasExplicitFormat =
    formatIt != optionsWithValue.end() &&
    formatIt->second.size() > 0;

  if (hasExplicitFormat) {
    requested = toLowerCase(formatIt->second);
  }

  const bool jsonShortcut =
    optionsWithoutValue.find("--json") != optionsWithoutValue.end();

  if (jsonShortcut) {
    if (hasExplicitFormat && !equal(requested, "json")) {
      logError(
        commandName + ": '--json' cannot be combined with '--format=" +
        formatIt->second + "'" + helpHint(commandName)
      );
      exit(1);
    }

    requested = "json";
  }

  if (requested.size() == 0) {
    return requested;
  }

  bool supported = false;
  String supportedList;
  size_t supportedIndex = 0;
  for (const auto& candidate : supportedFormats) {
    if (supportedIndex++ > 0) {
      supportedList += ", ";
    }
    supportedList += "'" + candidate + "'";
    if (equal(requested, candidate)) {
      supported = true;
    }
  }

  if (!supported) {
    logError(
      commandName + ": unsupported format '" + requested +
      "'; supported formats are " + supportedList + helpHint(commandName)
    );
    exit(1);
  }

  if (equal(requested, "json") && gLogJson) {
    logError(
      commandName +
      ": structured command output cannot be combined with global JSON logs. "
      "Unset ORO_LOG_JSON or remove the global '--json' flag, and use "
      "'--log-file=<path>' if you also need logs." + helpHint(commandName)
    );
    exit(1);
  }

  return requested;
}

optionsAndEnv parseCommandLineOptions (
  const std::span<const char*>& options,
  const CommandLineOptions& availableOptions,
  const String& subcommand
) {
  optionsAndEnv result;
  Map<> optionsWithValue;
  std::unordered_set<String> optionsWithoutValue;
  std::vector<String> envs;
  const bool allowMissingTarget = equal(subcommand, "init");
  const bool subcommandOwnsJsonFlag =
    commandDeclaresOptionAlias(availableOptions, "--json");

  for (size_t i = 0; i < options.size(); i++) {
    String arg = options[i];
    size_t equalPos = arg.find('=');
    String key;
    String value;

    if (arg == "-h" || arg == "--help") {
      printHelp(subcommand);
      exit(0);
    }

    if (equal(arg, "--verbose") || equal(arg, "-V")) {
      flagVerboseMode = true;
      env::set("ORO_VERBOSE", "1");
      updateLogLevelFromEnv();
      continue;
    }

    if (equal(arg, "--debug") || equal(arg, "-D")) {
      env::set("ORO_DEBUG", "1");
      updateLogLevelFromEnv();
      continue;
    }

    if (equal(arg, "--quiet") || equal(arg, "-q")) {
      flagQuietMode = true;
      continue;
    }

    if (equal(arg, "--no-color")) {
      env::set("ORO_LOG_NO_COLOR", "1");
      continue;
    }

    if (equal(arg, "--json") && !subcommandOwnsJsonFlag) {
      if (!gMcpStdioMode) {
        flagJsonMode = true;
        env::set("ORO_LOG_JSON", "1");
        updateLogJsonFromEnv();
      }
      continue;
    }

    if (arg.rfind("--log-file=", 0) == 0) {
      value = arg.substr(String("--log-file=").size());
      if (value.size() > 0) {
        gLogFilePath = value;
        env::set("ORO_LOG_FILE", value);
      }
      continue;
    }

    bool hasEqualSignDelimiter = equalPos != String::npos;
    // Option in the form "--key=value" or "-k=value"
    if (hasEqualSignDelimiter) {
      key = arg.substr(0, equalPos);
      value = arg.substr(equalPos + 1);
    } else {
      key = arg;
    }

    // path or positional argument
    if (key.size() && !key.starts_with("-")) {
      if (equal(subcommand, "config")) {
        // For `config`, treat the first bare argument as a key/query
        // instead of a filesystem target so `oroc config <key>` works.
        if (optionsWithValue.count("--key") == 0 && optionsWithValue.count("--describe") == 0) {
          optionsWithValue["--key"] = key;
        } else {
          logError("too many positional arguments for 'config'; use '--key' or '--describe' for additional queries");
          printHelp(subcommand);
          exit(1);
        }
      } else if (equal(subcommand, "version")) {
        // For `version`, treat a single bare argument as the new version
        // spec (e.g., `major`, `minor`, `1.2.3`, etc.) instead of a
        // filesystem target path.
        if (optionsWithValue.count("--new-version") == 0) {
          optionsWithValue["--new-version"] = key;
        } else {
          logError("too many positional arguments for 'version'; expected at most one version spec");
          printHelp(subcommand);
          exit(1);
        }
      } else if (equal(subcommand, "versions")) {
        // For `versions`, treat a single bare argument as the dependency
        // name to filter on (e.g., `uv`, `sqlite`, `iroh`) rather than a
        // filesystem target path.
        if (optionsWithValue.count("--dependency") == 0) {
          optionsWithValue["--dependency"] = key;
        } else {
          logError("too many positional arguments for 'versions'; expected at most one dependency name");
          printHelp(subcommand);
          exit(1);
        }
      } else if (equal(subcommand, "help")) {
        if (optionsWithValue.count("--query") > 0 && optionsWithValue["--query"].size() > 0) {
          optionsWithValue["--query"] += " ";
        }
        optionsWithValue["--query"] += key;
      } else {
        assignTargetFromArgument(key, allowMissingTarget);
      }
      value = "";
      key = "";
      continue;
    }

    // find option
    CommandLineOption recognizedOption;
    bool found = false;
    for (const auto option : availableOptions) {
      for (const auto alias : option.aliases) {
        if (alias == key) {
          recognizedOption = option;
          found = true;
          key = recognizedOption.aliases[0];
          if (!recognizedOption.shouldHaveValue && value.size() > 0) {
            logError("option '" + key + "' does not require a value");
            printHelp(subcommand);
            exit(1);
          }
          if (!hasEqualSignDelimiter && recognizedOption.shouldHaveValue) {
            // Option in the form "--key value" or "-k value"
            if (i + 1 < options.size() && options[i + 1][0] != '-') {
              value = options[++i];
            // Option in the form "--key" or "-k"
            } else {
              value = "";
              if (i + 1 < options.size() && options[i + 1][0] != '-') {
                assignTargetFromArgument(options[i + 1], false);
              }
            }
          }
          if (!recognizedOption.isOptional && value.empty()) {
            logError("option '" + key + "' requires a value");
            printHelp(subcommand);
            exit(1);
          }
          break;
        }
      }
      if (found) break;
    }

    if (!found) {
      logError("unrecognized option '" + key + "'");
      printHelp(subcommand);
      exit(1);
    }

    const bool isMultiValueOption = equal(key, "--copy");
    if (!isMultiValueOption &&
        (optionsWithValue.count(key) > 0 || optionsWithoutValue.find(key) != optionsWithoutValue.end())) {
      logError("option '" + key + "' is used more than once");
      printHelp(subcommand);
      exit(1);
    }

    if (value.size() == 0) {
      value = rc[subcommand + "_" + key];
    }

    if (equal(key, "--copy") && value.size() > 0) {
      auto parts = split(value, ':');
      if (parts.size() == 2 && parts[0].size() > 0 && parts[1].size() > 0) {
        value = parts[0] + "=" + parts[1];
      }
    }

    if (equal(key, "--env")) {
      if (value.size() > 0) {
        // An explicit assignment may contain whitespace in its value. Only
        // parse a list when the argument contains environment variable names.
        const auto parts = value.find('=') == String::npos
          ? parseStringList(value)
          : Vector<String> {value};
        for (const auto& part : parts) {
          envs.push_back(part);
        }
      }
      continue;
    }

    if (!value.empty()) {
      if (isMultiValueOption && optionsWithValue.count(key) > 0) {
        if (optionsWithValue[key].size() > 0) {
          optionsWithValue[key] += ";";
        }
        optionsWithValue[key] += value;
      } else {
        optionsWithValue[key] = value;
      }
    } else {
      optionsWithoutValue.insert(key);
    }

    if (equal(key, "--log-file")) {
      if (value.size() > 0) {
        gLogFilePath = value;
        env::set("ORO_LOG_FILE", value);
      }
      continue;
    }
  }

  if (targetPath.empty()) {
    targetPath = fs::current_path();
  }

  result.optionsWithValue = optionsWithValue;
  result.optionsWithoutValue = optionsWithoutValue;
  result.envs = envs;

  for (const auto& requiredOption : availableOptions) {
    if (requiredOption.isOptional) {
      continue;
    }
    bool isMissing = optionsWithValue.count(requiredOption.aliases[0]) == 0 || optionsWithoutValue.find(requiredOption.aliases[0]) != optionsWithoutValue.end();
    if (!requiredOption.isOptional && isMissing) {
      logError("required option '" + requiredOption.aliases[0] + "' is missing");
      printHelp(subcommand);
      exit(1);
    }
  }

  return result;
}

int main (int argc, char* argv[]) {
  try {
    if (argc > 0 && argv == nullptr) {
      logError("invalid null argument vector");
      return 1;
    }

    const String cliInvocationPath = (argc > 0 && argv != nullptr && argv[0] != nullptr)
      ? String(argv[0])
      : String("oroc");

    const auto maybeScopeRuntimeHomeForMcp = [&]() {
      if (env::get("ORO_HOME").size() > 0) return;
      if (argc < 2 || argv == nullptr) return;

      int subIndex = 1;
      while (subIndex < argc) {
        String a = argv[subIndex];
        if (equal(a, "--verbose") || equal(a, "-V")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--debug") || equal(a, "-D")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--quiet") || equal(a, "-q")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--no-color")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--json")) {
          subIndex++;
          continue;
        }
        if (a.rfind("--log-file=", 0) == 0) {
          subIndex++;
          continue;
        }
        break;
      }

      if (subIndex >= argc) return;
      if (!equal(String(argv[subIndex]), "mcp")) return;

      fs::path workspaceRoot = fs::current_path();
      static const std::unordered_set<String> valueOptions = {
        "--host",
        "--port",
        "--endpoint",
        "--token",
        "--workspace",
        "--config"
      };

      for (int i = subIndex + 1; i < argc; i++) {
        String a = argv[i];

        if (a.rfind("--workspace=", 0) == 0) {
          workspaceRoot = fs::absolute(a.substr(String("--workspace=").size())).lexically_normal();
          break;
        }

        if (valueOptions.find(a) != valueOptions.end()) {
          if (i + 1 >= argc) break;
          if (a == "--workspace") {
            workspaceRoot = fs::absolute(String(argv[i + 1])).lexically_normal();
            break;
          }
          i++;
          continue;
        }

        if (!a.empty() && a.front() == '-') {
          continue;
        }

        // Positional workspace root (if provided).
        workspaceRoot = fs::absolute(a).lexically_normal();
        break;
      }

      const auto mcpHome = (workspaceRoot / ".oro_home").lexically_normal();
      env::set("ORO_HOME", mcpHome.string());
    };

    maybeScopeRuntimeHomeForMcp();

    const bool shouldForceMcpStdio = [&]() -> bool {
      if (argc < 2 || argv == nullptr) return false;

      int subIndex = 1;
      while (subIndex < argc) {
        String a = argv[subIndex];
        if (equal(a, "--verbose") || equal(a, "-V")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--debug") || equal(a, "-D")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--quiet") || equal(a, "-q")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--no-color")) {
          subIndex++;
          continue;
        }
        if (equal(a, "--json")) {
          subIndex++;
          continue;
        }
        if (a.rfind("--log-file=", 0) == 0) {
          subIndex++;
          continue;
        }
        break;
      }

      if (subIndex >= argc) return false;
      if (!equal(String(argv[subIndex]), "mcp")) return false;

      for (int i = subIndex + 1; i < argc; i++) {
        if (equal(String(argv[i]), "--http")) {
          return false;
        }
      }

      return true;
    }();

    if (shouldForceMcpStdio) {
      gMcpStdioMode = true;
      flagQuietMode = true;
      flagJsonMode = false;
      gLogJson = false;
      gLogFilePath = "";
      env::set("ORO_LOG_JSON", "");
      env::set("ORO_LOG_FILE", "");
      gLogLevel = CliLogLevel::WARN;
    }

    updateLogLevelFromEnv();
    updateLogJsonFromEnv();
    updateLogFileFromEnv();

    if (gMcpStdioMode) {
      flagQuietMode = true;
      flagJsonMode = false;
      gLogJson = false;
      gLogFilePath = "";
      env::set("ORO_LOG_JSON", "");
      env::set("ORO_LOG_FILE", "");
      gLogLevel = CliLogLevel::WARN;
    }
    defaultTemplateAttrs = {
      { "cli_version", VERSION_FULL_STRING },
      { "project_name", "beepboop" },
      { "cli_name", "oroc" }
    };

    if (argc < 2) {
      printHelp(gCliDisplayName);
      exit(0);
    }

    // Support global flags before subcommand (e.g., `oroc --quiet --debug build`)
    int offset = 1;
    while (offset < argc) {
      String a = argv[offset];
      if (equal(a, "--verbose") || equal(a, "-V")) {
        if (!gMcpStdioMode) {
          flagVerboseMode = true;
          env::set("ORO_VERBOSE", "1");
          updateLogLevelFromEnv();
        }
        offset++;
        continue;
      }
      if (equal(a, "--debug") || equal(a, "-D")) {
        if (!gMcpStdioMode) {
          env::set("ORO_DEBUG", "1");
          updateLogLevelFromEnv();
        }
        offset++;
        continue;
      }
      if (equal(a, "--quiet") || equal(a, "-q")) {
        flagQuietMode = true;
        offset++;
        continue;
      }
      if (equal(a, "--no-color")) {
        env::set("ORO_LOG_NO_COLOR", "1");
        offset++;
        continue;
      }
      if (equal(a, "--json")) {
        if (!gMcpStdioMode) {
          flagJsonMode = true;
          env::set("ORO_LOG_JSON", "1");
          updateLogJsonFromEnv();
        }
        offset++;
        continue;
      }
      if (a.rfind("--log-file=", 0) == 0) {
        if (!gMcpStdioMode) {
          auto value = a.substr(String("--log-file=").size());
          if (value.size() > 0) {
            gLogFilePath = value;
            env::set("ORO_LOG_FILE", value);
          }
        }
        offset++;
        continue;
      }
      break;
    }

    if (offset >= argc) {
      printHelp(gCliDisplayName);
      exit(0);
    }

    argc -= (offset - 1);
    argv += (offset - 1);

    String rawSubcommand = argv[1];
    String effectiveSubcommand = rawSubcommand;
    int firstOptionIndex = 2;

    if (rawSubcommand.rfind("update-", 0) == 0 && !equal(rawSubcommand, "update")) {
      logError(
        "subcommand '" + String(gCliDisplayName) + " " + rawSubcommand +
        "' has been replaced by 'update <subcommand>'"
      );
      printHelp("update");
      exit(1);
    }

    if (equal(rawSubcommand, "update")) {
      if (argc < 3) {
        printHelp("update");
        exit(0);
      }

      String nested = argv[2];
      // Treat common help flags as a request for update help rather than as a
      // nested subcommand name so `oroc update -h|--help|help` behaves like a
      // standard help invocation and exits with status 0.
      if (equal(nested, "-h") || equal(nested, "--help") || equal(nested, "help")) {
        printHelp("update");
        exit(0);
      }
      bool recognized = false;

      auto mapNested = [&](const String& name) -> String {
        return String("update-") + name;
      };

      if (equal(nested, "keygen")) {
        effectiveSubcommand = mapNested("keygen");
        recognized = true;
      } else if (equal(nested, "server")) {
        effectiveSubcommand = mapNested("server");
        recognized = true;
      } else if (equal(nested, "info")) {
        effectiveSubcommand = mapNested("info");
        recognized = true;
      } else if (equal(nested, "sign")) {
        effectiveSubcommand = mapNested("sign");
        recognized = true;
      } else if (equal(nested, "verify")) {
        effectiveSubcommand = mapNested("verify");
        recognized = true;
      } else if (equal(nested, "bundle")) {
        effectiveSubcommand = mapNested("bundle");
        recognized = true;
      } else if (equal(nested, "extract")) {
        effectiveSubcommand = mapNested("extract");
        recognized = true;
      } else if (equal(nested, "init")) {
        effectiveSubcommand = mapNested("init");
        recognized = true;
      } else if (equal(nested, "validate")) {
        effectiveSubcommand = mapNested("validate");
        recognized = true;
      }

      if (!recognized) {
        logError("unknown update subcommand '" + nested + "'");
        printHelp("update");
        exit(1);
      }

      firstOptionIndex = 3;
    }

    gLogSubcommand = effectiveSubcommand;
    gLogArgv.clear();
    for (int i = firstOptionIndex; i < argc; i++) {
      gLogArgv.push_back(String(argv[i]));
    }

  #ifndef _WIN32
    signal(SIGHUP, signalHandler);
    signal(SIGUSR1, signalHandler);
    signal(SIGUSR2, signalHandler);
  #endif

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (equal(effectiveSubcommand, "-v") || equal(effectiveSubcommand, "--version")) {
    std::cout << VERSION_FULL_STRING << std::endl;
    if (flagVerboseMode || env::get("ORO_DEBUG").size() > 0) {
      std::cerr << "Installation path: " << getHomeHome() << std::endl;
    }
    exit(0);
  }

  if (equal(effectiveSubcommand, "-h") || equal(effectiveSubcommand, "--help")) {
    printHelp(gCliDisplayName);
    exit(0);
  }

  if (equal(effectiveSubcommand, "help")) {
    CommandLineOptions helpOptions = {
      { { "--json" }, true, false },
      { { "--log-file" }, true, true }
    };

    const auto parsed = parseCommandLineOptions(
      std::span(const_cast<const char**>(argv), argc).subspan(firstOptionIndex, argc - firstOptionIndex),
      helpOptions,
      "help"
    );

    const auto outputFormat = resolveCommandOutputFormat(
      "help",
      parsed.optionsWithValue,
      parsed.optionsWithoutValue,
      { "text", "json" },
      "text"
    );

    const auto query = trim(parsed.optionsWithValue.count("--query") > 0
      ? parsed.optionsWithValue.at("--query")
      : String(""));
    const auto* exactDoc = query.size() > 0 ? findCliHelpDocByQuery(query) : nullptr;

    if (equal(outputFormat, "json")) {
      if (query.size() == 0) {
        printCliHelpSearchJson(query, listCliHelpCatalog(), false, renderCliHelpText("oroc"));
        exit(0);
      }

      if (exactDoc != nullptr) {
        const auto normalizedQuery = normalizeHelpSearchText(query);
        const auto queryTokens = split(normalizedQuery, ' ');
        Vector<CliHelpMatch> exactMatches;
        exactMatches.push_back(buildCliHelpMatch(
          *exactDoc,
          renderCliHelpText(*exactDoc),
          normalizedQuery,
          queryTokens,
          1000,
          true
        ));

        printCliHelpSearchJson(
          query,
          exactMatches,
          true
        );
        exit(0);
      }

      const auto matches = searchCliHelp(query);
      printCliHelpSearchJson(query, matches, false);
      exit(matches.empty() ? 1 : 0);
    }

    if (query.size() == 0) {
      printHelp("oroc");
      exit(0);
    }

    if (exactDoc != nullptr) {
      printHelp(exactDoc->command);
      exit(0);
    }

    const auto matches = searchCliHelp(query);
    printCliHelpSearchResults(query, matches);
    exit(matches.empty() ? 1 : 0);
  }

  if (equal(effectiveSubcommand, "--prefix")) {
    std::cout << getHomeHome() << std::endl;
    exit(0);
  }

  if (effectiveSubcommand[0] == '-') {
    logError("unknown option: " + effectiveSubcommand + helpHint(""));
    printHelp(gCliDisplayName);
    exit(1);
  }

  // note: lastOption was unused; removing to avoid warnings
  const int numberOfOptions = argc - firstOptionIndex;

#if defined(_WIN32)
  static String HOME = env::get("HOMEPATH");
#else
  static String HOME = env::get("HOME");
#endif

  // `/etc/.ororc` (global)
  initializeRC(Path("/etc"));
  // `/etc/oro/.ororc` (global)
  initializeRC(Path("/etc") / "oro");
  // `/etc/oro/config/.ororc` (global)
  initializeRC(Path("/etc") / "oro" / "config");
  // `$HOME/.oro/config/.ororc` (user)
  initializeRC(Path(HOME)  / ".oro" / "config");
  // `$HOME/.config/oro/.ororc` (user)
  initializeRC(Path(HOME)  / ".config" / "oro");
  // `$HOME/.ororc` (user)
  initializeRC(HOME);
  // `$PWD/.ororc` (local)
  initializeRC(targetPath);

  // `$ORO_HOME/.oro.env` (global/user)
  initializeEnv(prefixFile());
  // `$PWD/.oro.env` (local)
  initializeEnv(targetPath); // overrides global config with local

  auto getPaths = [&](String platform) -> Paths {
    Paths paths;
    String platformPath = platform == "win32" ? "win" : platform;

    paths.platformSpecificOutputPath = {
      targetPath /
      settings["build_output"] /
      platformPath
    };

    if (platform == "mac") {
      Path pathBase = "Contents";
      Path packageName = Path(settings["build_name"] + ".app");
      paths.pathPackage = { paths.platformSpecificOutputPath  / packageName };
      paths.pathBin = { paths.pathPackage / pathBase / "MacOS" };
      paths.pathResourcesRelativeToUserBuild = {
        paths.platformSpecificOutputPath  /
        packageName /
        pathBase /
        "Resources"
      };
      return paths;
    } else if (platform == "linux") {
      settings["meta_revision"] = "1";

      // this follows the .deb file naming convention
      Path packageName = (
        settings["build_name"] + "_" +
        "v" + settings["meta_version"] + "-" +
        settings["meta_revision"] + "_" +
        replace(runtime::platform.arch, "x86_64", "amd64")
      );
      paths.pathPackage = { paths.platformSpecificOutputPath  / packageName };
      paths.pathBin = {
        paths.pathPackage /
        "opt" /
        settings["build_name"]
      };
      paths.pathResourcesRelativeToUserBuild = paths.pathBin;
      return paths;
    } else if (platform == "win32") {
      paths.pathPackage = {
        paths.platformSpecificOutputPath  /
        Path(settings["build_name"] + "-v" + settings["meta_version"])
      };

      paths.pathBin = paths.pathPackage;
      paths.pathResourcesRelativeToUserBuild = paths.pathPackage;
      return paths;
    } else if (platform == "ios" || platform == "ios-simulator") {
      Path pathBase = "Contents";
      Path packageName = settings["build_name"] + ".app";
      paths.pathPackage = { paths.platformSpecificOutputPath  / packageName };
      paths.pathBin = { paths.platformSpecificOutputPath / pathBase / "MacOS" };
      paths.pathResourcesRelativeToUserBuild = paths.platformSpecificOutputPath / "ui";
      return paths;
    } else if (platform == "android" || platform == "android-emulator") {
      auto output = fs::absolute(targetPath) / paths.platformSpecificOutputPath;
      paths.pathResourcesRelativeToUserBuild = {
        paths.platformSpecificOutputPath / "app" / "src" / "main" / "assets"
      };
      return paths;
    }
    logError("unknown platform: " + platform);
    exit(1);
  };

  auto createSubcommand = [&](
    const String& subcommand,
    const CommandLineOptions& availableOptions,
    const bool& needsConfig,
    Function<void(Map<>, UnorderedSet<String>)> subcommandHandler
  ) -> void {
    if (effectiveSubcommand == subcommand) {
      auto commandlineOptions = std::span(const_cast<const char**>(argv), argc).subspan(firstOptionIndex, numberOfOptions);
      auto optionsAndEnv = parseCommandLineOptions(commandlineOptions, availableOptions, subcommand);

      auto& optionsWithValue = optionsAndEnv.optionsWithValue;
      auto& optionsWithoutValue = optionsAndEnv.optionsWithoutValue;
      auto envs = optionsAndEnv.envs;

      if (needsConfig) {
        auto configExists = false;
        auto configInferred = false;
        auto code = String("");
        auto configDocument = String("");
        auto inferredEntry = String("");
        Map<> inferredOverrides;
        UserConfigFormat configFormat = UserConfigFormat::Ini;
        Path configPath;
        const auto preferredConfigPath = targetPath / "oro.toml";
        const auto oroIniConfigPath = targetPath / "oro.ini";

        if (optionsWithValue.count("--config") > 0) {
          auto provided = fs::absolute(optionsWithValue["--config"]).lexically_normal();
          if (!fileExists(provided)) {
            logError("--config expects a path to an existing oro.toml or oro.ini file");
            exit(1);
          }
          configPath = provided;
          const auto configFilenameLower = toLowerCase(configPath.filename().string());
          if (configFilenameLower != "oro.toml" && configFilenameLower != "oro.ini") {
            logError("--config expects a path to an existing oro.toml or oro.ini file");
            exit(1);
          }
          configFormat = detectConfigFormatForPath(configPath);
          targetPath = configPath.parent_path();
          targetArgumentPath = configPath;
          targetSourcePath.clear();
          optionsWithValue["--config"] = configPath.string();
        } else {
          const bool hasPreferred = fileExists(preferredConfigPath);
          const bool hasOroIni = fileExists(oroIniConfigPath);

          if (hasPreferred && hasOroIni) {
            logWarn("detected both oro.toml and oro.ini; defaulting to oro.toml");
          }

          if (hasPreferred) {
            configPath = preferredConfigPath;
            configFormat = UserConfigFormat::Toml;
          } else if (hasOroIni) {
            configPath = oroIniConfigPath;
            configFormat = UserConfigFormat::Ini;
          }
        }

        gActiveConfigPath = configPath;

        initializeRC(targetPath);
        initializeEnv(targetPath);

        if (!configPath.empty() && fileExists(configPath)) {
          configDocument = readFile(configPath);
          configExists = configDocument.size() > 0;
        } else if (!equal(subcommand, "init") && !equal(subcommand, "env") && !equal(subcommand, "config")) {
    auto inference = inferConfigFromPath(targetPath, targetSourcePath);
    if (!inference.success) {
      const String cliName(gCliDisplayName);
      String initHint = "oro.toml or oro.ini not found in " + targetPath.string() + ". Run '" + cliName + " init' to create one or ensure you're in a project directory.";
      logError(initHint + helpHint("init"));
      exit(1);
    }
          configDocument = inference.ini;
          inferredOverrides = inference.overrides;
          inferredEntry = inference.entryDisplay;
          configExists = true;
          configInferred = true;
          configFormat = UserConfigFormat::Ini;
        }

        settings["oro_active_config_path"] = configPath.string();
        settings["oro_active_config_format"] = configFormat == UserConfigFormat::Toml ? "toml" : "ini";

        if (!envs.empty()) {
          Vector<std::pair<String, String>> envAssignments;
          envAssignments.reserve(envs.size());
          for (const auto& value : envs) {
            const auto separator = value.find('=');
            if (separator != String::npos) {
              const auto key = trim(value.substr(0, separator));
              if (key.empty()) {
                logError("--env expects NAME or NAME=VALUE");
                exit(1);
              }
              envAssignments.emplace_back(key, value.substr(separator + 1));
            } else if (env::has(value)) {
              envAssignments.emplace_back(value, env::get(value));
            } else {
              logError("environment variable '" + value + "' is not set");
              exit(1);
            }
          }
          configDocument = mergeEnvironmentAssignments(
            configDocument,
            configFormat,
            envAssignments
          );
        }

        Vector<String> cliArguments;
        const auto args = std::span(argv, argc).subspan(2);
        cliArguments.reserve(args.size());
        for (const auto& arg : args) {
          cliArguments.push_back(trim(String(arg)));
        }

        if (!cliArguments.empty()) {
          configDocument += "\n[oroc]\n";
          if (configFormat == UserConfigFormat::Toml) {
            StringStream stream;
            stream << "argv = [";
            for (size_t idx = 0; idx < cliArguments.size(); ++idx) {
              if (idx > 0) {
                stream << ", ";
              }
              stream << quoteTomlString(cliArguments[idx]);
            }
            stream << "]\n";
            configDocument += stream.str();
          } else {
            configDocument += "argv = " + joinIniArguments(cliArguments) + "\n";
          }
        }

        bool appendedRootSection = false;
        for (const auto& tuple : rc) {
          if (tuple.first.starts_with("settings_")) {
            continue;
          }
          if (configFormat == UserConfigFormat::Toml) {
            configDocument += "\n" + tuple.first + " = " + quoteTomlString(tuple.second) + "\n";
          } else {
            if (!appendedRootSection) {
              configDocument += "\n[]\n";
              appendedRootSection = true;
            }
            configDocument += tuple.first + " = " + tuple.second + "\n";
          }
        }

        if (configInferred) {
          auto origin = targetArgumentPath.empty() ? targetPath.string() : targetArgumentPath.string();
          if (inferredEntry.size() > 0) {
            logInfo("Inferred configuration for '" + origin + "' (entry: " + inferredEntry + ")");
          } else {
            logInfo("Inferred configuration for '" + origin + "'");
          }
        }

        settingsSource = tmpl(configDocument, Map<> {
          {"platform.arch", platform.arch},
          {"platform.arch.short", replace(platform.arch, "x86_64", "x64")},
          {"platform.os", platform.os},
          {"platform.os.short", replace(platform.os, "win32", "win")}
        });

        gEmbeddedConfigFormat = configFormat;

        try {
          const auto userConfig = parseUserConfigSource(settingsSource, configFormat);
          extendMap(settings, userConfig);
        } catch (const std::exception& error) {
          String configLabel;
          if (!configPath.empty()) {
            configLabel = configPath.string();
          } else if (configFormat == UserConfigFormat::Toml) {
            configLabel = "oro.toml";
          } else {
            configLabel = "inferred configuration";
          }
          logError("failed to parse configuration file '" + configLabel + "': " + String(error.what()));
          exit(1);
        }

        if (settingsSource.size() > 0) {
          auto hex = bytes::encodeHexString(settingsSource);
          auto bytesStream = StringStream();
          auto length = hex.size() - 1;
          int size = 0;

          for (int j = 0; j < length; j += 2) {
            size++;

            if (j != 0 && (j & 3) == 0) {
              bytesStream << "\n";
            }

            bytesStream << "  ";
            if (j + 2 < length) {
              bytesStream << "0x" << hex.substr(j, 2) << ",";
            } else if (j + 1 < length) {
              bytesStream << "0x0" << hex.substr(j, 1);
            } else {
              bytesStream << "0x" << hex.substr(j, 2);
            }
          }

          const auto formatLiteral = configFormat == UserConfigFormat::Toml ? "1" : "0";
          code = String(
            "static const unsigned char __oro_runtime_user_config_bytes[" + std::to_string(size) + "] = {\n" + bytesStream.str() + "\n};\n"
            "static const int __oro_runtime_user_config_format = " + formatLiteral + ";\n"
          );
        }

        if (settings["meta_type"] == "extension" || settings["build_type"] == "extension") {
          auto extension = settings["build_name"];
          settings["build_extensions_" + extension + "_path"] = fs::current_path().string();

          for (const auto& entry : settings) {
            if (entry.first.starts_with("extension_sources")) {
              settings["build_extensions_" + extension] += entry.second;
            } else if (entry.first.starts_with("extension_")) {
              auto key = replace(entry.first, "extension_", extension + "_");
              auto value = entry.second;
              auto index = "build_extensions_" + key;
              if (settings[index].size() > 0) {
                settings[index] += " " + value;
              } else {
                settings[index] = value;
              }
            }
          }
        }

        // Allow for local `.ororc` '[settings] ...' entries to overload the
        // project's settings in the configuration file:
        // [settings.ios]
        // simulator_device = "My local device"
        Map<> tmp;
        for (const auto& tuple : rc) {
          if (tuple.first.starts_with("settings_")) {
            auto key = replace(tuple.first, "settings_", "");
            tmp[key] = tuple.second;
          } else if (tuple.first.starts_with("env_")) {
            tmp[tuple.first] = tuple.second;
          }
        }

        extendMap(settings, tmp);

        if (code.size() > 0) {
          settings["ini_code"] = code;
        }

        if (configInferred) {
          settings["oroc_inferred"] = "true";
          extendMap(settings, inferredOverrides);
        }

        // only validate if config exists and we didn't exit early above
        if (configExists && !equal(subcommand, "init") && !equal(subcommand, "env")) {
          // Check if build_name is set
          if (settings.count("build_name") == 0) {
            logError("'name' value is required in the configuration file's [build] section");
            exit(1);
          }

          // Define regular expression to match spaces, and special characters except dash and underscore
          std::regex name_pattern("[^a-zA-Z0-9_\\-]");
          // Check if the name matches the pattern
          if (std::regex_search(settings["build_name"], name_pattern)) {
            logError("'name' in the [build] section can only contain alphanumeric characters, dashes, and underscores");
            exit(1);
          }

          // Validate that the configured version is a valid SemVer 2.0.0 value.
          // This allows optional pre-release and build metadata segments.
          {
            oro::runtime::semver::Version parsedVersion;
            String semverError;
            if (!oro::runtime::semver::parse(settings["meta_version"], parsedVersion, &semverError)) {
              if (semverError.size() == 0) {
                semverError = "must be a valid semantic version";
              }
              logError("'version' in the configuration file's [meta] section is invalid: " + semverError);
            exit(1);
          }
          }

          // default values
          settings["build_output"] = settings["build_output"].size() > 0 ? settings["build_output"] : "build";
          settings["meta_lang"] = settings["meta_lang"].size() > 0 ? settings["meta_lang"] : "en-us";
          settings["meta_version"] = settings["meta_version"].size() > 0 ? settings["meta_version"] : "1.0.0";
          settings["meta_title"] = settings["meta_title"].size() > 0 ? settings["meta_title"] : settings["build_name"];
          if (settings["meta_license"].size() == 0) {
            settings["meta_license"] = "custom";
          }
          settings["application_agent"] = settings["application_agent"].size() > 0 ? "true" : "false";

          for (auto const arg : std::span(argv, argc).subspan(2, numberOfOptions)) {
            if (equal(arg, "--prod") || equal(arg, "-P")) {
              flagDebugMode = false;
              break;
            }
          }

          String suffix = "";

          // internal
          if (flagDebugMode) {
            suffix += "-dev";
          }

          if (settings["meta_application_protocol"].size() == 0) {
            const auto bundleIdentifier = settings["meta_bundle_identifier"];
            settings["meta_application_protocol"] = replace(replace(bundleIdentifier, "\\.", "-"), "_", "-");
          }

          settings["debug"] = flagDebugMode;

          std::replace(settings["build_name"].begin(), settings["build_name"].end(), ' ', '_');
          settings["build_name"] += suffix;
        }


        settings["arch"] = replace(platform.arch, "x86_64", "amd64");
      }

      if (settings["meta_copyright"].empty()) {
        settings["meta_copyright"] = "(c) " + settings["meta_title"] + " 2025";
      }

      subcommandHandler(optionsWithValue, optionsWithoutValue);
    }
  };

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions initOptions = {
    { { "--config", "-C" }, true, false },
    { { "--name", "-n" }, true, true },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };
  createSubcommand("init", initOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    auto isTargetPathEmpty = fs::exists(targetPath) ? fs::is_empty(targetPath) : true;
    auto configOnly = optionsWithoutValue.find("--config") != optionsWithoutValue.end();
    auto projectName = optionsWithValue["--name"].size() > 0 ? optionsWithValue["--name"] : targetPath.filename().string();

    logInfo("project name: " + projectName);

    if (!configOnly) {
      if (isTargetPathEmpty) {
        // create src/index.html
        fs::create_directories(targetPath / "src");
        writeFile(targetPath / "src" / "index.html", gHelloWorld);
        logInfo("src/index.html created in " + targetPath.string());

        writeFile(targetPath / "src" / "index.js", gHelloWorldScript);
        logInfo("src/index.js created in " + targetPath.string());

        writeFile(targetPath / "src" / "serviceworker.js", gHelloWorldServiceWorker);
        logInfo("src/serviceworker.js created in " + targetPath.string());

        // copy icon.png
        fs::copy(prefixPath("assets/icon.png"), targetPath / "src" / "icon.png", fs::copy_options::overwrite_existing);
        logInfo("icon.png created in " + targetPath.string() + "/src");

        if (platform.win) {
          // copy icon.ico
          fs::copy(prefixPath("assets/icon.ico"), targetPath / "src" / "icon.ico", fs::copy_options::overwrite_existing);
          logInfo("icon.ico created in " + targetPath.string() + "/src");
        }
      } else {
        logVerbose("Current directory was not empty. Assuming index.html and icon are already in place.");
      }
      // create .gitignore
      if (!fs::exists(targetPath / ".gitignore")) {
        writeFile(targetPath / ".gitignore", gDefaultGitignore);
        logInfo(".gitignore created in " + targetPath.string());
      } else {
        logVerbose(".gitignore already exists in " + targetPath.string());
      }
    }

    // create oro.toml
    const auto oroConfigPath = targetPath / "oro.toml";
    if (fs::exists(oroConfigPath)) {
      logVerbose("oro.toml already exists in " + targetPath.string());
    } else {
      if (projectName.size() > 0) {
        defaultTemplateAttrs["project_name"] = projectName;
      }
      const auto renderedIni = tmpl(gDefaultConfig, defaultTemplateAttrs);
      writeFile(oroConfigPath, convertIniTemplateToToml(renderedIni));
      logInfo("oro.toml created in " + targetPath.string());
    }

    exit(0);
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions listDevicesOptions = {
    { { "--platform" }, false, true },
    { { "--format", "-f" }, true, true },
    { { "--json" }, true, false },
    { { "--ecid" }, true, false },
    { { "--udid" }, true, false },
    { { "--only" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };
  createSubcommand("list-devices", listDevicesOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    bool isUdid =
      optionsWithoutValue.find("--udid") != optionsWithoutValue.end() ||
      equal(rc["list-devices_udid"], "true");
    bool isEcid =
      optionsWithoutValue.find("--ecid") != optionsWithoutValue.end() ||
      equal(rc["list-devices_ecid"], "true");
    bool isOnly =
      optionsWithoutValue.find("--only") != optionsWithoutValue.end() ||
      equal(rc["list-devices_only"], "true");
    const auto outputFormat = resolveCommandOutputFormat(
      "list-devices",
      optionsWithValue,
      optionsWithoutValue,
      { "text", "json" },
      "text"
    );
    const bool jsonOutput = equal(outputFormat, "json");

    auto targetPlatform = optionsWithValue["--platform"];
    JSON::Array devicesJson;

    if (targetPlatform == "ios" && platform.mac) {
      if (isUdid && isEcid) {
        logWarn("--udid and --ecid are mutually exclusive");
        printHelp("list-devices");
        exit(1);
      }

      if (isOnly && !isUdid && !isEcid) {
        logWarn("--only requires --udid or --ecid");
        printHelp("list-devices");
        exit(1);
      }

      String cfgUtilPath = getCfgUtilPath();
      String command = cfgUtilPath + " list-devices";
      auto r = exec(command);

      if (r.exitCode == 0) {
        std::regex re(R"(ECID:\s(\S*)\s*UDID:\s(\S*).*Name:\s(.*))");
        std::smatch matches;

        while (std::regex_search(r.output, matches, re)) {
          const auto ecid = trim(matches[1].str());
          const auto udid = trim(matches[2].str());
          const auto name = trim(matches[3].str());
          if (jsonOutput) {
            const auto id = isEcid ? ecid : udid;
            const auto idType = isEcid ? "ecid" : "udid";
            devicesJson.push(JSON::Object::Entries {
              { "platform", "ios" },
              { "name", name },
              { "id", id },
              { "idType", idType },
              { "ecid", ecid },
              { "udid", udid }
            });
            if (isOnly) {
              break;
            }
          } else if (isUdid) {
            if (isOnly) {
              std::cout << udid << std::endl;
              break;
            }
            std::cout << name << ": " << udid << std::endl;
          } else if (isEcid) {
            if (isOnly) {
              std::cout << ecid << std::endl;
              break;
            }
            std::cout << name << ": " << ecid << std::endl;
          } else {
            std::cout << name << std::endl;
            std::cout << "ECID: " << ecid << " UDID: " << udid << std::endl;
            if (isOnly) {
              break;
            }
          }
          r.output = matches.suffix();
        }
        if (jsonOutput) {
          std::cout << JSON::stringify(devicesJson) << std::endl;
        }
        exit(0);
      } else {
        logError("Could not list devices using " + command);
        exit(1);
      }
    } else if (targetPlatform == "android") {
      auto androidHome = getAndroidHome();
      StringStream adb;

      if (!platform.win) {
        adb << androidHome << "/platform-tools/";
      } else {
        adb << androidHome << "\\platform-tools\\";
      }

      adb << "adb" << (platform.win ? ".exe" : "");

      auto r = exec(adb.str() + " devices");

      if (r.exitCode != 0) {
        exit(r.exitCode);
      }

      std::stringstream stream(r.output);
      String line;

      while (std::getline(stream, line)) {
        line = trim(line);

        if (
          line.empty() ||
          line == "List of devices attached" ||
          line.starts_with("* daemon ") ||
          line.starts_with("adb server ")
        ) {
          continue;
        }

        const auto separator = line.find_first_of(" \t");
        if (separator == String::npos) {
          continue;
        }

        const auto serial = trim(line.substr(0, separator));
        const auto status = trim(line.substr(separator + 1));

        if (serial.empty() || status != "device") {
          continue;
        }

        if (jsonOutput) {
          devicesJson.push(JSON::Object::Entries {
            { "platform", "android" },
            { "id", serial },
            { "idType", "serial" },
            { "serial", serial },
            { "status", "device" }
          });
        } else {
          std::cout << serial << std::endl;
        }
        if (isOnly) {
          break;
        }
      }

      if (jsonOutput) {
        std::cout << JSON::stringify(devicesJson) << std::endl;
      }
      exit(0);
    } else {
      logError(
        "list-devices supports Android targets via adb on any host, and iOS targets on macOS." +
        helpHint("list-devices")
      );
      exit(1);
    }
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions versionOptions = {
    { { "--config", "-C" }, true, true },
    { { "--preid" }, true, true },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };
  CommandLineOptions versionsOptions = {
    { { "--format", "-f" }, true, true },
    { { "--json" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };
  createSubcommand("version", versionOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    const bool inferred = settings["oroc_inferred"] == "true";
    const Path configPath = gActiveConfigPath;
    const UserConfigFormat configFormat = gEmbeddedConfigFormat;

    if (inferred || configPath.empty()) {
      logError("version: an explicit configuration file is required. Run 'oroc init' to create oro.toml or pass --config <path>.");
      exit(1);
    }

    String current = settings["meta_version"];
    if (current.size() == 0) {
      logError("version: 'meta.version' is not set in configuration");
      exit(1);
    }

    oro::runtime::semver::Version parsed;
    String parseError;
    if (!oro::runtime::semver::parse(current, parsed, &parseError)) {
      if (parseError.size() == 0) {
        parseError = "must be a valid semantic version";
      }
      logError("version: current 'meta.version' is invalid: " + parseError);
      exit(1);
    }

    const String newVersionSpec = optionsWithValue["--new-version"];

    if (newVersionSpec.size() == 0) {
      // No arguments: print the current app version.
      std::cout << parsed.str() << std::endl;
      exit(0);
    }

    const String preid = optionsWithValue["--preid"];
    const String spec = newVersionSpec;

    String nextVersion;
    auto releaseType = oro::runtime::semver::ReleaseType::Patch;
    bool isReleaseType = false;

    if (equal(spec, "major")) {
      releaseType = oro::runtime::semver::ReleaseType::Major;
      isReleaseType = true;
    } else if (equal(spec, "minor")) {
      releaseType = oro::runtime::semver::ReleaseType::Minor;
      isReleaseType = true;
    } else if (equal(spec, "patch")) {
      releaseType = oro::runtime::semver::ReleaseType::Patch;
      isReleaseType = true;
    } else if (equal(spec, "premajor")) {
      releaseType = oro::runtime::semver::ReleaseType::Premajor;
      isReleaseType = true;
    } else if (equal(spec, "preminor")) {
      releaseType = oro::runtime::semver::ReleaseType::Preminor;
      isReleaseType = true;
    } else if (equal(spec, "prepatch")) {
      releaseType = oro::runtime::semver::ReleaseType::Prepatch;
      isReleaseType = true;
    } else if (equal(spec, "prerelease")) {
      releaseType = oro::runtime::semver::ReleaseType::Prerelease;
      isReleaseType = true;
    }

    if (isReleaseType) {
      nextVersion = oro::runtime::semver::inc(parsed.str(), releaseType, preid);
      if (nextVersion.size() == 0) {
        logError("version: failed to increment semantic version");
        exit(1);
      }
    } else {
      oro::runtime::semver::Version target;
      String targetError;
      if (!oro::runtime::semver::parse(spec, target, &targetError)) {
        if (targetError.size() == 0) {
          targetError = "must be a valid semantic version or release type";
        }
        logError("version: invalid version spec '" + spec + "': " + targetError);
        exit(1);
      }
      nextVersion = target.str();
    }

    if (nextVersion == parsed.str()) {
      logError("version: new version is identical to current version (" + nextVersion + ")");
      exit(1);
    }

    if (!updateVersionInConfigFile(configPath, configFormat, nextVersion)) {
      exit(1);
    }

    settings["meta_version"] = nextVersion;

    std::cout << nextVersion << std::endl;
    exit(0);
  });

  createSubcommand("versions", versionsOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    auto versions = collectDependencyVersions();

    String requested;
    if (optionsWithValue.count("--dependency") > 0) {
      requested = optionsWithValue["--dependency"];
    }

    const auto format = resolveCommandOutputFormat(
      "versions",
      optionsWithValue,
      optionsWithoutValue,
      { "text", "json" },
      "text"
    );

    if (requested.size() > 0) {
      auto it = versions.find(requested);
      if (it == versions.end()) {
        std::cerr << "Unknown dependency: " << requested << std::endl;
        exit(1);
      }

      if (equal(format, "json")) {
        std::map<String, String> filtered;
        filtered[it->first] = it->second;
        JSON::Object object(filtered);
        std::cout << JSON::stringify(object) << std::endl;
      } else {
        std::cout << it->second << std::endl;
      }
      exit(0);
    }

    if (equal(format, "json")) {
      JSON::Object object(versions);
      std::cout << JSON::stringify(object) << std::endl;
    } else {
      for (const auto& entry : versions) {
        std::cout << entry.first << " " << entry.second << std::endl;
      }
    }

    exit(0);
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions installAppOptions = {
    { { "--debug", "-D" }, true, false },
    { { "--device" }, true, true },
    { { "--platform" }, true, true },
    { { "--prod", "-P" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--target" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("install-app", installAppOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    String commandOptions = "";
    String targetPlatform = optionsWithValue["--platform"];
    String installTarget = optionsWithValue["--target"];

    if (targetPlatform.size() > 0) {
      // just assume android when 'android-emulator' is given
      if (targetPlatform == "android-emulator") {
        targetPlatform = "android";
      }

      if (targetPlatform != "ios" && targetPlatform != "android") {
        logError("Unsupported platform: " + targetPlatform + helpHint("install-app"));
        exit(1);
      }
    }

    if (targetPlatform.size() == 0) {
      if (!platform.mac && !platform.linux) {
        logError("Unsupported host desktop platform. Only 'macOS' and 'Linux' is supported");
        exit(1);
      }

      targetPlatform = platform.os;
    }

    auto device = optionsWithValue["--device"];
    if (device.size() > 0) {
      if (targetPlatform == "ios") {
        commandOptions += " --ecid " + device + " ";
      } else if (targetPlatform == "android") {
        commandOptions += " -s " + device;
      }
    }

    if (targetPlatform == "ios") {
      auto cfgUtilPath = getCfgUtilPath();
      auto ipaPath = (
        getPaths(targetPlatform).platformSpecificOutputPath /
        "build" /
        String(settings["build_name"] + ".ipa") /
        String(settings["build_name"] + ".ipa")
      );

      if (!fs::exists(ipaPath)) {
        const String cliName(gCliDisplayName);
        logError("Could not find " + ipaPath.string() + ". Run '" + cliName + " build --platform=ios' to generate the .ipa first.");
        exit(1);
      }

      // this command will install the app to the first connected device which was
      // added to the provisioning profile if no --device is provided.
      auto command = cfgUtilPath + " " + commandOptions + "install-app " + ipaPath.string();
      if (flagDebugMode) {
        logDebug(command);
      }

      auto r = exec(command);

      if (r.exitCode != 0) {
        const String cliName(gCliDisplayName);
        logError("failed to install the app. Is the device plugged in? Check 'adb devices' and use '--device'. See '" + cliName + " install-app --help' (common errors)");
        if (flagDebugMode) {
          logDebug(r.output);
        }
        exit(1);
      }
    } else if (targetPlatform == "android") {
      auto androidHome = getAndroidHome();
      auto paths = getPaths(targetPlatform);
      auto output = paths.platformSpecificOutputPath;
      auto app = output / "app";
      StringStream adb;

      if (!platform.win) {
        adb << androidHome << "/platform-tools/";
      } else {
        adb << androidHome << "\\platform-tools\\";
      }

      adb << "adb" << commandOptions << " install ";

      if (flagDebugMode) {
        adb << (app / "build" / "outputs" / "apk" / "dev" / "debug" / "app-dev-debug.apk").string();
      } else {
        adb << (app / "build" / "outputs" / "apk" / "live" / "debug" / "app-live-debug.apk").string();
      }

      auto command = adb.str();
      auto r = exec(command);

      if (r.exitCode != 0) {
        const String cliName(gCliDisplayName);
        logError("failed to install the app. Is the device plugged in? Check 'adb devices' and use '--device'. See '" + cliName + " install-app --help' (common errors)");
        exit(1);
      }
    } else if (platform.mac) {
      if (installTarget.size() == 0) {
        installTarget = "/";
      }

      auto paths = getPaths(targetPlatform);
      auto pkgArchive = paths.platformSpecificOutputPath / (settings["build_name"] + ".pkg");
      auto zipArchive = paths.platformSpecificOutputPath / (settings["build_name"] + ".zip");
      auto appDirectory = paths.platformSpecificOutputPath / (settings["build_name"] + ".app");

      if (fs::exists(pkgArchive)) {
        String command = "";

        if (installTarget == "/") {
          command = "sudo installer";
        } else {
          command = "installer";
        }

        if (flagVerboseMode) {
          command += " -verbose";
        }

        command += " -pkg " + pkgArchive.string();
        command += " -target " + installTarget;

        auto r = exec(command);
        if (r.exitCode != 0) {
          logError("Failed to install app in target");

          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        if (flagVerboseMode) {
          logVerbose(r.output);
        }
      } else if (fs::exists(zipArchive)) {
        String command = (
          "ditto -x -k " +
          zipArchive.string() +
          " " + (Path(installTarget) / "Applications").string()
        );

        auto r = exec(command);
        if (r.exitCode != 0) {
          logError("Failed to unzip app to install target");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        if (flagVerboseMode) {
          logVerbose(r.output);
        }
      } else if (fs::exists(appDirectory)) {
        String command = (
          "cp -r " +
          appDirectory.string() +
          " " + (Path(installTarget) / "Applications").string()
        );

        auto r = exec(command);
        if (r.exitCode != 0) {
          logError("Failed to copy app to install target");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        if (flagVerboseMode) {
          logVerbose(r.output);
        }
      } else {
        const String cliName(gCliDisplayName);
        logError("Unable to determine macOS application or package to install. Build first with '" + cliName + " build' and re-run. See '" + cliName + " install-app --help' (common errors)");
        exit(1);
      }
    } else if (platform.linux) {
      auto paths = getPaths(targetPlatform);
      Path pathPackage = paths.platformSpecificOutputPath / toLowerCase(
        settings["build_name"] + "_" +
        settings["meta_version"] + "_" +
        replace(platform.arch, "x86_64", "amd64")
      );

      auto debArchive = pathPackage.string() + ".deb";
      String command = "dpkg -i " + debArchive;
      auto r = exec(command);
      if (r.exitCode != 0) {
        logError("Failed to install app to target");
        if (flagDebugMode) {
          logDebug(r.output);
        }
        exit(r.exitCode);
      }

      if (flagVerboseMode) {
        logVerbose(r.output);
      }
    } else {
      logError("Unsupported platform target." + helpHint("install-app"));
      exit(1);
    }

    if (targetPlatform == "ios" || targetPlatform == "android") {
      logInfo("Successfully installed the app on your device(s).");
    } else {
      logInfo("Successfully installed the app to your desktop.");
    }
    exit(0);
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions printBuildDirOptions = {
    { { "--platform" }, true, true },
    { { "--root" }, true, false},
    { { "--prod", "-P" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };

  createSubcommand("print-build-dir", printBuildDirOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    bool flagRoot = optionsWithoutValue.find("--root") != optionsWithoutValue.end();
    // if --platform is specified, use the build path for the specified platform
    auto targetPlatform = optionsWithValue["--platform"];
    Paths paths = getPaths(targetPlatform.size() > 0 ? targetPlatform : platform.os);

    if (flagRoot) {
      std::cout << paths.platformSpecificOutputPath.string() << std::endl;
      exit(0);
    }

    std::cout << paths.pathResourcesRelativeToUserBuild.string() << std::endl;
    exit(0);
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions runOptions = {
    { { "--config" }, true, true },
    { { "--platform" }, true, true },
    { { "--prod", "-P" }, true, false },
    { { "--test", "-t" }, true, true },
    { { "--headless", "-H" }, true, false },
    { { "--debug", "-D" }, true, false },
    { { "--quiet", "-q" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--env", "-E" }, true, true },
    { { "--port" }, true, true },
    { { "--host"}, true, true },
    { { "--remote-debugging-port" }, true, true },
    { { "--tls-keylog" }, true, true },
    { { "--allow-exec" }, true, false },
    { { "--sanitizers" }, true, false },
    { { "--log-file" }, true, true }
  };

  CommandLineOptions buildOptions = {
    { { "--quiet", "-q" }, true, false },
    { { "--only-build", "-o" }, true, false },
    { { "--run", "-r" }, true, false },
    { { "--watch", "-w" }, true, false },
    { { "--package", "-p" }, true, false },
    { { "--package-format", "-f" }, true, true },
    { { "--sign" }, true, false },
    { { "--sign-key" }, true, true },
    { { "--codesign", "-c" }, true, false },
    { { "--notarize", "-n" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--copy" }, true, true },
    { { "--log-file" }, true, true }
  };

  // Insert the elements of runOptions into buildOptions
  buildOptions.insert(buildOptions.end(), runOptions.begin(), runOptions.end());
  createSubcommand("build", buildOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    auto isForExtensionOnly = settings["meta_type"] == "extension" || settings["build_type"] == "extension";

    if (!isForExtensionOnly) {
      if (settings["meta_bundle_identifier"].size() == 0) {
        logError("[meta] bundle_identifier option is empty");
        exit(1);
      }

      std::regex reverseDnsPattern(R"(^[a-z0-9]+([-a-z0-9]*[a-z0-9]+)?(\.[a-z0-9]+([-a-z0-9]*[a-z0-9]+)?)*$)", std::regex::icase);
      if (!std::regex_match(settings["meta_bundle_identifier"], reverseDnsPattern)) {
          logError("[meta] bundle_name has invalid format :" + settings["meta_bundle_identifier"]);
          logWarn("Please use reverse DNS notation (https://developer.apple.com/documentation/bundleresources/information_property_list/cfbundleidentifier#discussion)");
          exit(1);
      }
    }

    // Merge any CLI-level copy mappings into the resolved settings so they
    // behave like additional '[build] copy' entries regardless of config source.
    if (optionsWithValue.count("--copy") > 0) {
      auto cliCopy = trim(optionsWithValue["--copy"]);
      if (cliCopy.size() > 0) {
        if (settings["build_copy"].size() > 0) {
          settings["build_copy"] += ";" + cliCopy;
        } else {
          settings["build_copy"] = cliCopy;
        }
      }
    }

    String argvForward = "";
    String targetPlatform = optionsWithValue["--platform"];

    bool flagRunUserBuildOnly = optionsWithoutValue.find("--only-build") != optionsWithoutValue.end() || equal(rc["build_only"], "true");
    bool flagCodeSign = optionsWithoutValue.find("--codesign") != optionsWithoutValue.end() || equal(rc["build_codesign"], "true");
    bool flagBuildHeadless = settings["build_headless"] == "true";
    bool flagRunHeadless = optionsWithoutValue.find("--headless") != optionsWithoutValue.end();
    bool flagShouldRun = optionsWithoutValue.find("--run") != optionsWithoutValue.end() || equal(rc["build_run"], "true");
    bool flagShouldNotarize = optionsWithoutValue.find("--notarize") != optionsWithoutValue.end() || equal(rc["build_notarize"], "true");
    bool flagShouldSignPackages = optionsWithoutValue.find("--sign") != optionsWithoutValue.end() || equal(rc["build_sign"], "true");
    bool flagShouldPackage = optionsWithoutValue.find("--package") != optionsWithoutValue.end() || equal(rc["build_package"], "true");
    String signKeyId = optionsWithValue["--sign-key"];
    if (signKeyId.size() == 0 && rc["build_sign_key"].size() > 0) {
      signKeyId = rc["build_sign_key"];
    }
    bool flagBuildForIOS = false;
    bool flagBuildForAndroid = false;
    bool flagBuildForAndroidEmulator = false;
    bool flagBuildForSimulator = false;
    bool flagBuildTest = optionsWithoutValue.find("--test") != optionsWithoutValue.end() || optionsWithValue["--test"].size() > 0;
    bool flagAllowExec = optionsWithoutValue.find("--allow-exec") != optionsWithoutValue.end() || equal(rc["allow_exec"], "true");
    bool flagEnableSanitizers = optionsWithoutValue.find("--sanitizers") != optionsWithoutValue.end() || equal(rc["enable_sanitizers"], "true");

    if (flagAllowExec) {
      settings["allow_exec"] = "true";
    }
    if (flagEnableSanitizers) {
      env::set("ORO_ENABLE_SANITIZERS", "1");
      settings["enable_sanitizers"] = "true";
      logInfo("Enabling ASan/UBSan for desktop core builds via ORO_ENABLE_SANITIZERS=1");
    }
    String tlsKeylogPath = optionsWithValue["--tls-keylog"];
    if (tlsKeylogPath.size() > 0) {
      env::set("ORO_TLS_KEYLOG", tlsKeylogPath);
    }
    bool flagShouldWatch = optionsWithoutValue.find("--watch") != optionsWithoutValue.end() || equal(rc["build_watch"], "true");
    String testFile = optionsWithValue["--test"];

    if (optionsWithValue["--package-format"].size() > 0) {
      flagShouldPackage = true;
    }

    if (flagShouldRun && flagShouldPackage) {
      logError("use the 'run' command after packaging.");
      exit(1);
    }

    if (flagBuildTest && testFile.size() == 0) {
      const String cliName(gCliDisplayName);
      logError("--test value is required. See '" + cliName + " run --help' (common errors)");
      exit(1);
    }

    if (testFile.size() > 0) {
      argvForward += " --test=" + testFile;
    }

    if (flagBuildHeadless || flagRunHeadless) {
      argvForward += " --headless";
    }

    if (optionsWithValue.count("--remote-debugging-port") > 0) {
      const auto port = trim(optionsWithValue["--remote-debugging-port"]);
      if (port.size() > 0) {
        argvForward += " --remote-debugging-port=" + port;
      }
    }

    if (flagAllowExec) {
      argvForward += " --allow-exec";
    }

    if (optionsWithoutValue.find("--quiet") != optionsWithoutValue.end() || equal(rc["build_quiet"], "true")) {
      flagQuietMode = true;
    }

    if (flagShouldNotarize && !platform.mac) {
      logWarn("Notarization is only supported on macOS. Ignoring option.");
      flagShouldNotarize = false;
    }

    if (targetPlatform.size() > 0) {
      if (targetPlatform == "ios") {
        flagBuildForIOS = true;
      } else if (targetPlatform == "android") {
        flagBuildForAndroid = true;
      } else if (targetPlatform == "android-emulator") {
        flagBuildForAndroid = true;
        flagBuildForAndroidEmulator = true;
      } else if (targetPlatform == "ios-simulator") {
        flagBuildForIOS = true;
        flagBuildForSimulator = true;
      } else {
        std::cout << "Unsupported platform: " << targetPlatform << helpHint("build") << std::endl;
        exit(1);
      }
    } else {
      targetPlatform = platform.os;
    }

    argvForward +=  " --platform=" + targetPlatform;
    auto platformFriendlyName = targetPlatform == "win32" ? "windows" : targetPlatform;
    platformFriendlyName = platformFriendlyName == "android-emulator" ? "android" : platformFriendlyName;

    String devHost = "localhost";
    if (optionsWithValue.count("--host") > 0) {
      devHost = optionsWithValue["--host"];
    } else if (flagBuildForIOS || flagBuildForAndroid) {
      devHost = detectDevHostIP();
    }

    if (!devHost.starts_with("http")) {
      devHost = String("http://") + devHost;
    }

    settings.insert(std::make_pair("host", devHost));

    String devPort = "0";
    if (optionsWithValue.count("--port") > 0) {
      devPort = optionsWithValue["--port"];
    }
    settings.insert(std::make_pair("port", devPort));

    if (!devHost.empty() && !devPort.empty() && devPort != "0") {
      const auto url = URL(devHost + ":" + devPort);
      if (settings["webview_insecure_domains"].empty()) {
        settings["webview_insecure_domains"] = url.hostname;
      } else {
        settings["webview_insecure_domains"] += " " + url.hostname;
      }
    }

    auto cnt = 0;

    AndroidCliState androidState;
    androidState.targetPlatform = targetPlatform;

    const bool debugEnv = (
      env::get("ORO_DEBUG").size() > 0 ||
      env::get("DEBUG").size() > 0
    );
    auto debugBuild = debugEnv;

    const bool verboseEnv = (
      env::get("ORO_VERBOSE").size() > 0 ||
      env::get("VERBOSE").size() > 0
    );

    auto oldCwd = fs::current_path();
    auto devNull = ">" + String((!platform.win) ? "/dev/null" : "NUL") + (" 2>&1");

    String localDirPrefix = !platform.win ? "./" : "";
    String quote = !platform.win ? "'" : "\"";
    String slash = !platform.win ? "/" : "\\";

    if (settings.count("meta_compile_bitcode") == 0) settings["meta_compile_bitcode"] = "false";
    if (settings.count("meta_upload_bitcode") == 0) settings["meta_upload_bitcode"] = "false";
    if (settings.count("meta_upload_symbols") == 0) settings["meta_upload_symbols"] = "true";

    if (settings.count("meta_file_limit") == 0) {
      settings["meta_file_limit"] = "4096";
    }

    // set it automatically, hide it from the user
    settings["meta_revision"] = "1";

    if (env::get("CXX").size() == 0) {
      logWarn("$CXX env var not set, assuming defaults");

      if (platform.win) {
        env::set("CXX=clang++");
      } else {
        env::set("CXX=/usr/bin/clang++");
      }
    }

    if (env::get("CI").size() == 0 && !isSetupComplete(platformFriendlyName)) {
      logError("Build dependency setup is incomplete for " + platformFriendlyName + ", Use 'setup' to resolve: ");
      std::cout << gCliDisplayName << " setup --platform=" << platformFriendlyName << std::endl;
      exit(1);
    }

    Paths paths = getPaths(targetPlatform);

    auto executable = Path(settings["build_name"] + (platform.win ? ".exe" : ""));
    auto binaryPath = paths.pathBin / executable;
    Path configPath;
    if (settings.contains("oro_active_config_path") && settings["oro_active_config_path"].size() > 0) {
      configPath = Path(settings["oro_active_config_path"]);
    } else if (fileExists(targetPath / "oro.toml")) {
      configPath = targetPath / "oro.toml";
    } else if (fileExists(targetPath / "oro.ini")) {
      configPath = targetPath / "oro.ini";
    }

    if (!fs::exists(binaryPath) && !flagBuildForAndroid && !flagBuildForAndroidEmulator && !flagBuildForIOS && !flagBuildForSimulator) {
      flagRunUserBuildOnly = false;
    } else {
      struct stat stats;
      if (stat(convertWStringToString(binaryPath).c_str(), &stats) == 0) {
        if (ORO_RUNTIME_BUILD_TIME > stats.st_mtime) {
          flagRunUserBuildOnly = false;
        }

        const auto runtimeArchive = resolveRuntimeStaticArchive(
          "lib/" + platform.arch + "-desktop"
        );
        struct stat runtimeArchiveStats;
        if (
          stat(convertWStringToString(runtimeArchive).c_str(), &runtimeArchiveStats) == 0 &&
          runtimeArchiveStats.st_mtime > stats.st_mtime
        ) {
          flagRunUserBuildOnly = false;
        }
      }
    }

    if (flagRunUserBuildOnly) {
      struct stat binaryPathStats;
      struct stat configPathStats;

      if (stat(convertWStringToString(binaryPath).c_str(), &binaryPathStats) == 0) {
        if (!configPath.empty() && stat(convertWStringToString(configPath).c_str(), &configPathStats) == 0) {
          if (configPathStats.st_mtime > binaryPathStats.st_mtime) {
            flagRunUserBuildOnly = false;
          }
        }
      }
    }

    auto removePath = paths.platformSpecificOutputPath;
    if (targetPlatform == "android") {
      removePath = settings["build_output"] + "/android/app/src";
    }

    if (flagRunUserBuildOnly == false && fs::exists(removePath)) {
      auto p = fs::current_path() / Path(removePath);
      try {
        fs::remove_all(p);
            logInfo("cleaned: " + p.string());
      } catch (fs::filesystem_error const& ex) {
            logError("could not clean path (binary could be busy)");
            logError(String("ex: ") + ex.what());
            logError(String("ex code: ") + std::to_string(ex.code().value()));

            if (flagBuildForAndroid) {
              logWarn("check for gradle processes.");
            }

        exit(1);
      }
    }

    auto pathResourcesRelativeToUserBuild = paths.pathResourcesRelativeToUserBuild;

    String flags;
    String files;

    Path pathResources;
    Path pathToArchive;

    auto appendTLSFlags = [&](String& targetFlags) {
      const auto enableTLS = trim(env::get("ORO_ENABLE_TLS"));
      const auto enableMbedTLS = trim(env::get("ORO_ENABLE_MBEDTLS"));

      String provider = toLowerCase(trim(env::get("ORO_TLS_PROVIDER")));
      const String extraCFlags = env::get("ORO_TLS_CFLAGS");
      const String extraLDFlags = env::get("ORO_TLS_LDFLAGS");

      const auto bundledLibDir = Path(prefixFile("lib/" + platform.arch + "-desktop"));
      const bool hasBundledMbedTLS = (
        fs::exists(bundledLibDir / "libmbedtls.a") &&
        fs::exists(bundledLibDir / "libmbedx509.a") &&
        fs::exists(bundledLibDir / "libmbedcrypto.a")
      );

      if (provider.empty()) {
        if (enableMbedTLS == "1") {
          provider = "mbedtls";
        }
      }

      bool tlsRequested = (enableTLS == "1" || enableMbedTLS == "1" || !provider.empty() || !extraCFlags.empty() || !extraLDFlags.empty());
      if (
        !tlsRequested &&
        hasBundledMbedTLS &&
        platform.linux &&
        !platform.android
      ) {
        provider = "mbedtls";
        tlsRequested = true;
      }

      if (!tlsRequested) {
        return;
      }

      auto failTLSProvider = [&](const String& message) {
        logError(message);
        exit(1);
      };

      auto runDetect = [&](const String& cmd) -> bool {
        auto r = exec(cmd.c_str());
        return r.exitCode == 0;
      };

      const bool supportsBuiltInTLSProvider =
        platform.win || (platform.linux && !platform.android);

      if (!supportsBuiltInTLSProvider) {
        String targetName = "this target";
        if (platform.mac) {
          targetName = "macOS";
        } else if (platform.ios) {
          targetName = "iOS";
        } else if (platform.android) {
          targetName = "Android";
        }

        logWarn(
          "TLS was requested for " + targetName + ", but this repository does not ship a built-in TLS provider there. "
          "Building without TLS support; oro:tls and related TLS entry points will return NOT_IMPLEMENTED on this target."
        );
        env::set("ORO_TLS_PROVIDER", "");
        return;
      }

      if (provider.empty()) {
        if (platform.win) {
          provider = "schannel";
        } else if (platform.linux) {
          if (runDetect("pkg-config --exists openssl")) {
            provider = "openssl";
          } else if (hasBundledMbedTLS || runDetect("pkg-config --exists mbedtls")) {
            provider = "mbedtls";
          }
        }
      }

      if (provider.empty()) {
        failTLSProvider(
          "TLS was requested, but no supported built-in provider is available. "
          "Supported built-in providers in this repository are Linux desktop: mbedtls|openssl and Windows desktop: schannel."
        );
      }

      if (provider == "gnutls") {
        failTLSProvider("GnuTLS is not implemented in this repository.");
      }

      if (provider == "securetransport") {
        failTLSProvider("SecureTransport is not implemented in this repository.");
      }

      if (provider == "android") {
        failTLSProvider("The Android TLS provider is not implemented in this repository.");
      }

      if (provider == "schannel" && !platform.win) {
        failTLSProvider("The Schannel TLS provider is only supported on Windows targets.");
      }

      env::set("ORO_TLS_PROVIDER", provider);

      auto appendFragment = [&](const String& fragment) {
        if (fragment.empty()) return;
        targetFlags += " ";
        targetFlags += fragment;
      };

      auto appendDefine = [&](const String& macro) {
        targetFlags += " ";
        targetFlags += macro;
      };

      if (provider == "openssl") {
        appendDefine("-DORO_RUNTIME_TLS_OPENSSL=1");
        if (platform.linux && extraCFlags.empty() && extraLDFlags.empty()) {
          appendFragment("`pkg-config --cflags --libs openssl`");
          return;
        }
      } else if (provider == "mbedtls") {
        appendDefine("-DORO_RUNTIME_ENABLE_MBEDTLS=1");
        if (!hasBundledMbedTLS && platform.linux && extraCFlags.empty() && extraLDFlags.empty()) {
          appendFragment("`pkg-config --cflags --libs mbedtls`");
          return;
        }
      } else if (provider == "schannel") {
        appendDefine("-DORO_RUNTIME_TLS_SCHANNEL=1");
      } else {
        failTLSProvider("Unknown TLS provider '" + provider + "'.");
      }

      appendFragment(extraCFlags);
      appendFragment(extraLDFlags);
    };

    if (platform.mac && exec("xcode-select -p").exitCode != 0) {
      logError("Please install Xcode from https://apps.apple.com/us/app/xcode/id497799835");
      exit(1);
    }

    bool isForDesktop = !flagBuildForIOS && !flagBuildForAndroid;

    if (isForDesktop) {
      fs::create_directories(paths.platformSpecificOutputPath / "include" / "oro");
      writeFile(paths.platformSpecificOutputPath / "include" / "oro" / "_user-config-bytes.hh", settings["ini_code"]);
    }

    //
    // Apple requires you to compile XCAssets/AppIcon.appiconset with a catalog for iOS and MacOS.
    //
    auto compileIconAssets = [&]() {
      auto src = paths.platformSpecificOutputPath;

      Vector<Tuple<double, double>> types = {};
      JSON::Array images;

      const String prefix = isForDesktop ? "mac" : "ios";
      const String key = prefix + "_icon_sizes";
      const auto sizes = split(settings[key], " ");

        for (const auto& type : sizes) {
          const auto pair = split(type, '@');

          if (pair.size() != 2 || pair.size() == 0) {
            logError("icon size requires <size>@<scale>");
            return;
          }

        const String size = pair[0];
        const String scale = pair[1];

        if (size == "1024") {
         images.push(JSON::Object::Entries {
            { "size", size + "x" + size },
            { "idiom", isForDesktop ? "mac" : "ios-marketing" },
            { "filename", "Icon-" + size + "x" + size + "@" + scale + ".png" },
            { "scale", scale }
          });
        } else if (isForDesktop) {
          images.push(JSON::Object::Entries {
            { "size", size + "x" + size },
            { "idiom", "mac" },
            { "filename", "Icon-" + size + "x" + size + "@" + scale + ".png" },
            { "scale", scale }
          });
        } else {
         images.push(JSON::Object::Entries {
            { "size", size + "x" + size },
            { "idiom", "iphone" },
            { "filename", "Icon-" + size + "x" + size + "@" + scale + ".png" },
            { "scale", scale }
          });
         images.push(JSON::Object::Entries {
            { "size", size + "x" + size },
            { "idiom", "ipad" },
            { "filename", "Icon-" + size + "x" + size + "@" + scale + ".png" },
            { "scale", scale }
          });
        }

        types.push_back(std::make_tuple(std::stof(pair[0]), std::stof(pair[1])));
      }

      auto assetsPath = fs::path { src / "Assets.xcassets" };
      auto iconsPath = fs::path { assetsPath / "AppIcon.appiconset" };

      fs::create_directories(iconsPath);

      JSON::Object json = JSON::Object::Entries {
        { "images", images },
        { "info", JSON::Object::Entries {
          { "version", 1 },
          { "author", "xcode" }
        }}
      };

      writeFile(iconsPath / "Contents.json", json.str());
      if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
        logDebug(json.str());
      }

      for (const auto& type : types) {
        const auto size = std::get<0>(type);
        const auto scale = std::get<1>(type);
        const auto scaled = JSON::Number(size * scale).str();
        const auto destFileName = "Icon-" + JSON::Number(size).str() + "x" + JSON::Number(size).str() + "@" + JSON::Number(scale).str() + "x.png";
        const auto destFilePath = iconsPath.string() + "/" + destFileName;

        const auto src = isForDesktop ? settings["mac_icon"] : settings["ios_icon"];

        StringStream sipsCommand;
        sipsCommand
          << "sips"
          << " -z " << scaled << " " << scaled
          << " " << src
          << " --out " << destFilePath;

        if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
          logVerbose(sipsCommand.str());
        }

        const auto r = exec(sipsCommand.str().c_str());

        if (r.exitCode != 0) {
          logError("failed to create project icons");
          logDebug(r.output);
          exit(1);
        }

        if (flagBuildForIOS) {
          const auto resources = paths.platformSpecificOutputPath;
          fs::copy(destFilePath, resources / destFileName, fs::copy_options::overwrite_existing);
        }
      }

      const auto dest = isForDesktop
        ? paths.pathResourcesRelativeToUserBuild
        : paths.platformSpecificOutputPath;

      StringStream compileAssetsCommand;
      compileAssetsCommand
        << "xcrun "
        << "actool \"" << assetsPath.string() << "\" "
        << "--include-all-app-icons "
        << "--compile \"" << dest.string() << "\" "
        << "--platform " << (targetPlatform == "ios" || targetPlatform == "ios-simulator" ? "iphoneos" : "macosx") << " "
        << "--minimum-deployment-target " << (targetPlatform == "ios" || targetPlatform == "ios-simulator" ? "15.0" : "10.15") << " "
        << "--app-icon AppIcon "
        << "--output-partial-info-plist "
        << "\""
        << (paths.platformSpecificOutputPath / "assets-partial-info.plist").string()
        << "\"";

      if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
        logVerbose(compileAssetsCommand.str());
      }

      auto r = exec(compileAssetsCommand.str().c_str());

      if (r.exitCode != 0) {
        logError("failed to compile car file from xcode assets. Ensure Xcode is installed and selected (xcode-select --switch /Applications/Xcode.app). See README (Troubleshooting)");
        logDebug(r.output);
        exit(1);
      }

      // if (env::get("DEBUG") != "1") fs::remove_all(assetsPath);
      logInfo("generated icons");
    };

    //
    // Darwin Package Prep
    // ---
    //
    if (platform.mac && isForDesktop) {
      logInfo("preparing build for mac");

      flags = "-std=c++2a -ObjC++ -v";
      const bool enableOpenMP = !flagCodeSign && macCompilerSupportsOpenMP();
      if (enableOpenMP) {
        flags += " -fopenmp";
      }
      flags += " -framework UniformTypeIdentifiers";
      flags += " -framework CoreBluetooth";
      flags += " -framework CoreLocation";
      flags += " -framework Network";
      flags += " -framework UserNotifications";
      flags += " -framework WebKit";
      flags += " -framework Metal";
      flags += " -framework Accelerate";
      flags += " -framework Carbon";
      flags += " -framework Cocoa";
      flags += " -framework CoreFoundation";
      flags += " -framework Foundation";
      flags += " -framework IOKit";
      flags += " -framework OSLog";
      flags += " -framework Security";
      flags += " -framework SystemConfiguration";
      flags += " -DMACOS=1";
      if (flagCodeSign) {
        flags += " -DORO_RUNTIME_PLATFORM_SANDBOXED=1";
      } else {
        flags += " -DORO_RUNTIME_PLATFORM_SANDBOXED=0";
      }
      flags += " -I\"" + Path(paths.platformSpecificOutputPath / "include").string() + "\"";
      flags += " -I" + prefixFile();
      flags += " -I" + prefixFile("include");
      flags += " -L" + prefixFile("lib/" + platform.arch + "-desktop");
      if (flagCodeSign) {
        flags += " -Wl,-rpath,@executable_path";
        flags += " -L" + prefixFile("lib/" + platform.arch + "-desktop/codesign");
      }
      flags += " -fPIC";
      flags += " " + runtimeLinkFlag();
      if (enableOpenMP) {
        flags += " -lomp";
      }
      flags += " -luv";
      flags += " -lllama";
      flags += " -lggml";
      flags += " -lggml-base";
      flags += " -lggml-blas";
      flags += " -lggml-cpu";
      flags += " -lggml-metal";
      if (fs::exists(prefixPath("lib/" + platform.arch + "-desktop/libsodium.a"))) {
        flags += " -lsodium";
      }
      if (fs::exists(prefixPath("lib/" + platform.arch + "-desktop/libusb-1.0.a"))) {
        flags += " -lusb-1.0";
      }
      if (fs::exists(prefixPath("lib/" + platform.arch + "-desktop/libwhisper.a"))) {
        flags += " -lwhisper";
      }
      if (fs::exists(prefixPath("lib/" + platform.arch + "-desktop/liboro_iroh.a"))) {
        flags += " -loro_iroh";
      }
#if ORO_RUNTIME_HAVE_LIBIPFS
      flags += " -lipfs";
      flags += " -lresolv";
#endif
      appendTLSFlags(flags);
      files += prefixFile("objects/" + platform.arch + "-desktop/desktop/main.o");
      files += prefixFile("src/init.cc");
      flags += " " + getCxxFlags();

      Path pathBase = "Contents";
      pathResources = { paths.pathPackage / pathBase / "Resources" };

      fs::create_directories(paths.pathBin);
      fs::create_directories(pathResources);

      if (settings.contains("mac_info_plist_file")) {
        const auto files = parseStringList(settings["mac_info_plist_file"]);
        for (const auto file : files) {
          settings["mac_info_plist_data"] += readFile(file);
        }
      }

      if (!settings.contains("mac_info_plist_data")) {
        settings["mac_info_plist_data"] = "";
      }

      if (flagBuildHeadless) {
        settings["mac_info_plist_data"] += (
          "  <key>LSBackgroundOnly</key>\n"
          "  <true/>\n"
        );
      }

      // determine XCode version
      do {
        auto r = exec("xcodebuild -version | head -n1 | awk '{print $2}'");
        if (r.exitCode != 0) {
          if (flagDebugMode) {
            logError("Failed to determine XCode version.");
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        settings["__xcode_version"] = trim(r.output);
      } while (0);

      // determine XCode Build version
      do {
        auto r = exec("xcodebuild -version | tail -n1 | awk '{print $3}'");
        if (r.exitCode != 0) {
          logError("Failed to determine XCode Build version code.");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        settings["__xcode_build_version"] = trim(r.output);
      } while (0);

      // determine macOS SDK build version
      do {
        auto r = exec(" xcodebuild -sdk macosx -version | grep ProductBuildVersion | awk '{ print $2 }'");
        if (r.exitCode != 0) {
          if (flagDebugMode) {
            logError("Failed to determine MacOSX SDK build version code.");
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        settings["__xcode_macosx_sdk_build_version"] = trim(r.output);
      } while (0);

      // determine macOS SDK version
      do {
        auto r = exec(" xcodebuild -sdk macosx -version | grep ProductVersion | awk '{ print $2 }'");
        if (r.exitCode != 0) {
          if (flagDebugMode) {
            logError("Failed to determine MacOSX SDK version.");
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        settings["__xcode_macosx_sdk_version"] = trim(r.output);
      } while (0);

      if (settings["mac_minimum_supported_version"].size() == 0) {
        settings["mac_minimum_supported_version"] = "13.0.0";
      }

      settings["macos_app_transport_security_domain_exceptions"] = "";

      if (settings["meta_application_links"].size() > 0) {
        const auto links = parseStringList(trim(settings["meta_application_links"]), ' ');

        for (const auto link : links) {
          auto domain = split(link, '?')[0];
          settings["macos_app_transport_security_domain_exceptions"] += (
            "      <key>" + domain + "</key>\n"
            "      <dict>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>\n"
            "        <false/>\n"
            "        <key>NSIncludesSubdomains</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionMinimumTLSVersion</key>\n"
            "        <string>1.0</string>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>\n"
            "        <false/>\n"
            "      </dict>\n"
          );
        }
      }

      if (settings["webview_insecure_domains"].size() > 0) {
        const auto links = parseStringList(trim(settings["webview_insecure_domains"]), ' ');

        for (const auto link : links) {
          auto domain = split(link, '?')[0];
          settings["macos_app_transport_security_domain_exceptions"] += (
            "      <key>" + domain + "</key>\n"
            "      <dict>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>\n"
            "        <false/>\n"
            "        <key>NSIncludesSubdomains</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionMinimumTLSVersion</key>\n"
            "        <string>1.0</string>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>\n"
            "        <false/>\n"
            "      </dict>\n"
          );
        }
      }

      auto plistInfo = tmpl(gMacOSInfoPList, settings);

      writeFile(paths.pathPackage / pathBase / "Info.plist", plistInfo);

      auto credits = tmpl(gCredits, defaultTemplateAttrs);

      writeFile(paths.pathResourcesRelativeToUserBuild / "Credits.html", credits);

      fs::create_directories(paths.pathPackage / pathBase / "MacOS");

      if (macCompilerSupportsOpenMP()) {
        const auto runtimeLibompPath = prefixPath("lib/" + platform.arch + "-desktop/codesign/libomp.dylib");
        const auto bundledLibompPath = paths.pathPackage / pathBase / "MacOS" / "libomp.dylib";

        if (!fs::exists(runtimeLibompPath)) {
          logError("missing bundled OpenMP runtime for macOS build: " + runtimeLibompPath.string());
          logError("Install an LLVM package (preferred) or the libomp package, then rebuild or relink the runtime before building this app.");
          logError("If you are using the runtime repo locally, rerun 'pnpm relink' or 'npm run relink' after installing libomp.");
          exit(1);
        }

        try {
          fs::copy(
            runtimeLibompPath,
            bundledLibompPath,
            fs::copy_options::overwrite_existing
          );
        } catch (const fs::filesystem_error& e) {
          logError("failed to stage bundled OpenMP runtime for macOS build");
          logError("source: " + runtimeLibompPath.string());
          logError("destination: " + bundledLibompPath.string());
          logError(e.what());
          exit(1);
        }
      }
    }

    if (platform.mac && isForDesktop) {
      compileIconAssets();
    }

    // used in multiple if blocks, need to declare here
    auto androidEnableStandardNdkBuild = settings["android_enable_standard_ndk_build"] == "true";

    settings.insert(std::make_pair("android_bundle_identifier", replace(settings["meta_bundle_identifier"], "-", "_")));

    if (flagBuildForAndroid) {
      auto bundle_path = Path(replace(settings["android_bundle_identifier"], "\\.", "/")).make_preferred();
      auto bundle_path_underscored = replace(replace(settings["android_bundle_identifier"], "_", "_1"), "\\.", "_");

      auto output = paths.platformSpecificOutputPath;
      auto app = output / "app";
      auto src = app / "src";
      auto jni = src / "main" / "jni";
      auto res = src / "main" / "res";
      auto pkg = src / "main" / "java" / bundle_path;
      auto runtime  = src / "main" / "java" / "oro" / "runtime";

      if (settings["android_main_activity"].size() == 0) {
        settings["android_main_activity"] = String(DEFAULT_ANDROID_MAIN_ACTIVITY_NAME);
      }

      if (settings["android_application"].size() == 0) {
        settings["android_application"] = String(DEFAULT_ANDROID_APPLICATION_NAME);
      }

      fs::create_directories(output);
      fs::create_directories(src);
      fs::create_directories(pkg);
      fs::create_directories(runtime);
      fs::create_directories(jni);
      fs::create_directories(jni / "src");
      fs::create_directories(res);
      fs::create_directories(res / "layout");
      fs::create_directories(res / "values");
      fs::create_directories(res / "mipmap");

      pathResources = { src / "main" / "assets" };

      // set current path to output directory
      if (debugEnv || verboseEnv) logVerbose("cd " + output.string());
      fs::current_path(output);

      auto androidHome = getAndroidHome();
      StringStream sdkmanager;

      if (debugEnv || verboseEnv) logVerbose("sdkmanager --version 2>&1 >/dev/null");

      sdkmanager << androidHome;
      if (env::get("ANDROID_SDK_MANAGER").size() > 0) {
        sdkmanager << "/" << env::get("ANDROID_SDK_MANAGER");
      } else if (std::system("  sdkmanager --version 2>&1 >/dev/null") != 0) {
        if (!platform.win) {
          sdkmanager << "/cmdline-tools/latest/bin/sdkmanager";
        } else {
          sdkmanager << "\\cmdline-tools\\latest\\bin\\sdkmanager.bat";
        }
      }

      // Create default output to advise user in case gradle init fails

      // TODO(mribbons): Check if we need this in CI - Upload licenses folder from workstation
      // https://developer.android.com/studio/intro/update#download-with-gradle


      // quote entire command and "yes" for windows, this is the only to get around "c:\Program" not a command error
      // note that we don't want to use this quote on non windows, therefore make an extra variable
      auto cmdQuote = platform.win ? "\"" : "";
      // redirect yes stderr to stdout, this hides "broken pipe" / "no space left on device" errors that are caused by sdkmanager terminating normally
      String licenseAccept =
      cmdQuote + quote + (env::get("ANDROID_SDK_MANAGER_ACCEPT_LICENSES").size() > 0 ? (env::get("ANDROID_SDK_MANAGER_ACCEPT_LICENSES")) : "echo") + quote  +
      (platform.win ? " 2>&1" : "") + " | " +
      quote + sdkmanager.str() + quote + " --licenses" + cmdQuote;

      auto licenseResult = exec(licenseAccept.c_str());
      if (licenseResult.exitCode != 0) {
        logWarn(licenseResult.output);
        logWarn(
          String("Check licenses and run again: \n") +
          licenseAccept +
          "\n"
        );
      }

      // TODO: internal, no need to save to settings
      settings["android_sdk_manager_path"] = sdkmanager.str();

      String gradlePath = env::get("GRADLE_HOME").size() > 0 ? env::get("GRADLE_HOME") + slash + "bin" + slash : "";

      StringStream gradleInitCommand;
      gradleInitCommand
        << gradlePath
        << "gradle "
        << "--no-configuration-cache "
        << "--no-build-cache "
        << "--no-scan "
        << "--offline "
        << "--quiet "
        << "init "
        << "--dsl groovy "
        << "--use-defaults "
        << "--overwrite";

      if (debugEnv || verboseEnv) logVerbose(gradleInitCommand.str());
      if (std::system(gradleInitCommand.str().c_str()) != 0) {
        logError("failed to invoke `gradle init` command. Ensure Gradle and JDK are installed, and accept Android SDK licenses. See README (Troubleshooting)");
        // In case user didn't accept licenses above
        logWarn(
          String("Check licenses and run again: \n") +
          (platform.win ? "set" : "export") +
          (" JAVA_HOME=\"" + env::get("JAVA_HOME") + "\" && ") +
          licenseAccept +
          "\n"
         );
        exit(1);
      }

      fs::copy(prefixPath("src/init.cc"), jni, fs::copy_options::overwrite_existing);

      fs::copy(
        prefixPath("include"),
        jni / "include",
        fs::copy_options::overwrite_existing | fs::copy_options::recursive
      );

      writeFile(jni / "include" / "oro" / "_user-config-bytes.hh", settings["ini_code"]);

      auto aaptNoCompressOptionsNormalized = std::vector<String>();
      auto aaptNoCompressDefaultOptions = split(R"OPTIONS("htm","html","txt","json","jsonld","js","jsx","mjs","ts","tsx","css","xml","wasm")OPTIONS", ',');
      auto aaptNoCompressOptions = parseStringList(settings["android_aapt_no_compress"]);

      settings["android_aapt_no_compress"] = "";

      for (auto const &option : aaptNoCompressOptions) {
        auto value = trim(option);

        if ('"' != value[0]) {
          value = "\"" + value + "\"";
        }

        if (settings["android_aapt_no_compress"].size() > 0) {
          settings["android_aapt_no_compress"] += ", ";
        }

        settings["android_aapt_no_compress"] = value;
        aaptNoCompressOptionsNormalized.push_back(value);
      }

      for (auto const &option : aaptNoCompressDefaultOptions) {
        auto value = trim(option);


        auto cursor = std::find(
          aaptNoCompressOptionsNormalized.begin(),
          aaptNoCompressOptionsNormalized.end(),
          trim(option)
        );

        if (cursor == aaptNoCompressOptionsNormalized.end()) {
          if (settings["android_aapt_no_compress"].size() > 0) {
            settings["android_aapt_no_compress"] += ", ";
          }

          settings["android_aapt_no_compress"] += value;
        }
      }

      if (settings["android_aapt"].size() == 0) {
        settings["android_aapt"] = "";
      }

      if (settings["android_native_abis"].size() == 0) {
        settings["android_native_abis"] = env::get("ANDROID_SUPPORTED_ABIS");
      }

      if (settings["android_ndk_abi_filters"].size() == 0) {
        settings["android_ndk_abi_filters"] = settings["android_native_abis"];
      }

      if (settings["android_ndk_abi_filters"].size() == 0) {
        settings["android_ndk_abi_filters"] = "'arm64-v8a', 'x86_64'";
      } else {
        auto filters = settings["android_ndk_abi_filters"];
        settings["android_ndk_abi_filters"] = "";

        for (auto const &filter : parseStringList(filters)) {
          auto value = trim(replace(filter, "\"", "'"));

          if (value.size() == 0) {
            continue;
          }

          if (value[0] != '\'') {
            value = "'" + value;
          }

          if (value[value.size() - 1] != '\'') {
            value += "'";
          }

          if (settings["android_ndk_abi_filters"].size() > 0) {
            settings["android_ndk_abi_filters"] += ", ";
          }

          settings["android_ndk_abi_filters"] += value;
        }
      }

      Map<> manifestContext;

      manifestContext["android_manifest_xml_permissions"] = "\n";

      if (settings["permissions_allow_notifications"] != "false") {
        manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.POST_NOTIFICATIONS\" />\n";
      }

      if (settings["permissions_allow_geolocation"] != "false") {
        manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.ACCESS_FINE_LOCATION\" />\n";
        manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.ACCESS_COARSE_LOCATION\" />\n";
        if (settings["permissions_allow_geolocation_in_background"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.ACCESS_BACKGROUND_LOCATION\" />\n";
        }
      }

      if (settings["permissions_allow_user_media"] != "false") {
        if (settings["permissions_allow_camera"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.CAMERA\" />\n";
        }

        if (settings["permissions_allow_microphone"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.CAPTURE_AUDIO_OUTPUT\" />\n";
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.MODIFY_AUDIO_SETTINGS\" />\n";
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.RECORD_AUDIO\" />\n";
        }
      }

      if (settings["permissions_allow_read_media"] != "false") {
        manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.READ_EXTERNAL_STORAGE\" android:maxSdkVersion=\"32\" />\n";
        manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\" android:maxSdkVersion=\"29\" />\n";
        if (settings["permissions_allow_read_media_images"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.READ_MEDIA_IMAGES\" />\n";
        }

        if (settings["permissions_allow_read_media_video"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.READ_MEDIA_VIDEO\" />\n";
        }

        if (settings["permissions_allow_read_media_audio"] != "false") {
          manifestContext["android_manifest_xml_permissions"] += "  <uses-permission android:name=\"android.permission.READ_MEDIA_AUDIO\" />\n";
        }
      }

      if (settings["android_manifest_permissions"].size() > 0) {
        settings["android_manifest_permissions"] = replace(settings["android_manifest_permissions"], ",", " ");
        for (auto const &value : parseStringList(settings["android_manifest_permissions"])) {
          auto permission = replace(trim(value), "\"", "");
          StringStream xml;

          std::transform(permission.begin(), permission.end(), permission.begin(), ::toupper);

          xml
            << "  <uses-permission android:name="
            << "\"android.permission." << permission << "\""
            << " />";

          manifestContext["android_manifest_xml_permissions"] += xml.str() + "\n";
        }
      }

      if (settings["android_allow_cleartext"].size() == 0) {
        if (flagDebugMode) {
          settings["android_allow_cleartext"] = "android:usesCleartextTraffic=\"true\"\n";
        } else {
          settings["android_allow_cleartext"] = "";
        }
      }

      settings["android_activity_intent_filters"] = "";
      if (settings["meta_application_links"].size() > 0) {
        const auto links = parseStringList(trim(settings["meta_application_links"]), ' ');
        for (const auto link : links) {
          const auto parts = split(link, '?');
          const auto host = parts[0];
          settings["android_activity_intent_filters"] += (
            "        <intent-filter android:autoVerify=\"true\">\n"
            "          <action android:name=\"android.intent.action.VIEW\" />\n"
            "          <category android:name=\"android.intent.category.DEFAULT\" />\n"
            "          <category android:name=\"android.intent.category.BROWSABLE\" />\n"
            "          <data android:scheme=\"http\" />\n"
            "          <data android:scheme=\"https\" />\n"
            "          <data android:host=\"" + host + "\" />\n"
            "        </intent-filter>\n"
          );
        }
      }

      // set internal variables used by templating system to generate build.gradle
      if (androidEnableStandardNdkBuild) {
        settings["android_default_config_external_native_build"].assign(
          "    externalNativeBuild {\n"
          "      ndkBuild {\n"
          "        arguments \"NDK_APPLICATION_MK:=src/main/jni/Application.mk\"\n"
          "      }\n"
          "    }"
        );

        settings["android_external_native_build"].assign(
          "  externalNativeBuild {\n"
          "    ndkBuild {\n"
          "      path \"src/main/jni/Android.mk\"\n"
          "    }\n"
          "  }"
        );
      } else {
        settings["android_default_config_external_native_build"].assign("    // externalNativeBuild called manually for -j parallel support. Disable with [android]...enable_standard_ndk_build = true in your config (oro.toml)\n");
        settings["android_external_native_build"].assign("  // externalNativeBuild called manually for -j parallel support. Disable with [android]...enable_standard_ndk_build = true in your config (oro.toml)\n");
      }

      auto androidResources = settings["android_resources"];
      auto androidIcon = settings["android_icon"];

      if (androidIcon.size() > 0) {
        settings["android_application_icon_config"] = (
          String("  android:roundIcon=\"@mipmap/ic_launcher_round\"\n") +
          String("  android:icon=\"@mipmap/ic_launcher\"\n")
        );

        fs::copy(targetPath / androidIcon, res / "mipmap" / "icon.png", fs::copy_options::overwrite_existing);
        fs::copy(targetPath / androidIcon, res / "mipmap" / "ic_launcher.png", fs::copy_options::overwrite_existing);
        fs::copy(targetPath / androidIcon, res / "mipmap" / "ic_launcher_round.png", fs::copy_options::overwrite_existing);
      } else {
        settings["android_application_icon_config"] = "";
      }

      // Android Project
      writeFile(
        src / "main" / "AndroidManifest.xml",
        trim(tmpl(tmpl(gAndroidManifest, settings), manifestContext))
      );

      settings["android_signingconfig_release"] = "";
      settings["android_buildtypes_release_config"] = "";

      if (
        settings["android_codesign_keystore_file"].size() > 0 &&
        settings["android_codesign_keystore_password"].size() > 0 &&
        settings["android_codesign_key_alias"].size() > 0 &&
        settings["android_codesign_key_password"].size()
      ) {
        settings["android_signingconfig_release"] += "     storeFile file('{{android_codesign_keystore_file}}')\n";
        settings["android_signingconfig_release"] += "     storePassword '{{android_codesign_keystore_password}}'\n";
        settings["android_signingconfig_release"] += "     keyAlias '{{android_codesign_key_alias}}'\n";
        settings["android_signingconfig_release"] += "     keyPassword '{{android_codesign_key_password}}'\n";
        settings["android_buildtypes_release_config"] += "     signingConfig signingConfigs.release\n";
      }


      writeFile(app / "proguard-rules.pro", trim(tmpl(gProGuardRules, settings)));
      writeFile(app / "build.gradle", trim(tmpl(gGradleBuildForSource, settings)));

      writeFile(output / "settings.gradle", trim(tmpl(gGradleSettings, settings)));
      writeFile(output / "build.gradle", trim(tmpl(gGradleBuild, settings)));
      writeFile(output / "gradle.properties", trim(tmpl(gGradleProperties, settings)));

      writeFile(res / "layout" / "web_view.xml", trim(tmpl(gAndroidLayoutWebView, settings)));
      writeFile(res / "layout" / "window_container_view.xml", trim(tmpl(gAndroidLayoutWindowContainerView, settings)));
      writeFile(res / "values" / "strings.xml", trim(tmpl(gAndroidValuesStrings, settings)));

      // allow user space to override all `res/` files
      if (fs::exists(androidResources)) {
        fs::copy(androidResources, res, fs::copy_options::overwrite_existing);
      }

      auto cflags = flagDebugMode
        ? settings.count("debug_flags") ? settings["debug_flags"] : ""
        : settings.count("build_flags") ? settings["build_flags"] : "";

      StringStream pp;
      pp
        << "-DDEBUG=" << (flagDebugMode ? 1 : 0) << " "
        << "-DANDROID=1" << " "
        << "-DORO_RUNTIME_VERSION=" << VERSION_STRING << " "
        << "-DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING << " ";

      Map<> makefileContext;

      makefileContext["cflags"] = cflags + " " + pp.str();
      makefileContext["cflags"] += " " + settings["android_native_cflags"];
      makefileContext["__android_native_extensions_context"] = "";

      if (settings["android_native_abis"].size() > 0) {
        makefileContext["android_native_abis"] = settings["android_native_abis"];
      } else {
        makefileContext["android_native_abis"] = "arm64-v8a x86_64";
      }

      // custom native sources
      for (
        auto const &file :
        parseStringList(settings["android_native_sources"], ' ')
      ) {
        auto filename = Path(file).filename();
        writeFile(
          jni / "src" / filename,
          tmpl(std::regex_replace(
            convertWStringToString(readFile(targetPath / file )),
            std::regex("__BUNDLE_IDENTIFIER__"),
            settings["android_bundle_identifier"]
          ), settings)
        );
      }

      Vector<String> seenExtensions;
      for (const auto& tuple : settings) {
        auto& key = tuple.first;
        if (tuple.second.size() == 0) continue;
        if (key.starts_with("build_extensions_")) {
          if (key.find("compiler_flags") != String::npos) continue;
          if (key.find("compiler_debug_flags") != String::npos) continue;
          if (key.find("linker_flags") != String::npos) continue;
          if (key.find("linker_debug_flags") != String::npos) continue;
          if (key.find("configure_script") != String::npos) continue;
          if (key.starts_with("build_extensions_mac_")) continue;
          if (key.starts_with("build_extensions_linux_")) continue;
          if (key.starts_with("build_extensions_win_")) continue;
          if (key.starts_with("build_extensions_ios_")) continue;
          if (key.ends_with("_path")) continue;

          String extension;
          if (key.starts_with("build_extensions_android")) {
            extension = replace(key, "build_extensions_android_", "");
          } else {
            extension = replace(key, "build_extensions_", "");
          }

          extension = split(extension, '_')[0];
          fs::current_path(targetPath);

          if (std::find(seenExtensions.begin(), seenExtensions.end(), extension) != seenExtensions.end()) {
            continue;
          }
          seenExtensions.push_back(extension);

          const auto extensionKey = "build_extensions_" + extension;
          const auto scopedExtensionKey = "build_extensions_android_" + extension;
          const auto suffixScopedExtensionKey = extensionKey + "_android";
          auto source = settings[extensionKey + "_source"];
          if (source.size() == 0) {
            source = settings[scopedExtensionKey + "_source"];
          }
          auto oldCwd = fs::current_path();
          fs::current_path(targetPath);

          if (source.size() == 0) {
            for (const auto& sourceKey : {extensionKey, scopedExtensionKey, suffixScopedExtensionKey}) {
              if (fs::is_directory(settings[sourceKey])) {
                source = settings[sourceKey];
                settings[sourceKey] = "";
                break;
              }
            }
          }

          if (source.size() > 0) {
            Path target;
            if (fs::exists(source)) {
              target = source;
            } else if (source.ends_with(".git")) {
              auto path = Path { source };
              target = paths.platformSpecificOutputPath / "extensions" / replace(path.filename().string(), ".git", "");

              if (!fs::exists(target)) {
                if (!ensureExecAllowed(settings, "build")) exit(1);
                auto isSafeSource = [](const String& s) {
                  if (s.find("..") != String::npos) return false;
                  if (s.find(';') != String::npos) return false;
                  if (s.find('&') != String::npos) return false;
                  if (s.find('|') != String::npos) return false;
                  if (s.find('`') != String::npos) return false;
                  if (s.find("$") != String::npos) return false;
                  if (s.find("\n") != String::npos || s.find("\r") != String::npos) return false;
                  return (
                    s.starts_with("https://") ||
                    s.starts_with("ssh://") ||
                    s.starts_with("git@") ||
                    s.starts_with("file://")
                  );
                };
                if (!isSafeSource(source)) {
                  logError("unsafe extension source for git clone: " + source);
                  exit(1);
                }
                auto exitCode = std::system(("git clone " + source + " " + target.string()).c_str());

                if (exitCode) {
                  exit(exitCode);
                }
              }
            }

            if (fs::exists(target)) {
              Map<> config;
              Path extensionConfigPath;
              auto extensionFormat = UserConfigFormat::Ini;
              const auto extensionToml = target / "oro.toml";
              const auto extensionOroIni = target / "oro.ini";

              if (fileExists(extensionToml)) {
                extensionConfigPath = extensionToml;
                extensionFormat = UserConfigFormat::Toml;
              } else if (fileExists(extensionOroIni)) {
                extensionConfigPath = extensionOroIni;
                extensionFormat = UserConfigFormat::Ini;
              }

              if (!extensionConfigPath.empty()) {
                config = parseUserConfigSource(readFile(extensionConfigPath), extensionFormat);
              }

              settings["build_extensions_" + extension + "_path"] = target.string();

              for (const auto& entry : config) {
                if (entry.first.starts_with("extension_sources")) {
                  settings["build_extensions_" + extension] += entry.second;
                } else if (entry.first.starts_with("extension_")) {
                  auto key = replace(entry.first, "extension_", extension + "_");
                  auto value = entry.second;
                  auto index = "build_extensions_" + key;
                  if (settings[index].size() > 0) {
                    settings[index] += " " + value;
                  } else {
                    settings[index] = value;
                  }
                }
              }
            }
          }

          auto compilerFlags = replace(
            settings["build_extensions_compiler_flags"] + " " +
            settings["build_extensions_android_compiler_flags"] + " " +
            settings["build_extensions_" + extension + "_compiler_flags"] + " " +
            settings["build_extensions_" + extension + "_android_compiler_flags"] +  " ",
            "-I",
            "-I$(LOCAL_PATH)/"
          );

          auto compilerDebugFlags = replace(
            settings["build_extensions_compiler_debug_flags"] + " " +
            settings["build_extensions_android_compiler_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_android_compiler_debug_flags"] +  " ",
            "-I",
            "-I$(LOCAL_PATH)/"
          );

          auto linkerFlags = replace(
            settings["build_extensions_linker_flags"] + " " +
            settings["build_extensions_android_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_android_linker_flags"] +  " ",
            "-I",
            "-I$(LOCAL_PATH)/"
          );

          auto linkerDebugFlags = replace(
            settings["build_extensions_linker_debug_flags"] + " " +
            settings["build_extensions_android_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_android_linker_debug_flags"] + " ",
            "-I",
            "-I$(LOCAL_PATH)/"
          );

          auto configure = settings["build_extensions_" + extension + "_configure_script"];
          auto build = settings["build_extensions_" + extension + "_build_script"];
          auto copy = settings["build_extensions_" + extension + "_build_copy"];
          auto path = settings["build_extensions_" + extension + "_path"];
          auto target = settings["build_extensions_" + extension + "_target"];

          auto sources = StringStream();
          auto make = StringStream();
          auto cwd = targetPath;
          std::unordered_set<String> cflags;
          std::unordered_set<String> cppflags;

          compilerFlags += " -DORO_RUNTIME_EXTENSION=1";

          if (path.size() > 0) {
            fs::current_path(targetPath);
            fs::current_path(path);
          } else {
            fs::current_path(targetPath);
          }

          if (build.size() > 0) {
            if (!ensureExecAllowed(settings, "build")) exit(1);
            auto exitCode = std::system((build + argvForward).c_str());
            if (exitCode) {
              exit(exitCode);
            }
          }

          if (configure.size() > 0) {
            auto output = replace(exec(configure + argvForward).output, "\n", " ");
            if (output.size() > 0) {
              for (const auto& source : parseStringList(output, ' ')) {
                auto destination = fs::absolute(targetPath / source);
                sources << destination.string() << " ";
              }
            }
          }

          if (copy.size() > 0) {
            auto pathResources = paths.pathResourcesRelativeToUserBuild;
            for (const auto& file : parseStringList(copy, ' ')) {
              auto parts = split(file, ':');
              auto target = parts[0];
              auto destination = parts.size() == 2 ? pathResources / parts[1] : pathResources;
              fs::create_directories(destination.parent_path());
              fs::copy(
                target,
                destination,
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
            }
          }

          auto CXX = env::get("CXX");
          auto CC = env::get("CC");

          for (
            auto source : parseStringList(
              trim(
                settings[extensionKey] + " " +
                settings[scopedExtensionKey] + " " +
                settings[suffixScopedExtensionKey]
              ),
              ' '
            )
          ) {
            if (target == "wasm32" || target == "wasm32-wasi") {
              // just build sources
              sources << (fs::current_path() / source).string() << " ";
              continue;
            }

            if (fs::is_directory(source)) {
              auto target = Path(source).filename();
              fs::create_directories(jni / target);
              fs::copy(
                source,
                jni / target,
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
              continue;
            } else if (!fs::is_regular_file(source)) {
              continue;
            }

            auto destination = (
              Path(source).parent_path() /
              Path(source).filename().string()
            ).string();

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logDebug("extension source: " + source);
            }

            fs::create_directories(jni / Path(destination).parent_path());
            fs::copy(source, jni / destination, fs::copy_options::overwrite_existing);

            if (destination.ends_with(".hh") || destination.ends_with(".h")) {
              continue;
            } else if (source.ends_with(".wasm")) {
              fs::copy(
                source,
                (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm")),
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
              continue;
            } else if (destination.ends_with(".cc") || destination.ends_with(".cpp") || destination.ends_with(".c++") || destination.ends_with(".mm")) {
              cppflags.insert("-std=c++2a");
              cppflags.insert("-fexceptions");
              cppflags.insert("-frtti");
              cppflags.insert("-fsigned-char");
            }

            sources << destination << " ";
          }

          fs::current_path(oldCwd);

          Vector<String> libs;
          auto archs = settings["android_native_abis"];

          if (archs.size() == 0) {
            archs = "arm64-v8a x86_64";
          }

          for (const auto& source : parseStringList(sources.str(), ' ')) {
            if (source.ends_with(".a")) {
              auto name = replace(Path(source).filename().string(), ".a", "");
              for (const auto& arch : parseStringList(archs, ' ')) {
                if (source.find(arch) != -1) {
                  fs::create_directories(jni / ".." / "libs" / arch);
                  fs::copy(source, jni / ".." / "libs" / arch / Path(source).filename(), fs::copy_options::overwrite_existing);
                }
              }

              if (std::find(libs.begin(), libs.end(), name) == libs.end()) {
                libs.push_back(name);
                make << "## " << Path(source).filename().string() << std::endl;
                make << "include $(CLEAR_VARS)" << std::endl;
                make << "LOCAL_MODULE := " << name << std::endl;
                make << "LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/" << Path(source).filename().string() << std::endl;
                make << "include $(PREBUILT_STATIC_LIBRARY)" << std::endl;
              }
            }
          }

          if (target == "wasm32") {
            String compiler;
            auto compilerFlags = replace(
              settings["build_extensions_compiler_flags"] + " " +
              settings["build_extensions_android_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_android_compiler_flags"] +  " ",
              "-I",
              "-I$(LOCAL_PATH)/"
            );

            auto compilerDebugFlags = replace(
              settings["build_extensions_compiler_debug_flags"] + " " +
              settings["build_extensions_android_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_android_compiler_debug_flags"] +  " ",
              "-I",
              "-I$(LOCAL_PATH)/"
            );

            compilerFlags += " -DORO_RUNTIME_EXTENSION=1";
            compiler = CXX.size() > 0 ? CXX : "clang++";
            compilerFlags += " -v";
            compilerFlags += " -std=c++2a -v";
            if (platform.mac) {
              compilerFlags += " -ObjC++";
            } else if (platform.win) {
              compilerFlags += " -stdlib=libstdc++";
            }

            if (platform.win || platform.linux) {
              compilerFlags += " -Wno-unused-command-line-argument";
            }

            if (compiler.ends_with("clang++")) {
              compiler = compiler.substr(0, compiler.size() - 2);
            } else if (compiler.ends_with("clang++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + ".exe";
            } else if (compiler.ends_with("g++")) {
              compiler = compiler.substr(0, compiler.size() - 2) + "cc";
            } else if (compiler.ends_with("g++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + "cc.exe";
            }
            auto compileExtensionWASMCommand = StringStream();
            auto lib = (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm"));
            fs::create_directories(lib.parent_path());
            compileExtensionWASMCommand
              << compiler
              << " -I" + Path(paths.platformSpecificOutputPath / "include").string()
              << (" -I" + quote + trim(prefixFile("include")) + quote)
              << (" -I" + quote + trim(prefixFile("include/oro/webassembly")) + quote)
              << (" -I" + quote + trim(prefixFile("src")) + quote)
              << (" -L" + quote + trim(prefixFile("lib")) + quote)
              << " -DORO_RUNTIME_EXTENSION_WASM=1"
              << " --target=wasm32"
              << " --no-standard-libraries"
              << " -Wl,--import-memory"
              << " -Wl,--allow-undefined"
              << " -Wl,--export-all"
              << " -Wl,--no-entry"
              << " -Wl,--demangle"
              << (" -L" + quote + trim(prefixFile("lib/" + platform.arch + "-desktop")) + quote)
              << " -fvisibility=hidden"
              << " -DIOS=0"
              << " -DANDROID=1"
              << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
              << " -DHOST=" << "\\\"" << devHost << "\\\""
              << " -DPORT=" << devPort
              << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
              << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
              << " " << trim(compilerFlags + " " + (flagDebugMode ? compilerDebugFlags : ""))
              << " " << sources.str()
              << " -o " << lib.string();

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logVerbose(compileExtensionWASMCommand.str());
            }

            do {
              auto r = exec(compileExtensionWASMCommand.str());

              if (r.exitCode != 0) {
                logError("Unable to build WASM extension (" + extension + ")");
                logDebug(r.output);
                exit(r.exitCode);
              }
            } while (0);
            continue;
          } else if (target == "wasm32-wasi") {
            // TODO
            continue;
          }

          make << "## oro/extensions/" << extension << ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME << std::endl;
          make << "include $(CLEAR_VARS)" << std::endl;
          make << "LOCAL_MODULE := extension-" << extension << std::endl;
          make << std::endl;
          make << "LOCAL_CFLAGS += \\" << std::endl;
          for (const auto& cflag : cflags) {
            make << "  " << cflag << " \\" << std::endl;
          }
          if (flagDebugMode) {
            make << "  -g                         \\" << std::endl;
          }
          make << "  -I$(LOCAL_PATH)/include    \\" << std::endl;
          make << "  -I$(LOCAL_PATH)            \\" << std::endl;
          make << "  -pthreads                  \\" << std::endl;
          make << "  -fPIC                      \\" << std::endl;
          make << "  -O0" << std::endl;
          make << std::endl;

          make << "LOCAL_CFLAGS += \\" << std::endl;
          make << "  -DDEBUG=" << (flagDebugMode ? 1 : 0) << " \\" << std::endl;
          make << "  -DANDROID=1" << " \\" << std::endl;
          make << "  -DORO_RUNTIME_VERSION=" << VERSION_STRING << " \\" << std::endl;
          make << "  -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING << std::endl;
          make << std::endl;

          if (compilerFlags.size() > 0) {
            make << "LOCAL_CFLAGS +=  \\" << std::endl;
            make << compilerFlags << std::endl;
            make << std::endl;
          }

          if (flagDebugMode && compilerDebugFlags.size() > 0) {
            make << "LOCAL_CFLAGS +=  \\" << std::endl;
            make << compilerDebugFlags << std::endl;
            make << std::endl;
          }

          if (linkerFlags.size() > 0) {
            make << "LOCAL_LDFLAGS +=  \\" << std::endl;
            make << linkerFlags << std::endl;
            make << std::endl;
          }

          if (flagDebugMode && linkerDebugFlags.size() > 0) {
            make << "LOCAL_LDFLAGS +=  \\" << std::endl;
            make << linkerDebugFlags << std::endl;
            make << std::endl;
          }

          if (flags.size() > 0) {
            make << "LOCAL_CFLAGS += " << flags << std::endl;
          }

          if (settings["android_native_cflags"].size() > 0) {
            make << "LOCAL_CFLAGS += " << settings["android_native_cflags"] << std::endl;
          }

          make << "LOCAL_CPPFLAGS += \\" << std::endl;
          for (const auto& cppflag : cppflags) {
            make << "  " << cppflag << " \\" << std::endl;
          }
          make << std::endl;

          make << "LOCAL_LDLIBS += -landroid -llog -lz" << std::endl;

          for (const auto& lib : libs) {
            make << "LOCAL_STATIC_LIBRARIES += " << lib << std::endl;
          }
          make << std::endl;

          make
            << "LOCAL_STATIC_LIBRARIES += liboro-runtime-static libuv libllama "
            << "libwhisper libggml libggml-cpu libggml-base libusb libsodium"
            << std::endl;
          make << "LOCAL_SRC_FILES = init.cc" <<  std::endl;

          for (const auto& source : parseStringList(sources.str(), ' ')) {
            if (source.ends_with(".c") || source.ends_with(".cc") || source.ends_with(".mm") || source.ends_with(".m")) {
              make << "LOCAL_SRC_FILES += " << source <<  std::endl;
            }
          }

          make << std::endl;

          make << "include $(BUILD_SHARED_LIBRARY)" << std::endl;
          make << std::endl;

          makefileContext["__android_native_extensions_context"] += make.str();
        }
      }

      if (settings["android_native_makefile"].size() > 0) {
        makefileContext["android_native_make_context"] =
          trim(tmpl(tmpl(convertWStringToString(readFile(targetPath / settings["android_native_makefile"])), settings), makefileContext));
      } else {
        makefileContext["android_native_make_context"] = "";
      }

      writeFile(
        jni / "Application.mk",
        trim(tmpl(tmpl(gAndroidApplicationMakefile, makefileContext), settings))
      );

      writeFile(
        jni / "Android.mk",
        trim(tmpl(tmpl(gAndroidMakefile, makefileContext), settings))
      );

      // Android Sources
      std::map<Path, String> sources = {
        // android app package files
        {pkg / "app.kt", "src/android/app.kt"},
        {pkg / "main.kt", "src/android/main.kt"},
        {pkg / "UsbService.kt", "src/android/UsbService.kt"},

        // runtime package files
        {runtime / "app" / "app.kt", "src/runtime/app/app.kt"},
        {runtime / "bridge" / "bridge.kt", "src/runtime/bridge/bridge.kt"},
        {runtime / "debug" / "console.kt", "src/runtime/debug/console.kt"},
        {runtime / "ipc" / "message.kt", "src/runtime/ipc/message.kt"},
        {runtime / "securestorage" / "secure_storage.kt", "src/runtime/securestorage/secure_storage.kt"},
        {runtime / "usb" / "oro.kt", "src/runtime/usb/oro.kt"},
        {runtime / "usb" / "usb.kt", "src/runtime/usb/usb.kt"},
        {runtime / "webview.kt", "src/runtime/webview.kt"},
        {runtime / "webview" / "navigator.kt", "src/runtime/webview/navigator.kt"},
        {runtime / "webview" / "scheme_handlers.kt", "src/runtime/webview/scheme_handlers.kt"},
        {runtime / "window" / "dialog.kt", "src/runtime/window/dialog.kt"},
        {runtime / "window" / "manager.kt", "src/runtime/window/manager.kt"},
        {runtime / "window" / "window.kt", "src/runtime/window/window.kt"},
      };

      for (const auto& entry : sources) {
        const auto filename = prefixPath(entry.second);
        const auto value = convertWStringToString(readFile(filename));
        fs::create_directories(entry.first.parent_path());
        writeFile(
          entry.first,
          replace(
            value,
            "__BUNDLE_IDENTIFIER__",
            settings["android_bundle_identifier"]
          )
        );
      }

      // custom source files
      for (auto const &file : parseStringList(settings["android_sources"])) {
        // log(String("Android source: " + String(target / file)).c_str());
        writeFile(
          pkg / Path(file).filename(),
          tmpl(
            convertWStringToString(readFile(targetPath / file )),
            settings
          )
        );
      }
    }

    if (flagBuildForIOS && !platform.mac) {
      logError("Building for iOS on a non-mac platform is unsupported");
      exit(1);
    }

    if (platform.mac && flagBuildForIOS) {
      auto projectName = (settings["build_name"] + ".xcodeproj");
      auto schemeName = (settings["build_name"] + ".xcscheme");
      auto pathToProject = paths.platformSpecificOutputPath / projectName;
      auto pathToScheme = pathToProject / "xcshareddata" / "xcschemes";
      auto pathToProfile = fs::absolute(targetPath / settings["ios_provisioning_profile"]);

      fs::create_directories(pathToProject);
      fs::create_directories(pathToScheme);

      compileIconAssets();

      if (!flagBuildForSimulator) {
        if (!fs::exists(pathToProfile)) {
          logError("provisioning profile not found: " + pathToProfile.string() + ". " +
              "Please specify a valid provisioning profile in the " +
              "provisioning_profile field in the [ios] section of your configuration file (oro.toml or oro.ini). " +
              "You can also set 'ios_provisioning_specifier'." + helpHint("build"));
          exit(1);
        }
        String command = (
          "security cms -D -i " + pathToProfile.string()
        );

        if (env::has("APPLE_KEYCHAIN")) {
          command += " -k" + env::get("APPLE_KEYCHAIN");
        }

        auto r = exec(command);
        std::regex reUuid(R"(<key>UUID<\/key>\n\s*<string>(.*)<\/string>)");
        std::smatch matchUuid;

        if (!std::regex_search(r.output, matchUuid, reUuid)) {
          logError("failed to extract uuid from provisioning profile using \"" + command + "\". Ensure the provisioning profile path is correct and 'security' tools are available.");
          exit(1);
        }

        String uuid = matchUuid.str(1);

        std::regex reProvSpec(R"(<key>Name<\/key>\n\s*<string>(.*)<\/string>)");
        std::smatch matchProvSpec;

        if (!std::regex_search(r.output, matchProvSpec, reProvSpec)) {
          logError("failed to extract Provisioning Specifier from provisioning profile using \"" + command + "\". Ensure the provisioning profile is valid.");
          exit(1);
        }

        String provSpec = matchProvSpec.str(1);

        std::regex reTeamId(R"(<key>com\.apple\.developer\.team-identifier<\/key>\n\s*<string>(.*)<\/string>)");
        std::smatch matchTeamId;

        if (!std::regex_search(r.output, matchTeamId, reTeamId)) {
          logError("failed to extract Team Id from provisioning profile using \"" + command + "\". Ensure the provisioning profile is valid.");
          exit(1);
        }

        String team = matchTeamId.str(1);

        auto pathToInstalledProfile = Path(env::get("HOME")) /
          "Library" /
          "MobileDevice" /
          "Provisioning Profiles" /
          (uuid + ".mobileprovision");

        if (fs::exists(pathToInstalledProfile)) {
          fs::remove_all(pathToInstalledProfile);
        }

        fs::copy(pathToProfile, pathToInstalledProfile);

        settings["ios_provisioning_specifier"] = provSpec;
        settings["ios_provisioning_profile"] = uuid;
        settings["apple_team_identifier"] = team;
      }

      if (flagBuildForSimulator) {
        settings["ios_provisioning_specifier"] = "";
        settings["ios_provisioning_profile"] = "";
        settings["ios_codesign_identity"] = "";
        settings["apple_team_identifier"] = "";
      }

      if (settings["ios_sdkroot"].size() == 0) {
        if (flagBuildForSimulator) {
          settings["ios_sdkroot"] = "iphonesimulator";
        } else {
          settings["ios_sdkroot"] = "iphoneos";
        }
      }

      // iOS devices use arm64; simulator bundles must match the macOS host.
      auto arch = String(flagBuildForSimulator ? platform.arch : "arm64");
      auto deviceType = arch + "-iPhone" + (flagBuildForSimulator ? "Simulator" : "OS");

      auto deviceLibs = prefixPath("lib") / deviceType;
      auto deviceObjects = prefixPath("objects") / deviceType;

      if (!fs::exists(deviceLibs)) {
        logError("libs folder for the target platform doesn't exist: " + deviceLibs.string());
      }

      if (!fs::exists(deviceObjects)) {
        logError("objects folder for the target platform doesn't exist: " + deviceObjects.string());
      }

      if (!fs::exists(deviceLibs) || !fs::exists(deviceObjects)) {
        exit(1);
      }

      fs::copy(
        deviceLibs,
        paths.platformSpecificOutputPath / "lib",
        fs::copy_options::overwrite_existing | fs::copy_options::recursive
      );

      fs::copy(
        deviceObjects,
        paths.platformSpecificOutputPath / "objects",
        fs::copy_options::overwrite_existing | fs::copy_options::recursive
      );

      fs::copy(
        prefixPath("include"),
        paths.platformSpecificOutputPath / "include",
        fs::copy_options::overwrite_existing | fs::copy_options::recursive
      );

      writeFile(
        paths.platformSpecificOutputPath / "include" / "oro" / "_user-config-bytes.hh",
        settings["ini_code"]
      );

      Map<> xCodeProjectVariables = settings;
      extendMap(xCodeProjectVariables, Map<> {
        {"ORO_RUNTIME_VERSION", VERSION_STRING},
        {"ORO_RUNTIME_VERSION_HASH", VERSION_HASH_STRING},
        {"ORO_RUNTIME_PLATFORM_SANDBOXED", flagCodeSign ? "1" : "0"},
        {"__ios_native_extensions_build_ids", ""},
        {"__ios_native_extensions_build_refs", ""},
        {"__ios_native_extensions_build_context_refs", ""},
        {"__ios_native_extensions_build_context_sections", ""}
      });

      fs::create_directories(paths.pathResourcesRelativeToUserBuild / "oro" / "extensions");
      auto iosSdkPath = trim(flagBuildForSimulator
        ? exec("xcrun -sdk iphonesimulator -show-sdk-path").output
        : exec("xcrun -sdk iphoneos -show-sdk-path").output
      );

      auto extensions = Vector<String>();
      for (const auto& tuple : settings) {
        auto& key = tuple.first;
        if (tuple.second.size() == 0) continue;
        if (key.starts_with("build_extensions_")) {
          if (key.find("compiler_flags") != String::npos) continue;
          if (key.find("compiler_debug_flags") != String::npos) continue;
          if (key.find("linker_flags") != String::npos) continue;
          if (key.find("linker_debug_flags") != String::npos) continue;
          if (key.find("configure_script") != String::npos) continue;
          if (key.starts_with("build_extensions_mac_")) continue;
          if (key.starts_with("build_extensions_linux_")) continue;
          if (key.starts_with("build_extensions_win_")) continue;
          if (key.starts_with("build_extensions_android_")) continue;
          if (key.ends_with("_path")) continue;

          String extension;
          if (key.starts_with("build_extensions_ios")) {
            extension = replace(key, "build_extensions_ios_", "");
          } else {
            extension = replace(key, "build_extensions_", "");
          }

          extension = split(extension, '_')[0];
          fs::current_path(targetPath);

          const auto extensionKey = "build_extensions_" + extension;
          const auto scopedExtensionKey = "build_extensions_ios_" + extension;
          const auto suffixScopedExtensionKey = extensionKey + "_ios";
          auto source = settings[extensionKey + "_source"];
          if (source.size() == 0) {
            source = settings[scopedExtensionKey + "_source"];
          }

          if (source.size() == 0) {
            for (const auto& sourceKey : {extensionKey, scopedExtensionKey, suffixScopedExtensionKey}) {
              if (fs::is_directory(settings[sourceKey])) {
                source = settings[sourceKey];
                settings[sourceKey] = "";
                break;
              }
            }
          }

          if (source.size() > 0) {
            Path target;
            if (fs::exists(source)) {
              target = source;
            } else if (source.ends_with(".git")) {
              auto path = Path { source };
              target = paths.platformSpecificOutputPath / "extensions" / replace(path.filename().string(), ".git", "");

              if (!fs::exists(target)) {
                auto exitCode = std::system(("git clone " + source + " " + target.string()).c_str());

                if (exitCode) {
                  exit(exitCode);
                }
              }
            }

            if (fs::exists(target)) {
              Map<> config;
              Path extensionConfigPath;
              auto extensionFormat = UserConfigFormat::Ini;
              const auto extensionToml = target / "oro.toml";
              const auto extensionOroIni = target / "oro.ini";

              if (fileExists(extensionToml)) {
                extensionConfigPath = extensionToml;
                extensionFormat = UserConfigFormat::Toml;
              } else if (fileExists(extensionOroIni)) {
                extensionConfigPath = extensionOroIni;
                extensionFormat = UserConfigFormat::Ini;
              }

              if (!extensionConfigPath.empty()) {
                config = parseUserConfigSource(readFile(extensionConfigPath), extensionFormat);
              }
              settings["build_extensions_" + extension + "_path"] = target.string();

              for (const auto& entry : config) {
                if (entry.first.starts_with("extension_sources")) {
                  settings["build_extensions_" + extension] += entry.second;
                } else if (entry.first.starts_with("extension_")) {
                  auto key = replace(entry.first, "extension_", extension + "_");
                  auto value = entry.second;
                  auto index = "build_extensions_" + key;
                  if (settings[index].size() > 0) {
                    settings[index] += " " + value;
                  } else {
                    settings[index] = value;
                  }
                }
              }
            }
          }

          auto configure = settings["build_extensions_" + extension + "_configure_script"];
          auto build = settings["build_extensions_" + extension + "_build_script"];
          auto copy = settings["build_extensions_" + extension + "_build_copy"];
          auto path = settings["build_extensions_" + extension + "_path"];
          auto target = settings["build_extensions_" + extension + "_target"];

          auto sources = parseStringList(
            trim(
              settings[extensionKey] + " " +
              settings[scopedExtensionKey] + " " +
              settings[suffixScopedExtensionKey]
            ),
            ' '
          );

          auto objects = StringStream();
          auto libdir = prefixFile(String("lib/") + deviceType);

          if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) {
            logInfo("Building extension: " + extension + " (" + deviceType + ")");
            extensions.push_back(extension);
          }

          if (path.size() > 0) {
            fs::current_path(targetPath);
            fs::current_path(path);
          } else {
            fs::current_path(targetPath);
          }

          if (build.size() > 0) {
            auto exitCode = std::system((build + argvForward).c_str());
            if (exitCode) {
              exit(exitCode);
            }
          }

          if (configure.size() > 0) {
            auto output = replace(exec(configure + argvForward).output, "\n", " ");
            if (output.size() > 0) {
              for (const auto& source : parseStringList(output, ' ')) {
                sources.push_back(source);
              }
            }
          }

          if (copy.size() > 0) {
            auto pathResources = paths.platformSpecificOutputPath / "ui";
            for (const auto& file : parseStringList(copy, ' ')) {
              auto parts = split(file, ':');
              auto target = parts[0];
              auto destination = parts.size() == 2 ? pathResources / parts[1] : pathResources;
              fs::copy(
                target,
                destination,
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
            }
          }

          auto CXX = env::get("CXX");
          auto CC = env::get("CC");

        for (auto source : sources) {
          if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
            logDebug("extension source: " + source);
          }

            if (target == "wasm32" || target == "wasm32-wasi") {
              continue;
            }

            String compiler;

            auto compilerFlags = (
              settings["build_extensions_compiler_flags"] +
              settings["build_extensions_ios_compiler_flags"] +
              settings["build_extensions_" + extension + "_compiler_flags"] +
              settings["build_extensions_" + extension + "_ios_compiler_flags"] +
              " -framework UniformTypeIdentifiers" +
              " -framework CoreBluetooth" +
              " -framework QuartzCore" +
              " -framework CoreLocation" +
              " -framework Network" +
              " -framework UserNotifications" +
              " -framework Metal" +
              " -framework Accelerate" +
              " -framework WebKit" +
              " -framework Cocoa" +
              " -framework OSLog"
            );

            auto compilerDebugFlags = (
              settings["build_extensions_compiler_debug_flags"] + " " +
              settings["build_extensions_ios_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_ios_compiler_debug_flags"] + " "
            );

            compilerFlags += " -DORO_RUNTIME_EXTENSION=1";

            // --platform=ios should always build for arm64 even on Darwin x86_64
            if (!flagBuildForSimulator) {
              compilerFlags += " -arch arm64 ";
            }

            if (source.ends_with(".hh") || source.ends_with(".h")) {
              continue;
            } else if (source.ends_with(".wasm")) {
              fs::copy(
                source,
                (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm")),
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
              continue;
            } else if (source.ends_with(".cc") || source.ends_with(".cpp") || source.ends_with(".c++") || source.ends_with(".mm")) {
              compiler = "clang++";
              compilerFlags += " -std=c++2a -ObjC++ -v ";
            } else if (source.ends_with(".o") || source.ends_with(".a")) {
              objects << source << " ";
              continue;
            } else if (source.ends_with(".c")) {
              compiler = "clang";
              compilerFlags += " -ObjC -v ";
            } else {
              continue;
            }

            auto objectFile = source;
            objectFile = replace(objectFile, "\\.mm$", ".o");
            objectFile = replace(objectFile, "\\.m$", ".o");
            objectFile = replace(objectFile, "\\.cc$", ".o");
            objectFile = replace(objectFile, "\\.c$", ".o");

            auto filename = Path(objectFile).filename();
            auto object = (
              paths.pathResourcesRelativeToUserBuild /
              "oro" /
              "extensions" /
              extension /
              filename
            );

            fs::create_directories(object.parent_path());

            objects << object.string() << " ";
          auto compileExtensionObjectCommand = StringStream();
          compileExtensionObjectCommand
              << "xcrun -sdk " << (flagBuildForSimulator ? "iphonesimulator" : "iphoneos")
              << " " << compiler
              << " -I\"" << Path(paths.platformSpecificOutputPath / "include").string() << "\""
              << " -I" << prefixFile()
              << " -I" << prefixFile("include")
              << " -DIOS=1"
              << " -DANDROID=0"
              << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
              << " -DHOST=" << "\\\"" << devHost << "\\\""
              << " -DPORT=" << devPort
              << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
              << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
              << " -target " << (flagBuildForSimulator ? platform.arch + "-apple-ios-simulator": "arm64-apple-ios")
              << " -fembed-bitcode"
              << " -fPIC"
              << " " << trim(compilerFlags + " " + (flagDebugMode ? compilerDebugFlags : ""))
              << " " << (flagBuildForSimulator ? "-mios-simulator-version-min=" : "-miphoneos-version-min=") + env::get("IPHONEOS_VERSION_MIN", "15.0")
              << " -c " << source
              << " -o " << object.string();

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logVerbose(compileExtensionObjectCommand.str());
            }

            do {
              auto r = exec(compileExtensionObjectCommand.str());

              if (r.exitCode != 0) {
                logError("Unable to build extension object (" + object.string() + ")");
                logDebug(r.output);
                exit(r.exitCode);
              }
            } while (0);
          }

          if (target == "wasm32") {
            String compiler;
            auto compilerFlags = (
              settings["build_extensions_compiler_flags"] +
              settings["build_extensions_ios_compiler_flags"] +
              settings["build_extensions_" + extension + "_compiler_flags"] +
              settings["build_extensions_" + extension + "_ios_compiler_flags"]
            );

            auto compilerDebugFlags = (
              settings["build_extensions_compiler_debug_flags"] + " " +
              settings["build_extensions_ios_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_ios_compiler_debug_flags"] + " "
            );

            compilerFlags += " -DORO_RUNTIME_EXTENSION=1";
            compiler = CXX.size() > 0 ? CXX : "clang++";
            compilerFlags += " -v";
            compilerFlags += " -std=c++2a -v";
            if (platform.mac) {
              compilerFlags += " -ObjC++";
            } else if (platform.win) {
              compilerFlags += " -stdlib=libstdc++";
            }

            if (platform.win || platform.linux) {
              compilerFlags += " -Wno-unused-command-line-argument";
            }

            if (compiler.ends_with("clang++")) {
              compiler = compiler.substr(0, compiler.size() - 2);
            } else if (compiler.ends_with("clang++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + ".exe";
            } else if (compiler.ends_with("g++")) {
              compiler = compiler.substr(0, compiler.size() - 2) + "cc";
            } else if (compiler.ends_with("g++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + "cc.exe";
            }
            auto compileExtensionWASMCommand = StringStream();
            auto lib = (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm"));
            fs::create_directories(lib.parent_path());
            compileExtensionWASMCommand
              << compiler
              << " -I\"" + Path(paths.platformSpecificOutputPath / "include").string() << "\""
              << (" -I" + quote + trim(prefixFile("include")) + quote)
              << (" -I" + quote + trim(prefixFile("include/oro/webassembly")) + quote)
              << (" -I" + quote + trim(prefixFile("src")) + quote)
              << (" -L" + quote + trim(prefixFile("lib")) + quote)
              << " -DORO_RUNTIME_EXTENSION_WASM=1"
              << " --target=wasm32"
              << " --no-standard-libraries"
              << " -Wl,--import-memory"
              << " -Wl,--allow-undefined"
              << " -Wl,--export-all"
              << " -Wl,--no-entry"
              << " -Wl,--demangle"
              << (" -L" + quote + trim(prefixFile("lib/" + platform.arch + "-desktop")) + quote)
              << " -fvisibility=hidden"
              << " -DIOS=1"
              << " -DANDROID=0"
              << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
              << " -DHOST=" << "\\\"" << devHost << "\\\""
              << " -DPORT=" << devPort
              << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
              << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
              << " " << trim(compilerFlags + " " + (flagDebugMode ? compilerDebugFlags : ""))
              << " " << join(sources, " ")
              << " -o " << lib.string();

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logVerbose(compileExtensionWASMCommand.str());
            }

            do {
              auto r = exec(compileExtensionWASMCommand.str());

              if (r.exitCode != 0) {
                logError("Unable to build WASM extension (" + extension + ")");
                logDebug(r.output);
                exit(r.exitCode);
              }
            } while (0);
            continue;
          } else if (target == "wasm32-wasi") {
            // TODO
            continue;
          }

          auto linkerFlags = (
            settings["build_extensions_linker_flags"] + " " +
            settings["build_extensions_ios_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_ios_linker_flags"] + " "
          );

          auto linkerDebugFlags = (
            settings["build_extensions_linker_debug_flags"] + " " +
            settings["build_extensions_ios_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_ios_linker_debug_flags"] + " "
          );

          // --platform=ios should always build for arm64 even on Darwin x86_64
          if (!flagBuildForSimulator) {
            linkerFlags += " -arch arm64 ";
          }

          auto lib = (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME));
          fs::create_directories(lib.parent_path());
          auto compileExtensionLibraryCommand = StringStream();
          compileExtensionLibraryCommand
            << "xcrun -sdk " << (flagBuildForSimulator ? "iphonesimulator" : "iphoneos")
            << " clang++"
            << " " << objects.str()
            << " " << prefixFile("src/init.cc")
            << " " << flags
            << " -I" << Path(paths.platformSpecificOutputPath / "include").string()
            << " -I" << prefixFile()
            << " -I" << prefixFile("include")
            << " -L" + libdir
            << " " << runtimeLinkFlag()
            << " -luv"
            << " -lllama"
            << " -lwhisper"
            << " -lggml"
            << " -lggml-cpu"
            << " -lggml-base"
            << " -lsodium"
            << " -isysroot " << iosSdkPath << "/"
            << " -iframeworkwithsysroot /System/Library/Frameworks/"
            << " -F " << iosSdkPath << "/System/Library/Frameworks/"
            << " -framework UniformTypeIdentifiers"
            << " -framework CoreBluetooth"
            << " -framework CoreLocation"
            << " -framework Foundation"
            << " -framework Network"
            << " -framework Security"
            << " -framework UserNotifications"
            << " -framework Metal"
            << " -framework Accelerate"
            << " -framework WebKit"
            << " -framework UIKit"
            << " -fembed-bitcode"
            << " -std=c++2a"
            << " -ObjC++"
            << " -shared"
            << " -v"
            << " -target " << (flagBuildForSimulator ? platform.arch + "-apple-ios-simulator": "arm64-apple-ios")
            << " " << (flagBuildForSimulator ? "-mios-simulator-version-min=" : "-miphoneos-version-min=") + env::get("IPHONEOS_VERSION_MIN", "15.0")
            << " " << trim(linkerFlags + " " + (flagDebugMode ? linkerDebugFlags : ""))
            << " -o " << lib;

          if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
            logVerbose(compileExtensionLibraryCommand.str());
          }

          do {
            auto r = exec(compileExtensionLibraryCommand.str());

            if (r.exitCode != 0) {
              logError("Unable to build extension (" + extension + ")");
              logDebug(r.output);
              exit(r.exitCode);
            }
          } while (0);


          if (!flagDebugMode) {
            for (const auto& object : parseStringList(objects.str(), ' ')) {
              fs::remove_all(object);
            }
          }

          // mostly random build IDs and refs
          auto id = String("17D835592A262D7900") + std::to_string(rand64()).substr(0, 6);
          auto ref = String("17D835592A262D7900") + std::to_string(rand64()).substr(0, 6);

          xCodeProjectVariables["__ios_native_extensions_build_context_sections"] +=
            id + " /* " + lib.filename().string() + " */ = {"                     +
              ("isa = PBXBuildFile; ")                                            +
              ("fileRef = " + ref+ ";")                                           +
            "};\n";

          xCodeProjectVariables["__ios_native_extensions_build_context_refs"] +=
            ref + " /* " + lib.filename().string() + " */ = {"                       +
              ("isa = PBXFileReference; ")                                           +
              ("name = \"" + lib.filename().string() +  "\"; ")                      +
              ("path = \"ui/oro/extensions/" + lib.filename().string() + "\"; ")  +
              ("sourceTree = \"<group>\"; ")                                         +
            "};\n";

          xCodeProjectVariables["__ios_native_extensions_build_ids"] += id + ",\n";
          xCodeProjectVariables["__ios_native_extensions_build_refs"] += ref + ",\n";
        }
      }

      if (settings.contains("ios_info_plist_file")) {
        const auto files = parseStringList(settings["ios_info_plist_file"]);
        for (const auto file : files) {
          settings["ios_info_plist_data"] += readFile(file);
        }
      }

      settings["ios_nonexempt_encryption"] = settings.contains("ios_nonexempt_encryption") ? "true" : "false";

      if (!settings.contains("ios_info_plist_data")) {
        settings["ios_info_plist_data"] = "";
      }

      settings["ios_info_plist_data"] += (
        "  <key>UIBackgroundModes</key>\n"
        "  <array>\n"
        "    <string>fetch</string>\n"
        "    <string>processing</string>\n"
      );

      if (settings["permissions_allow_bluetooth"] != "false") {
        settings["ios_info_plist_data"] += (
          "    <string>bluetooth-central</string>\n"
          "    <string>bluetooth-peripheral</string>\n"
        );
      }

      if (settings["permissions_allow_geolocation"] != "false") {
        settings["ios_info_plist_data"] += (
          "    <string>location</string>\n"
        );
      }

      if (settings["permissions_allow_push_notifications"] == "true") {
        settings["ios_info_plist_data"] += (
          "    <string>remote-notification</string>\n"
        );
      }

      settings["ios_info_plist_data"] += (
        String("  </array>\n") +
        "  <key>BGTaskSchedulerPermittedIdentifiers</key>\n" +
        "  <array>\n" +
        "     <string>" + settings["meta_bundle_identifier"] + "</string>\n" +
        "  </array>\n"
      );

      settings["ios_info_plist_data"] += (
        "  <key>UIRequiredDeviceCapabilities</key>\n"
        "  <array>\n"
      );

      if (settings["permissions_allow_bluetooth"] != "false") {
        settings["ios_info_plist_data"] += (
          "     <string>bluetooth-le</string>\n"
        );
      }

      if (settings["permissions_allow_geolocation"] != "false") {
        settings["ios_info_plist_data"] += (
          "     <string>gps</string>\n"
          "     <string>location-services</string>\n"
        );
      }

      if (settings["permissions_allow_sensors"] != "false") {
        settings["ios_info_plist_data"] += (
          "     <string>accelerometer</string>\n"
          "     <string>gyroscope</string>\n"
        );
      }

      if (settings["permissions_allow_user_media"] != "false") {
        if (settings["permissions_allow_microphone"] != "false") {
          settings["ios_info_plist_data"] += (
            "     <string>microphone</string>\n"
          );
        }

        if (settings["permissions_allow_camera"] != "false") {
          settings["ios_info_plist_data"] += (
            "     <string>video-camera</string>\n"
          );
        }
      }

      settings["ios_info_plist_data"] += (
        "  </array>\n"
      );

      const auto versionParts = split(settings["meta_version"], '.');
      Vector<String> versionComponents = {};
      for (const auto& part : versionParts) {
        const auto value = trim(part);
        if (value.size() == 0) {
          continue;
        }

        if (value[0] >= 0x30 && value[0] <= 0x39) {
          versionComponents.push_back(value);
        }
      }

      if (versionComponents.size() > 0) {
        settings["ios_project_version"] = versionComponents[0];
      } else {
        settings["ios_project_version"] = "0";
      }

      if (settings["ios_deployment_target"].size() == 0) {
        settings["ios_deployment_target"] = "15.0";
      } else {
        const auto parts = split(settings["ios_deployment_target"], '.');

        if (parts.size() == 1) {
          settings["ios_deployment_target"] = parts[0]+ ".";
        } else if (parts.size() > 2) {
          settings["ios_deployment_target"] = parts[0] + "." + parts[1];
        }
      }

      if (settings["ios_category"].empty()) {
        settings["ios_category"] = "public.app-category.developer-tools";
      }

      if (settings["ios_protocol"].empty()) {
        settings["ios_protocol"] = settings["meta_application_protocol"];
      }

      xCodeProjectVariables["ios_project_version"] = settings["ios_project_version"];
      xCodeProjectVariables["ios_deployment_target"] = settings["ios_deployment_target"];
      xCodeProjectVariables["ios_category"] = settings["ios_category"];
      xCodeProjectVariables["ios_protocol"] = settings["ios_protocol"];

      settings["ios_app_transport_security_domain_exceptions"] = "";
      if (settings["webview_insecure_domains"].size() > 0) {
        const auto links = parseStringList(trim(settings["webview_insecure_domains"]), ' ');

        for (const auto link : links) {
          auto domain = split(link, '?')[0];
          settings["ios_app_transport_security_domain_exceptions"] += (
            "      <key>" + domain + "</key>\n"
            "      <dict>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>\n"
            "        <false/>\n"
            "        <key>NSIncludesSubdomains</key>\n"
            "        <true/>\n"
            "        <key>NSTemporaryExceptionMinimumTLSVersion</key>\n"
            "        <string>1.0</string>\n"
            "        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>\n"
            "        <false/>\n"
            "      </dict>\n"
          );
        }
      }

      if (settings["ios_distribution_method"].empty()) {
        settings["ios_distribution_method"] = "";
      }
      writeFile(paths.platformSpecificOutputPath / "exportOptions.plist", tmpl(gXCodeExportOptionsForIOS, settings));
      writeFile(paths.platformSpecificOutputPath / "Info.plist", tmpl(gIOSInfoPList, settings));
      writeFile(pathToProject / "project.pbxproj", tmpl(gXCodeProject, xCodeProjectVariables));
      writeFile(pathToScheme / schemeName, tmpl(gXCodeScheme, settings));

      pathResources = paths.platformSpecificOutputPath / "ui";
      fs::create_directories(pathResources);
    }

    //
    // Linux Package Prep
    // ---
    //
    if (platform.linux && isForDesktop) {
      logInfo("preparing build for linux");
      flags = " -std=c++2a `pkg-config --cflags --libs dbus-1 gtk+-3.0 webkit2gtk-4.1`";
      flags += " -ldl " + getCxxFlags();
      flags += " -I" + Path(paths.platformSpecificOutputPath / "include").string();
      flags += " -I" + prefixFile();
      flags += " -I" + prefixFile("include");
      flags += " -I" + prefixFile("include");
      flags += " -L" + prefixFile("lib/" + platform.arch + "-desktop");

      appendTLSFlags(flags);

      files += prefixFile("objects/" + platform.arch + "-desktop/desktop/main.o");
      files += prefixFile("src/init.cc");

      auto resolveIrohLib = [&](const String& baseDir) -> Path {
        return prefixPath(baseDir + "/liboro_iroh.a");
      };

      String libDir = "lib/" + platform.arch + "-desktop";
      Vector<Path> linuxCoreLibs = {
        resolveRuntimeStaticArchive(libDir),
        resolveIrohLib(libDir),
        prefixPath(libDir + "/libuv.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libusb-1.0.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libsodium.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libllama.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libwhisper.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libggml.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libggml-base.a"),
        prefixPath("lib/" + platform.arch + "-desktop/libggml-cpu.a")
      };
#if ORO_RUNTIME_HAVE_LIBIPFS
      linuxCoreLibs.push_back(
        prefixPath("lib/" + platform.arch + "-desktop/libipfs.a")
      );
#endif

      Vector<Path> linuxBundledLibs;
      auto appendBundledLib = [&](const String& name) {
        const auto path = prefixPath("lib/" + platform.arch + "-desktop/" + name);
        if (!fs::exists(path)) {
          return;
        }
        if (std::find(linuxBundledLibs.begin(), linuxBundledLibs.end(), path) == linuxBundledLibs.end()) {
          linuxBundledLibs.push_back(path);
        }
      };

      appendBundledLib("libmbedtls.a");
      appendBundledLib("libmbedtls.so");
      appendBundledLib("libmbedx509.a");
      appendBundledLib("libmbedx509.so");
      appendBundledLib("libmbedcrypto.a");
      appendBundledLib("libmbedcrypto.so");
      appendBundledLib("libiroh_c_ffi.a");
      appendBundledLib("libiroh_c_ffi.so");
      appendBundledLib("libiroh_ffi.a");
      appendBundledLib("libiroh_ffi.so");
      appendBundledLib("libiroh.a");
      appendBundledLib("libiroh.so");

      files += String(" -Wl,--start-group ");
      for (const auto& lib : linuxCoreLibs) {
        files += lib.string();
        files += " ";
      }
      for (const auto& lib : linuxBundledLibs) {
        files += lib.string();
        files += " ";
      }
      files += String("-Wl,--end-group ");

      pathResources = paths.pathBin;

      // @TODO(jwerle): support other Linux based OS
      Path pathControlFile = {
        paths.pathPackage /
        "DEBIAN"
      };

      Path pathManifestFile = {
        paths.pathPackage /
        "usr" /
        "share" /
        "applications"
      };

      Path pathIcons = {
        paths.pathPackage /
        "usr" /
        "share" /
        "icons" /
        "hicolor" /
        "256x256" /
        "apps"
      };

      {
        bool skipDesktopExtension = (
          trim(env::get("ORO_SKIP_DESKTOP_EXTENSION")) == "1" ||
          equal(settings["linux_skip_desktop_extension"], "true")
        );

    if (skipDesktopExtension) {
      logInfo("skipping desktop runtime extension build");
        } else {
          auto desktopExtensionsPath = pathResources / "lib" / "extensions";
          auto CXX = env::get("CXX", "clang++");
          const auto runtimeExtensionName = String("lib") + String(gRuntimeLibraryNames.front()) + "-desktop-extension.so";
          const auto runtimeExtensionOutput = desktopExtensionsPath / runtimeExtensionName;
          const auto runtimeArchive = resolveRuntimeStaticArchive(libDir);

          fs::create_directories(desktopExtensionsPath);

          StringStream command;
          command
            << CXX
            << " -shared"
            << " -Wl,--exclude-libs,ALL"
            << " -rdynamic"
            << " -fPIC"
            << " " << flags
            << " -o " << runtimeExtensionOutput.string()
            << " " << prefixFile("objects/" + platform.arch + "-desktop/extensions/linux.o")
            << " " << runtimeArchive.string()
            << " " << resolveIrohLib(libDir).string()
            << " " << prefixFile(libDir + "/libuv.a")
            << " " << prefixFile(libDir + "/libusb-1.0.a")
            << " " << prefixFile(libDir + "/libsodium.a")
            << " " << prefixFile(libDir + "/libwhisper.a")
            << " " << prefixFile(libDir + "/libllama.a")
            << " " << prefixFile(libDir + "/libggml.a")
            << " " << prefixFile(libDir + "/libggml-base.a")
            << " " << prefixFile(libDir + "/libggml-cpu.a");

          for (const auto& lib : linuxBundledLibs) {
            command << " " << lib.string();
          }

          logVerbose(command.str());
          auto result = exec(command.str().c_str());
          if (result.exitCode != 0) {
            logCapturedCommandOutput(result.output);
            logError("failed to compile desktop runtime extension." + compilerOutputHint());
            exit(1);
          }

          for (size_t i = 1; i < gRuntimeLibraryNames.size(); ++i) {
            const auto aliasName = String("lib") + String(gRuntimeLibraryNames[i]) + "-desktop-extension.so";
            const auto aliasPath = desktopExtensionsPath / aliasName;
            try {
              fs::copy_file(runtimeExtensionOutput, aliasPath, fs::copy_options::overwrite_existing);
            } catch (const fs::filesystem_error& ex) {
              logWarn("failed to prepare desktop extension alias '" + aliasPath.string() + "': " + ex.what());
            }
          }
        }
      }

      fs::create_directories(pathIcons);
      fs::create_directories(pathResources);
      fs::create_directories(pathManifestFile);
      fs::create_directories(pathControlFile);

      auto linuxExecPath =
        Path("/opt") /
        settings["build_name"] /
        settings["build_name"];

      // internal settings
      settings["linux_executable_path"] = linuxExecPath.string();
      settings["linux_icon_path"] = (
        Path("/usr") /
        "share" /
        "icons" /
        "hicolor" /
        "256x256" /
        "apps" /
        (settings["build_name"] + ".png")
      ).string();

      if (settings["application_agent"] == "true") {
        settings["linux_desktop_startup_notify"] = "false";
      } else {
        settings["linux_desktop_startup_notify"] = "true";
      }

      writeFile(pathManifestFile / (settings["build_name"] + ".desktop"), tmpl(gDesktopManifest, settings));
      writeFile(pathControlFile / "control", tmpl(gDebianManifest, settings));

      auto pathToIconSrc = (targetPath / settings["linux_icon"]).string();
      auto pathToIconDest = (pathIcons / (settings["build_name"] + ".png")).string();

      if (!fs::exists(pathToIconDest)) {
        if (fs::exists(pathToIconSrc)) {
          fs::copy(pathToIconSrc, pathToIconDest);
        } else {
        logWarn("[linux] icon '" + pathToIconSrc +  "' does not exist");
        }
      }
    }

    //
    // Windows Package Prep
    // ---
    //
    if (platform.win && isForDesktop) {
      logInfo("preparing build for win");
      auto prefix = prefixFile();

      flags = " -std=c++2a"
        " -D_MT"
        " -D_DLL"
        " -DWIN32"
        " -DWIN32_LEAN_AND_MEAN"
        " -Wl,-NODEFAULTLIB:libcmt"
        " -Wno-nonportable-include-path"
        " -I\"" + Path(paths.platformSpecificOutputPath / "include").string() + "\""
        " -I\"" + prefix + "include\""
        " -I\"" + prefix + "src\""
        " -L\"" + prefix + "lib\"";

      // See install.sh for more info on windows debug builds and d suffix
      auto missing_assets = false;
      auto debugBuild = debugEnv;
      if (debugBuild) {
        for (String libString : split(env::get("WIN_DEBUG_LIBS"), ',')) {
          if (libString.size() > 0) {
            if (libString[0] == '\"' && libString[libString.size()-2] == '\"')
              libString = libString.substr(1, libString.size()-2);

            Path lib(libString);
            if (!fs::exists(lib)) {
              logWarn("WIN_DEBUG_LIBS: File doesn't exist, aborting build: " + lib.string());
              missing_assets = true;
            } else {
              flags += " " + lib.string();
            }
          }
        }
      }

      if (debugBuild) {
        flags += " -D_DEBUG -g";
      }

      auto d = String(debugBuild ? "d" : "" );

      flags += " -I" + prefixFile("include");
      flags += " -L" + prefixFile("lib" + d + "/" + platform.arch + "-desktop");
      appendTLSFlags(flags);
      const String cliName(gCliDisplayName);
      auto main_o = prefixFile("objects/" + platform.arch + "-desktop/desktop/main" + d + ".o");
      if (!fs::exists(main_o)) {
        logWarn("Can't find main obj, unable to build: " + main_o + ". Ensure dev runtime objects are installed under ORO_HOME. Run '" + cliName + " setup --platform=windows'.");
        missing_assets = true;
      } else {
        files += main_o;
      }
      files += prefixFile("src/init.cc");
      auto static_runtime_path = resolveRuntimeStaticArchive("lib" + d + "/" + platform.arch + "-desktop", d + ".a");
      if (!fs::exists(static_runtime_path)) {
        logError("Can't find static runtime, unable to build: " + static_runtime_path.string() + ". Ensure dev runtime libs are installed under ORO_HOME. Run '" + cliName + " setup --platform=windows'.");
        missing_assets = true;
      } else {
        files += static_runtime_path.string() + " ";
      }

#if ORO_RUNTIME_HAVE_LIBIPFS
      auto static_libipfs_lib = prefixFile("lib" + d + "/" + platform.arch + "-desktop/libipfs" + d + ".lib");
      auto static_libipfs_a = prefixFile("lib" + d + "/" + platform.arch + "-desktop/libipfs" + d + ".a");
      if (fs::exists(static_libipfs_lib)) {
        files += static_libipfs_lib;
      } else if (fs::exists(static_libipfs_a)) {
        files += static_libipfs_a;
      } else {
        logWarn("Can't find libipfs static lib, IPFS features unavailable: " + static_libipfs_lib);
      }
#endif

      auto static_libusb = prefixFile("lib" + d + "/" + platform.arch + "-desktop/libusb-1.0.lib");
      if (fs::exists(static_libusb)) {
        files += static_libusb;
      } else {
        logWarn("Can't find libusb static lib, USB features unavailable: " + static_libusb);
      }

      auto static_libsodium = prefixFile("lib" + d + "/" + platform.arch + "-desktop/libsodium.lib");
      if (fs::exists(static_libsodium)) {
        files += static_libsodium;
      } else {
        logWarn("Can't find libsodium static lib, crypto features unavailable: " + static_libsodium);
      }

      if (missing_assets) {
        exit(1);
      }

      fs::create_directories(paths.pathPackage);

      pathResources = paths.pathResourcesRelativeToUserBuild;

      auto p = Path {
        paths.pathResourcesRelativeToUserBuild /
        "AppxManifest.xml"
      };

      if (settings["win_icon"].size() > 0) {
        auto winIconPath = fs::path(settings["win_icon"]);
        if (!fs::exists(winIconPath)) {
          logWarn("Windows icon path at '[win] icon' does not exist");
        } else {
          const auto extname = winIconPath.extension().string();
          fs::copy(
            winIconPath,
            pathResources,
            fs::copy_options::update_existing | fs::copy_options::recursive
          );
        }
      }

      if (settings["win_logo"].size() == 0 && settings["win_icon"].size() > 0) {
        settings["win_logo"] = fs::path(settings["win_icon"]).filename().string();
      }

      // internal, used in the manifest
      if (settings["meta_version"].size() > 0) {
        auto version = settings["meta_version"];
        auto winversion = split(version, '-')[0];

        settings["win_version"] = winversion + ".0";
      } else {
        settings["win_version"] = "0.0.0.0";
      }

      // internal, a path to win executable, used in the manifest
      settings["win_exe"] = executable.string();

      writeFile(p, tmpl(gWindowsAppManifest, settings));

      // TODO Copy the files into place
    }

    if (settings["tray_icon"].size() > 0) {
      auto trayIconPath = fs::path(settings["tray_icon"]);
      if (!fs::exists(trayIconPath)) {
        logWarn("Tray icon path at '[tray] icon' does not exist");
      } else {
        const auto extname = trayIconPath.extension().string();
        fs::copy(
          trayIconPath,
          pathResources / (String("application_tray_icon") + extname),
          fs::copy_options::update_existing | fs::copy_options::recursive
        );
      }
    }

    handleBuildPhaseForUser(
      settings,
      targetPlatform,
      pathResourcesRelativeToUserBuild,
      oldCwd,
      true
    );

    auto copyMapFiles = handleBuildPhaseForCopyMappedFiles(
      settings,
      targetPlatform,
      pathResourcesRelativeToUserBuild
    );

    logInfo("package prepared");

    auto runtimeHomeApi = env::get("ORO_HOME_API");
    if (runtimeHomeApi.size() == 0) {
      runtimeHomeApi = prefixPath("api").string();
    }

    if (fs::exists(fs::status(runtimeHomeApi))) {
      fs::create_directories(pathResources);
      fs::copy(
        runtimeHomeApi,
        pathResources / "oro",
        fs::copy_options::update_existing | fs::copy_options::recursive
      );

      // XXX(@jwerle): 'node_modules/' sometimes can be found in the
      // runtimeHomeApi directory if distributed with npm
      fs::remove_all(pathResources / "oro" / "node_modules");
    }

      if (flagBuildForIOS) {
        if (flagBuildForSimulator) {
          if (settings.count("ios_simulator_uuid") < 0) {
            checkIosSimulatorDeviceAvailability(settings["ios_simulator_device"]);
          }
          logInfo("building for iOS Simulator");
        }  else {
          logInfo("building for iOS");
        }

      auto pathToDist = oldCwd / paths.platformSpecificOutputPath;

      fs::create_directories(pathToDist);
      fs::create_directories(pathToDist / "core");
      fs::current_path(pathToDist);

      //
      // Copy and or create the source files we need for the build.
      //
      fs::copy(
        prefixPath("src/init.cc"),
        pathToDist,
        fs::copy_options::overwrite_existing
      );

      auto pathBase = pathToDist / "Base.lproj";
      fs::create_directories(pathBase);

      writeFile(pathBase / "LaunchScreen.storyboard", gStoryboardLaunchScreen);

      Map<> entitlementSettings;
      extendMap(entitlementSettings, settings);

      entitlementSettings["configured_entitlements"] = "";

      if (settings["permissions_allow_unvalidated_native_libraries"] == "true") {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.cs.disable-library-validation</key>\n"
          "  <true/>\n"
        );
      }

      if (settings["permissions_allow_push_notifications"] == "true") {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.developer.usernotifications.filtering</key>\n"
          "  <true/>\n"
        );

        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.developer.location.push</key>\n"
          "  <true/>\n"
        );
      }

      if (settings["meta_application_links"].size() > 0) {
        const auto links = parseStringList(trim(settings["meta_application_links"]), ' ');
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.developer.associated-domains</key>\n"
          "  <array>\n"
        );

        for (const auto link : links) {
          entitlementSettings["configured_entitlements"] += (
            "    <string>applinks:" + link + "</string>\n"
          );
        }

        entitlementSettings["configured_entitlements"] += (
          "  </array>\n"
        );
      }

      if (flagDebugMode && settings["ios_distribution_method"] == "debugging") {
        entitlementSettings["configured_entitlements"] += ("  <key>get-task-allow</key>\n" "  <true/>\n ");
      }

      writeFile(
        pathToDist / "oro.entitlements",
        tmpl(gXcodeEntitlements, entitlementSettings)
      );

      //
      // For iOS we're going to bail early and let XCode infrastructure handle
      // building, signing, bundling, archiving, noterizing, and uploading.
      //
      StringStream archiveCommand;
      String deviceIdentity = settings.count("ios_simulator_uuid") > 0
        ? "id=" + settings["ios_simulator_uuid"]
        : "name=" + settings["ios_simulator_device"];

      String destination = flagBuildForSimulator
        ? "platform=iOS Simulator,OS=latest," + deviceIdentity
        : "generic/platform=iOS";

      String deviceType;

      // TODO: should be "iPhone Distribution: <name/provisioning specifier>"?
      if (settings["ios_codesign_identity"].size() == 0) {
        settings["ios_codesign_identity"] = "iPhone Distribution";
      }

      const auto configuration = String(flagDebugMode ? "Debug" : "Release");
      const auto args = flagShouldPackage
        ? String("archive")
        : "CONFIGURATION_BUILD_DIR=" + pathToDist.string();

      archiveCommand
        << "xcodebuild"
        << " build " << args
        << " -allowProvisioningUpdates"
        << " -scheme " << settings["build_name"]
        << " -destination '" << destination << "'"
        << " -configuration " << configuration;

      if (flagShouldPackage) {
        archiveCommand << " -archivePath build/" << settings["build_name"];
      }

      if (flagBuildForSimulator) {
        archiveCommand << " ARCHS=" << platform.arch << " ONLY_ACTIVE_ARCH=NO";
      }

      if (!flagCodeSign) {
        archiveCommand
          << " CODE_SIGN_IDENTITY=\"\""
          << " CODE_SIGNING_REQUIRED=\"NO\""
          << " CODE_SIGN_ENTITLEMENTS=\"\""
          << " CODE_SIGNING_ALLOWED=\"NO\"";
      } else {
        archiveCommand
          << " CODE_SIGN_IDENTITY=\"" + settings["ios_codesign_identity"] + "\""
          << " CODE_SIGN_ENTITLEMENTS=\"" + (pathToDist.string() + "/oro.entitlements") +  "\"";
      }

      // log(archiveCommand.str().c_str());
      auto rArchive = exec(archiveCommand.str().c_str());

      if (rArchive.exitCode != 0) {
        logError("failed to archive project. Check signing identities/profiles in your config (oro.toml or oro.ini). See README (Troubleshooting)");
        if (rArchive.output.size() > 0) {
          logDebug(rArchive.output);
        }
        fs::current_path(oldCwd);
        exit(1);
      }

      if (flagCodeSign) {
        StringStream stream;
        stream
          << "codesign"
          << " -vvv"
          << " --force"
          << " --sign '" << settings["ios_codesign_identity"] << "'"
          << " --entitlements '" << pathToDist.string() << "/oro.entitlements'"
          << " --generate-entitlement-der"
          << " --preserve-metadata=identifier,flags,runtime";


        if (flagShouldPackage) {
          stream
            << " " << pathToDist.string()
            << "/build/"
            << settings["build_name"] << ".xcarchive"
            << "/Products/Applications/" << settings["build_name"] << ".app";
        } else {
          stream << " " << pathToDist.string() << "/" << settings["build_name"] << ".app";
        }

        const auto command = stream.str();
        // log(command);
        auto result = exec(command.c_str());
        if (result.exitCode != 0) {
          logError("failed to export project. Check signing/export options and try again. See README (Troubleshooting)");
          if (flagVerboseMode && result.output.size() > 0) {
            logDebug(result.output);
          }
          exit(1);
        }
      }

      logInfo("created archive");

      if (flagShouldPackage && !flagBuildForSimulator) {
        StringStream exportCommand;

        exportCommand
          << "xcodebuild"
          << " -exportArchive"
          << " -allowProvisioningUpdates"
          << " -archivePath build/" << settings["build_name"] << ".xcarchive"
          << " -exportPath build/" << settings["build_name"] << ".ipa"
          << " -exportOptionsPlist " << (pathToDist / "exportOptions.plist").string();

        // log(exportCommand.str());
        auto rExport = exec(exportCommand.str().c_str());

        if (rExport.exitCode != 0) {
          logError("failed to export project. Check signing/export options and try again. See README (Troubleshooting)");
          if (flagVerboseMode && rExport.output.size() > 0) {
            logDebug(rExport.output);
          }
          fs::current_path(oldCwd);
          exit(1);
        }

        logInfo("exported archive");
      }

      logInfo("completed");
      fs::current_path(oldCwd);
    }

    if (flagBuildForAndroid) {
      auto cwd = fs::current_path();
      auto app = paths.platformSpecificOutputPath / "app";
      auto androidHome = getAndroidHome();

      fs::current_path(paths.platformSpecificOutputPath);
      StringStream sdkmanager;
      StringStream packages;
      StringStream gradlew;
      String ndkVersion = "29.0.14206865";
      String androidNativePlatform = "android-26";
      String androidSdkPlatform = "android-37.0";

      if (platform.unix) {
        gradlew
          << "ANDROID_HOME=" << androidHome << " ";
      }

      sdkmanager << settings["android_sdk_manager_path"];

      packages
        << " "
        << quote << "ndk;" << ndkVersion << quote << " "
        << quote << "platform-tools" << quote << " "
        << quote << "platforms;" << androidSdkPlatform << quote << " "
        << quote << "build-tools;36.0.0" << quote << " ";

      if (flagBuildForAndroidEmulator) {
        packages
          << quote << "emulator" << quote << " "
          << quote << "system-images;" << androidSdkPlatform << ";google_apis;"
          << replace(platform.arch, "arm64", "arm64-v8a") << quote << " ";
      }

      sdkmanager
        << packages.str();

      if (
        env::get("ORO_SKIP_ANDROID_SDK_MANAGER").size() == 0
      ) {
        if (debugEnv || verboseEnv) {
          logVerbose(sdkmanager.str());
        }

        if (std::system(sdkmanager.str().c_str()) != 0) {
          logWarn("failed to initialize Android SDK (sdkmanager)");
        }
      }

      if (!androidEnableStandardNdkBuild) {
        StringStream ndkBuild;
        StringStream ndkBuildArgs;
        StringStream ndkTest;

        ndkBuild << "ndk-build" << (platform.win ? ".cmd" : "");
        ndkTest << ndkBuild.str() << " --version >" << (!platform.win ? "/dev/null" : "NUL") << " 2>&1";

        if (debugEnv || verboseEnv) logVerbose(ndkTest.str());
        if (std::system(ndkTest.str().c_str()) != 0) {
          ndkBuild.str("");
          ndkBuild << androidHome << slash << "ndk" << slash <<  ndkVersion << slash << "ndk-build" << (platform.win ? ".cmd" : "");

          ndkTest.str("");
          ndkTest
            << ndkBuild.str() << " --version >" << (!platform.win ? "/dev/null" : "NUL") << " 2>&1";

          if (debugEnv || verboseEnv) logVerbose(ndkTest.str());
          if (std::system(ndkTest.str().c_str()) != 0) {
            StringStream ndkError;
            ndkError
              << "ndk not in path or ANDROID_HOME at "
              << ndkBuild.str();
            logError(ndkError.str());
            exit(1);
          }
        }

        // TODO(mribbons): Cache binaries, hash based on source contents. Copy if cache matches rather than building.
        // TODO(mribbons): Expand cache system to other target platforms

        auto output = paths.platformSpecificOutputPath;
        auto src = app / "src";
        auto _main = src / "main";
        auto app_mk = _main / "jni" / "Application.mk";
        auto jni = _main / "jni";
        auto jniLibs = _main / "jniLibs";
        auto libs = _main / "libs";
        auto obj = _main / "obj";

        if (fs::exists(obj)) {
          fs::remove_all(obj);
        }

        // TODO(mribbons) - Copy specific abis
        fs::create_directories(libs);
        int androidStaticLibCount = 0;
        for (auto const& dir_entry : fs::directory_iterator(prefixFile() + "lib")) {
          if (dir_entry.is_directory() && dir_entry.path().stem().string().find("-android") != String::npos) {
            auto dest = libs / replace(dir_entry.path().stem().string(), "-android", "");
            try {
              if (debugEnv) logDebug("copy android lib: "+ dir_entry.path().string() + " => " + dest.string());
              fs::copy(
                dir_entry.path(),
                dest,
                fs::copy_options::overwrite_existing | fs::copy_options::recursive
              );
              androidStaticLibCount++;
            } catch (fs::filesystem_error &e) {
              logError(String("Unable to copy android lib: ") + (fs::exists(dest) ? "exists" : "missing") + ": " + e.what());
              throw;
            }
          }
        }

        if (androidStaticLibCount == 0) {
          const String cliName(gCliDisplayName);
          logError("No android static libs copied, app won't build. Run '" + cliName + " setup --platform=android' and check " + prefixFile() + "lib. See README (Troubleshooting)");
          exit(1);
        }

        auto buildJniLibs = true;
        if (fs::exists(jniLibs)) {
          int androidSharedLibCount = 0;
          for (auto const& dir_entry : fs::recursive_directory_iterator(jniLibs)) {
            if (isRuntimeSharedLibrary(dir_entry.path())) {
              // Count the number of runtime .so files in jniLibs, there will be one for each android architecture
              androidSharedLibCount++;
            }
          }

          // if we have found runtime .so's, we don't need to recompile, unless:
          // the runtime shared library count doesn't match the static lib count - There should be one libuv.a and runtime archive per android architecture
          if (androidSharedLibCount > 0 && androidSharedLibCount != (androidStaticLibCount / 2)) {
            buildJniLibs = true;
            logWarn("Android Shared Lib Count is incorrect, forcing rebuild.");
            fs::remove_all(jniLibs);
          } else {
            buildJniLibs = false;
          }
        }

        fs::create_directories(jniLibs);

        // don't build unless we're sure it is required, ndkbuild errors out if jniLibs is already populated
        if (buildJniLibs) {
          ndkBuildArgs
            << ndkBuild.str()
            << " -j"
            << " NDK_PROJECT_PATH=" << _main
            << " NDK_APPLICATION_MK=" << app_mk
            << (flagDebugMode ? " NDK_DEBUG=1" : "")
            << " APP_PLATFORM=" << androidNativePlatform
            << " NDK_LIBS_OUT=" << jniLibs;

          if (!(debugEnv || verboseEnv)) ndkBuildArgs << " >" << (!platform.win ? "/dev/null" : "NUL") << " 2>&1";

          if (debugEnv || verboseEnv) logVerbose(ndkBuildArgs.str());
          if (!ensureExecAllowed(settings, "build")) exit(1);
          if (std::system(ndkBuildArgs.str().c_str()) != 0) {
            logDebug(ndkBuildArgs.str());
            logError("ndk build failed. Ensure NDK is installed and allowed (use --allow-exec or ORO_ALLOW_EXEC=1). See README (Troubleshooting)");
            exit(1);
          }
        }
      }

      // just build for CI
      if (env::get("ORO_CI").size() > 0) {
        StringStream gradlew;
        gradlew << localDirPrefix << "gradlew build";

        if (!ensureExecAllowed(settings, "build")) exit(1);
      if (runSystemOrFail(
            gradlew.str(),
            String("ERROR: failed to invoke `gradlew build` command. Ensure Gradle and JDK are installed; accept Android SDK licenses.") + gradleJdkHint() + String(" See README (Troubleshooting)"),
            "build"
          ) != 0) {
        exit(1);
      }

        exit(0);
      }

      // Check for gradle in pwd. Don't fail, this is just for support.
      if (!fs::exists(String("gradlew") + (platform.win ? ".bat" : ""))) {
        logWarn("gradlew script not in pwd: " + fs::current_path().string());
      }

      String bundle = flagDebugMode ?
        localDirPrefix + "gradlew :app:bundleDebug --warning-mode all" :
        localDirPrefix + "gradlew :app:bundle";

      if (debugEnv || verboseEnv) logVerbose(bundle);
      if (!ensureExecAllowed(settings, "build")) exit(1);
      if (runSystemOrFail(
            bundle,
            String("ERROR: failed to invoke ") + bundle + String(" command. Ensure Gradle and JDK are installed.") + gradleJdkHint() + String(" See README (Troubleshooting)"),
            "build"
          ) != 0) {
        exit(1);
      }

      // clear stream
      gradlew.str("");
      gradlew
        << localDirPrefix
        << "gradlew assemble";

      if (debugEnv || verboseEnv) logVerbose(gradlew.str());
      if (std::system(gradlew.str().c_str()) != 0) {
        logError("failed to invoke `gradlew assemble` command. Ensure Gradle and JDK are installed. See README (Troubleshooting)");
        exit(1);
      }

      androidState.androidHome = androidHome;
      androidState.verbose = debugEnv || verboseEnv;
      androidState.devNull = devNull;
      androidState.platform = androidSdkPlatform;
      androidState.appPath = app;
      androidState.quote = quote;
      androidState.slash = slash;
      fs::current_path(cwd);
    }

    auto extraFlags = flagDebugMode
      ? settings.count("debug_flags") ? settings["debug_flags"] : ""
      : settings.count("build_flags") ? settings["build_flags"] : "";

    quote = "";
    if (platform.win && env::get("CXX").find(" ") != String::npos) {
      quote = "\"";
    }

    // build desktop extension
    if (isForDesktop) {
      static const auto IN_GITHUB_ACTIONS_CI = env::get("GITHUB_ACTIONS_CI").size() > 0;
      auto oldCwd = fs::current_path();
      fs::current_path(targetPath);

      StringStream compileCommand;

      fs::create_directories(paths.pathResourcesRelativeToUserBuild / "oro" / "extensions");

      auto extensions = Vector<String>();
      for (const auto& tuple : settings) {
        auto& key = tuple.first;
        if (tuple.second.size() == 0) continue;
        if (key.starts_with("build_extensions_")) {
          if (key.find("compiler_flags") != String::npos) continue;
          if (key.find("compiler_debug_flags") != String::npos) continue;
          if (key.find("linker_flags") != String::npos) continue;
          if (key.find("linker_debug_flags") != String::npos) continue;
          if (key.find("configure_script") != String::npos) continue;
          if (key.find("build_script") != String::npos) continue;
          if (key.starts_with("build_extensions_ios_")) continue;
          if (key.starts_with("build_extensions_android_")) continue;
          if (key.ends_with("_path")) continue;

          if (!platform.win && key.starts_with("build_extensions_win_")) continue;
          if (!platform.mac && key.starts_with("build_extensions_mac_")) continue;
          if (!platform.linux && key.starts_with("build_extensions_linux_")) continue;

          String extension;
          auto os = replace(replace(platform.os, "win32", "win"), "macos", "mac");;
          if (key.starts_with("build_extensions_" + os)) {
            extension = replace(key, "build_extensions_" + os + "_", "");
          } else {
            extension = replace(key, "build_extensions_", "");
          }

          extension = split(extension, '_')[0];
          fs::current_path(targetPath);

          const auto extensionKey = "build_extensions_" + extension;
          const auto scopedExtensionKey = "build_extensions_" + os + "_" + extension;
          const auto suffixScopedExtensionKey = extensionKey + "_" + os;
          auto source = settings[extensionKey + "_source"];
          if (source.size() == 0) {
            source = settings[scopedExtensionKey + "_source"];
          }

          if (source.size() == 0) {
            for (const auto& sourceKey : {extensionKey, scopedExtensionKey, suffixScopedExtensionKey}) {
              source = settings[sourceKey];
              if (source.size() > 0) {
                if (fs::is_directory(source)) {
                  settings[extensionKey + "_path"] = (targetPath / source).string();
                  settings[sourceKey] = "";
                }
                break;
              }
            }
          }

          if (source.size() > 0) {
            Path target;
            if (fs::exists(source)) {
              target = targetPath / source;
            } else if (source.ends_with(".git")) {
              auto path = Path { source };
              target = paths.platformSpecificOutputPath / "extensions" / replace(path.filename().string(), ".git", "");

              if (!fs::exists(target)) {
                auto exitCode = std::system(("git clone " + source + " " + target.string()).c_str());

                if (exitCode) {
                  exit(exitCode);
                }
              }
            }

            if (fs::exists(target) && fs::is_directory(target)) {
              target = fs::canonical(target);

              Map<> config;
              Path extensionConfigPath;
              auto extensionFormat = UserConfigFormat::Ini;
              const auto extensionToml = target / "oro.toml";
              const auto extensionOroIni = target / "oro.ini";

              if (fileExists(extensionToml)) {
                extensionConfigPath = extensionToml;
                extensionFormat = UserConfigFormat::Toml;
              } else if (fileExists(extensionOroIni)) {
                extensionConfigPath = extensionOroIni;
                extensionFormat = UserConfigFormat::Ini;
              }

              if (!extensionConfigPath.empty()) {
                config = parseUserConfigSource(readFile(extensionConfigPath), extensionFormat);
              }

              settings["build_extensions_" + extension + "_path"] = target.string();
              fs::current_path(target);

              for (const auto& entry : config) {
                if (entry.first.starts_with("extension_sources")) {
                  const auto sources = parseStringList(entry.second, ' ');
                  Vector<String> canonical;
                  for (const auto& source : sources) {
                    try {
                      canonical.push_back(fs::canonical(target / source).string());
                    } catch (const std::filesystem::filesystem_error& e) {
                      canonical.push_back((target / source).string());
                    }
                  }

                  settings["build_extensions_" + extension] = join(canonical, " ");
                } else if (entry.first.starts_with("extension_")) {
                  auto key = replace(entry.first, "extension_", extension + "_");
                  auto value = entry.second;
                  if (key.ends_with("_flags")) {
                    // Replace all $(…) with evaluated stdout.
                    // E.g. $(pkg-config --libs --cflags libssl) => -lssl
                    auto match = std::smatch{};
                    while (std::regex_search(value, match, std::regex("\\$\\((.*?)\\)"))) {
                      auto subcommand = match[1].str();
                      if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
                        logDebug("Running subcommand: " + subcommand);
                      }
                      auto proc = exec(subcommand);
                      if (proc.exitCode != 0) {
                        logError("failed to run subcommand: " + subcommand + ". Verify script path and allow-exec (use --allow-exec)." + helpHint("build"));
                        exit(proc.exitCode);
                      }
                      auto output = trim(replace(proc.output, "\n", " "));
                      value = value.replace(match[0].first, match[0].second, output);
                    }
                    // Replace all $\w+ with env var.
                    // E.g. $CXX => clang++
                    match = std::smatch{};
                    while (std::regex_search(value, match, std::regex("\\$(\\w+)"))) {
                      auto envVar = match[1].str();
                      auto envValue = envVar == "PWD"
                        ? target.string() // Use the extension root.
                        : env::get(envVar);
                      if (envValue.size() == 0) {
                        logError("failed to find env var: " + envVar + ". Set the variable in your environment or in oro.toml/oro.ini under [env].");
                        exit(1);
                      }
                      value = value.replace(match[0].first, match[0].second, envValue);
                    }
                    // Replace all ./ and ../ with absolute paths.
                    match = std::smatch{};
                    while (std::regex_search(value, match, std::regex("\\.\\.?/([^ ]|\\\\ )+"))) {
                      auto relativePath = match[0].str();
                      auto absolutePath = fs::absolute(target / relativePath);
                      try {
                        absolutePath = fs::canonical(absolutePath);
                        value = value.replace(
                          match[0].first,
                          match[0].second,
                          absolutePath.string()
                        );
                      } catch (const std::filesystem::filesystem_error& e) {
                        if (e.code() == std::errc::no_such_file_or_directory) {
                          logWarn("path not found: " + absolutePath.string() + ". Verify extension source paths and working directory.");
                          break;
                        } else {
                          throw e;
                        }
                      }
                    }
                  }
                  auto index = "build_extensions_" + key;
                  if (settings[index].size() > 0) {
                    settings[index] += " " + value;
                  } else {
                    settings[index] = value;
                  }
                }
              }

              if (settings["build_extensions_" + extension].size() == 0) {
                auto diagnosticsPath = extensionConfigPath.empty()
                  ? target
                  : extensionConfigPath;
                try {
          diagnosticsPath = fs::canonical(diagnosticsPath);
        } catch (const std::filesystem::filesystem_error&) {
          // Fall back to the non-canonical path if it does not exist yet.
        }

        logWarn(
          key + " has no sources, ignoring: " + diagnosticsPath.string()
        );
              }
            }
          }

          auto configure = settings["build_extensions_" + extension + "_configure_script"];
          auto build = settings["build_extensions_" + extension + "_build_script"];
          auto copy = settings["build_extensions_" + extension + "_build_copy"];
          auto path = settings["build_extensions_" + extension + "_path"];
          auto target = settings["build_extensions_" + extension + "_target"];

          auto sources = parseStringList(
            trim(
              settings[extensionKey] + " " +
              settings[scopedExtensionKey] + " " +
              settings[suffixScopedExtensionKey]
            ),
            ' '
          );

          auto objects = StringStream();
          auto lib = (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME));

          fs::create_directories(lib.parent_path());

          if (path.size() > 0) {
            fs::current_path(targetPath);
            fs::current_path(path);
          } else {
            fs::current_path(targetPath);
          }

          if (build.size() > 0) {
            auto exitCode = std::system((build + argvForward).c_str());
            if (exitCode) {
              exit(exitCode);
            }
          }

          if (configure.size() > 0) {
            ExecOutput result = exec(configure + argvForward);
            if (result.exitCode != 0) {
              logError("failed to configure extension: " + extension + ". Review configure script output above; consider verbose mode (-V)." + helpHint("build"));
              if (result.output.size() > 0) {
                logError(result.output);
              }
              exit(result.exitCode);
            }
            auto output = replace(result.output, "\n", " ");
            if (output.size() > 0) {
              for (const auto& source : parseStringList(output, ' ')) {
                sources.push_back(source);
              }
            }
          }

          if (copy.size() > 0) {
            auto pathResources = paths.pathResourcesRelativeToUserBuild;
            for (const auto& file : parseStringList(copy, ' ')) {
              auto parts = split(file, ':');
              auto target = parts[0];
              auto destination = parts.size() == 2 ? pathResources / parts[1] : pathResources;
              fs::copy(
                target,
                destination,
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
            }
          }

        if (sources.size() == 0) {
          continue;
        }

        if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) {
          logInfo("Building extension: " + extension + " (" + platform.os +  "-" + platform.arch + ")");
          extensions.push_back(extension);
        }

          if (target != "wasm32") {
            sources.push_back(prefixPath("src/init.cc").string());
          }

          auto CXX = env::get("CXX");
          auto CC = env::get("CC");

        for (auto source : sources) {
          if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
            logDebug("extension source: " + source);
          }

            auto compilerFlags = (
              settings["build_extensions_compiler_flags"] + " " +
              settings["build_extensions_" + os + "_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_" + os + "_compiler_flags"] + " "
            );

            auto compilerDebugFlags = (
              settings["build_extensions_compiler_debug_flags"] + " " +
              settings["build_extensions_" + os + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_" + os + "_compiler_debug_flags"] + " "
            );

            compilerFlags += " -DORO_RUNTIME_EXTENSION=1";

            if (platform.mac) {
              compilerFlags += " -framework UniformTypeIdentifiers";
              compilerFlags += " -framework CoreLocation";
              compilerFlags += " -framework Network";
              compilerFlags += " -framework UserNotifications";
              compilerFlags += " -framework Metal";
              compilerFlags += " -framework Accelerate";
              compilerFlags += " -framework WebKit";
              compilerFlags += " -framework Cocoa";
              compilerFlags += " -framework OSLog";
            }

            if (platform.linux) {
              compilerFlags += " " + trim(exec("pkg-config --cflags gtk+-3.0 webkit2gtk-4.1").output);
            }

            if (platform.win) {
              auto prefix = prefixFile();
              compilerFlags += " -I\"" + Path(paths.platformSpecificOutputPath / "include").string() + "\"";
              compilerFlags += " -I\"" + prefix + "include\"";
              compilerFlags += " -I\"" + prefix + "src\"";
              compilerFlags += " -L\"" + prefix + "lib\"";
            }

            if (platform.win && debugBuild) {
              compilerDebugFlags += "-D_DEBUG";
            }

            String compiler;

            if (source.ends_with(".hh") || source.ends_with(".h")) {
              continue;
            } else if (source.ends_with(".wasm")) {
              fs::copy(
                source,
                (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm")),
                fs::copy_options::update_existing | fs::copy_options::recursive
              );
              continue;
            } else if (source.ends_with(".cc") || source.ends_with(".cpp") || source.ends_with(".c++") || source.ends_with(".mm")) {
              compiler = CXX.size() > 0 && !IN_GITHUB_ACTIONS_CI ? CXX : "clang++";
              compilerFlags += " -v";
              compilerFlags += " -std=c++2a -v";
              if (platform.mac) {
                compilerFlags += " -ObjC++";
              } else if (platform.win) {
                compilerFlags += " -stdlib=libstdc++";
              }

              if (platform.win || platform.linux) {
                compilerFlags += " -Wno-unused-command-line-argument";
              }

              if (compiler.ends_with("clang++")) {
                compiler = compiler.substr(0, compiler.size() - 2);
              } else if (compiler.ends_with("clang++.exe")) {
                compiler = compiler.substr(0, compiler.size() - 6) + ".exe";
              } else if (compiler.ends_with("g++")) {
                compiler = compiler.substr(0, compiler.size() - 2) + "cc";
              } else if (compiler.ends_with("g++.exe")) {
                compiler = compiler.substr(0, compiler.size() - 6) + "cc.exe";
              }
            } else if (source.ends_with(".o") || source.ends_with(".a")) {
              objects << source << " ";
              continue;
            } else if (source.ends_with(".c") || source.ends_with(".m")) {
              compiler = CC.size() > 0 ? CC : "clang";
              if (platform.mac) {
                compilerFlags += " -ObjC -v";
              }
            } else {
              continue;
            }

        if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
          logDebug("extension source: " + source);
        }

            if (target == "wasm32" || target == "wasm32-wasi") {
              // just build sources
              continue;
            }

            auto objectFile = source;
            objectFile = replace(objectFile, "\\.mm$", ".o");
            objectFile = replace(objectFile, "\\.m$", ".o");
            objectFile = replace(objectFile, "\\.cc$", ".o");
            objectFile = replace(objectFile, "\\.c$", ".o");

            auto object = Path(objectFile);

            objects << (quote + object.string() + quote) << " ";
            auto compileExtensionObjectCommand = StringStream();
            compileExtensionObjectCommand
              << quote // win32 - quote the entire command
              << quote // win32 - quote the binary path
              << compiler
              << quote // win32 - quote the binary path
              << " -I" + Path(paths.platformSpecificOutputPath / "include").string()
              << (" -I" + quote + trim(prefixFile("include")) + quote)
              << (" -I" + quote + trim(prefixFile("src")) + quote)
              << (" -L" + quote + trim(prefixFile("lib")) + quote)
            #if defined(_WIN32)
              << (" -L" + quote + trim(prefixFile("lib\\" + platform.arch + "-desktop")) + quote)
              << " -D_MT"
              << " -D_DLL"
              << " -DWIN32"
              << " -DWIN32_LEAN_AND_MEAN"
              << " -Wno-nonportable-include-path"
            #else
              << (" -L" + quote + trim(prefixFile("lib/" + platform.arch + "-desktop")) + quote)
            #endif
              << " -fvisibility=hidden"
              << " -DIOS=0"
              // << " -U__CYGWIN__"
              << " -DANDROID=0"
              << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
              << " -DHOST=" << "\\\"" << devHost << "\\\""
              << " -DPORT=" << devPort
              << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
              << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
            #if !defined(_WIN32)
              << " -fPIC"
            #endif
              << " " << trim(compilerFlags + " " + (flagDebugMode ? compilerDebugFlags : ""))
              << " -c " << (quote + source + quote)
              << " -o " << (quote + object.string() + quote)
              << quote;

            struct stat sourceStats;
            struct stat objectStats;
            struct stat libraryStats;

            if (fs::exists(object)) {
              if (stat(convertWStringToString(source).c_str(), &sourceStats) == 0) {
                if (stat(convertWStringToString(object).c_str(), &objectStats) == 0) {
                  if (objectStats.st_mtime > sourceStats.st_mtime) {
                    continue;
                  }
                }
              }

              if (stat(convertWStringToString(source).c_str(), &sourceStats) == 0) {
                if (stat(convertWStringToString(lib).c_str(), &libraryStats) == 0) {
                  if (libraryStats.st_mtime > sourceStats.st_mtime) {
                    continue;
                  }
                }
              }
            }

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logVerbose(compileExtensionObjectCommand.str());
            }

            do {
              auto r = exec(compileExtensionObjectCommand.str());

              if (r.exitCode != 0) {
                logError("Unable to build extension object (" + object.string() + ")");
                logCapturedCommandOutput(r.output);
                exit(r.exitCode);
              }
            } while (0);
          }

          if (target == "wasm32") {
            String compiler;
            auto compilerFlags = (
              settings["build_extensions_compiler_flags"] + " " +
              settings["build_extensions_" + os + "_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_flags"] + " " +
              settings["build_extensions_" + extension + "_" + os + "_compiler_flags"] + " "
            );

            auto compilerDebugFlags = (
              settings["build_extensions_compiler_debug_flags"] + " " +
              settings["build_extensions_" + os + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_compiler_debug_flags"] + " " +
              settings["build_extensions_" + extension + "_" + os + "_compiler_debug_flags"] + " "
            );

            compilerFlags += " -DORO_RUNTIME_EXTENSION=1";
            compiler = CXX.size() > 0 && !IN_GITHUB_ACTIONS_CI ? CXX : "clang++";
            compilerFlags += " -v";
            compilerFlags += " -std=c++2a -v";
            if (platform.mac) {
              compilerFlags += " -ObjC++";
            } else if (platform.win) {
              compilerFlags += " -stdlib=libstdc++";
            }

            if (platform.win || platform.linux) {
              compilerFlags += " -Wno-unused-command-line-argument";
            }

            if (compiler.ends_with("clang++")) {
              compiler = compiler.substr(0, compiler.size() - 2);
            } else if (compiler.ends_with("clang++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + ".exe";
            } else if (compiler.ends_with("g++")) {
              compiler = compiler.substr(0, compiler.size() - 2) + "cc";
            } else if (compiler.ends_with("g++.exe")) {
              compiler = compiler.substr(0, compiler.size() - 6) + "cc.exe";
            }
            auto compileExtensionWASMCommand = StringStream();
            auto lib = (paths.pathResourcesRelativeToUserBuild / "oro" / "extensions" / extension / (extension + ".wasm"));
            fs::create_directories(lib.parent_path());
            compileExtensionWASMCommand
              << quote // win32 - quote the entire command
              << quote // win32 - quote the binary path
              << compiler
              << quote // win32 - quote the binary path
              << " -I" + Path(paths.platformSpecificOutputPath / "include").string()
              << (" -I" + quote + trim(prefixFile("include")) + quote)
              << (" -I" + quote + trim(prefixFile("include/oro/webassembly")) + quote)
              << (" -I" + quote + trim(prefixFile("src")) + quote)
              << (" -L" + quote + trim(prefixFile("lib")) + quote)
              << " -DORO_RUNTIME_EXTENSION_WASM=1"
              << " --target=wasm32"
              << " --no-standard-libraries"
              << " -Wl,--import-memory"
              << " -Wl,--allow-undefined"
              << " -Wl,--export-all"
              << " -Wl,--no-entry"
              << " -Wl,--demangle"
            #if defined(_WIN32)
              << (" -L" + quote + trim(prefixFile("lib\\" + platform.arch + "-desktop")) + quote)
              << " -D_MT"
              << " -D_DLL"
              << " -DWIN32"
              << " -DWIN32_LEAN_AND_MEAN"
              << " -Wno-nonportable-include-path"
            #else
              << (" -L" + quote + trim(prefixFile("lib/" + platform.arch + "-desktop")) + quote)
            #endif
              << " -fvisibility=hidden"
              << " -DIOS=0"
              << " -DANDROID=0"
              << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
              << " -DHOST=" << "\\\"" << devHost << "\\\""
              << " -DPORT=" << devPort
              << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
              << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
              << " " << trim(compilerFlags + " " + (flagDebugMode ? compilerDebugFlags : ""))
              << " " << join(sources, " ")
              << " -o " << (quote + lib.string() + quote);

            if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
              logVerbose(compileExtensionWASMCommand.str());
            }

            do {
              auto r = exec(compileExtensionWASMCommand.str());

              if (r.exitCode != 0) {
                logError("Unable to build WASM extension (" + extension + ")");
                logCapturedCommandOutput(r.output);
                exit(r.exitCode);
              }
            } while (0);
            continue;
          } else if (target == "wasm32-wasi") {
            // TODO
            continue;
          }

          auto linkerFlags = (
            settings["build_extensions_linker_flags"] + " " +
            settings["build_extensions_" + os + "_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_flags"] + " " +
            settings["build_extensions_" + extension + "_" + os + "_linker_flags"] + " "
          );

          auto linkerDebugFlags = (
            settings["build_extensions_linker_debug_flags"] + " " +
            settings["build_extensions_" + platform.os + "_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_linker_debug_flags"] + " " +
            settings["build_extensions_" + extension + "_" + os + "_linker_debug_flags"] + " "
          );

          if (platform.win && debugBuild) {
            linkerDebugFlags += "-D_DEBUG";
            for (String libString : split(env::get("WIN_DEBUG_LIBS"), ',')) {
              if (libString.size() > 0) {
                if (libString[0] == '\"' && libString[libString.size()-2] == '\"') {
                  libString = libString.substr(1, libString.size()-2);
                }

                Path lib(libString);
                if (!fs::exists(lib)) {
                  logWarn("WIN_DEBUG_LIBS: File doesn't exist, aborting build: " + lib.string());
                  exit(1);
                } else {
                  linkerDebugFlags += " " + lib.string();
                }
              }
            }
          }

#if defined(_WIN32)
          auto d = String(debugBuild ? "d" : "");
          auto static_uv = prefixFile("lib" + d + "\\" + platform.arch + "-desktop\\libuv.lib");
          auto static_libusb = prefixFile("lib" + d + "\\" + platform.arch + "-desktop\\libusb-1.0.lib");
          auto static_libsodium = prefixFile("lib" + d + "\\" + platform.arch + "-desktop\\libsodium.lib");
          auto static_llama = prefixFile("lib" + d + "\\" + platform.arch + "-desktop\\llama.lib");
          auto static_whisper = prefixFile("lib" + d + "\\" + platform.arch + "-desktop\\whisper.lib");
          auto static_runtime = resolveRuntimeStaticArchive("lib" + d + "/" + platform.arch + "-desktop", d + ".a").string();
          const auto static_libusb_arg = fs::exists(Path(static_libusb)) ? (" " + static_libusb) : String("");
          const auto static_libsodium_arg = fs::exists(Path(static_libsodium)) ? (" " + static_libsodium) : String("");
#else
          auto d = "";
          auto static_uv = "";
          auto static_libusb = "";
          auto static_libsodium = "";
          auto static_llama = "";
          auto static_whisper = "";
          auto static_runtime = "";
          const auto static_libusb_arg = String("");
          const auto static_libsodium_arg = String("");
#endif

          auto compileExtensionLibraryCommand = StringStream();
          compileExtensionLibraryCommand
            << quote // win32 - quote the entire command
            << quote // win32 - quote the binary path
            << env::get("CXX")
            << quote // win32 - quote the binary path
            << " " << static_runtime
            << " " << static_llama
            << " " << static_whisper
            << " " << static_uv
            << static_libusb_arg
            << static_libsodium_arg
            << " " << objects.str()
          #if defined(_WIN32)
            << (" -L" + quote + trim(prefixFile("lib\\" + platform.arch + "-desktop")) + quote)
            << " -D_MT"
            << " -D_DLL"
            << " -DWIN32"
            << " -DWIN32_LEAN_AND_MEAN"
            << " -Wl,-NODEFAULTLIB:libcmt"
            << " -Wno-nonportable-include-path"
          #else
            << " " << flags
            << " " << extraFlags
          #if defined(__linux__)
            << " -Wl,--exclude-libs,ALL"
            << " -luv"
            << " -lusb-1.0"
            << " -lllama"
            << " -lwhisper"
            << " " << runtimeLinkFlag()
          #endif
            << (" -L" + quote + trim(prefixFile("lib/" + platform.arch + "-desktop")) + quote)
          #endif
            << " -fvisibility=hidden"
            << " " << trim(linkerFlags + " " + (flagDebugMode ? linkerDebugFlags : ""))
            << " -I" + Path(paths.platformSpecificOutputPath / "include").string()
            << (" -I" + quote + trim(prefixFile("include")) + quote)
            << (" -I" + quote + trim(prefixFile("src")) + quote)
            << (" -L" + quote + trim(prefixFile("lib")) + quote)
            << " -shared"
            << " -std=c++2a"
          #if defined(__linux__)
            << " -fPIC"
          #endif
            << " -o " << (quote + lib.string() + quote)
            << quote; // win32 - quote the entire command

          if (platform.mac) {
            if (isForDesktop) {
              settings["mac_codesign_paths"] += (
                paths.pathResourcesRelativeToUserBuild /
                "oro" /
                "extensions" /
                extension /
                (extension + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME)
              ).string() + ";";
            }
          }

          if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1") {
            logDebug(compileExtensionLibraryCommand.str());
          }

          fs::create_directories(lib.parent_path());
          do {
            auto r = exec(compileExtensionLibraryCommand.str());

            if (r.exitCode != 0) {
              logError("Unable to build extension (" + extension + ")");
              logCapturedCommandOutput(r.output);
              exit(r.exitCode);
            }
          } while (0);

          if (!flagDebugMode) {
            for (const auto& object : parseStringList(objects.str(), ' ')) {
              fs::remove_all(object);
            }
          }
        }
      }

      fs::current_path(oldCwd);
    }

    if (flagRunUserBuildOnly == false && isForDesktop && !isForExtensionOnly) {
      // Ensure output directory exists before compiling the native binary.
      // On Linux, paths.pathBin points inside the package tree (opt/<app>),
      // and clang++ will fail with ENOENT if the parent path is missing.
      try {
        fs::create_directories(paths.pathBin);
      } catch (const fs::filesystem_error &e) {
        logError("unable to create output directory: " + paths.pathBin.string());
        logError(e.what());
        exit(1);
      }
      StringStream compileCommand;

      // windows / spaces in bin path - https://stackoverflow.com/a/27976653/3739540
          compileCommand
        << quote // win32 - quote the entire command
        << quote // win32 - quote the binary path
        << env::get("CXX")
        << quote // win32 - quote the binary path
        << " " << files
        << " " << flags
        << " " << extraFlags
        << " -o \"" << binaryPath.string() << "\""
        << " -DIOS=" << (flagBuildForIOS ? 1 : 0)
        << " -DANDROID=" << (flagBuildForAndroid ? 1 : 0)
        << " -DDEBUG=" << (flagDebugMode ? 1 : 0)
        << " -DHOST=" << "\\\"" << devHost << "\\\""
        << " -DPORT=" << devPort
            << " -DORO_RUNTIME_VERSION=" << VERSION_STRING
            << " -DORO_RUNTIME_VERSION_HASH=" << VERSION_HASH_STRING
            << quote; // win32 - quote the entire command

      if (env::get("DEBUG") == "1" || env::get("VERBOSE") == "1")
        logVerbose(compileCommand.str());

      auto r = exec(compileCommand.str());

      if (r.exitCode != 0) {
        logError("Unable to build");
        logCapturedCommandOutput(r.output);
        exit(r.exitCode);
      }

      logInfo("compiled native binary");
    }

    //
    // Linux Packaging
    // ---
    //
    if (flagShouldPackage && platform.linux && isForDesktop) {
      auto packageFormat = settings["build_linux_package_format"];

      if (optionsWithValue["--package-format"].size()) {
        packageFormat = optionsWithValue["--package-format"];
      }

      if (packageFormat.size() == 0) {
        packageFormat = "deb";
      }

      if (packageFormat == "deb") {
        Path pathSymLinks = {
          paths.pathPackage /
            "usr" /
            "local" /
            "bin"
        };

        auto linuxExecPath = Path {
          Path("/opt") /
            settings["build_name"] /
            settings["build_name"]
        };

        fs::create_directories(pathSymLinks);
        fs::create_symlink(
          linuxExecPath,
          pathSymLinks / settings["build_name"]
        );

        StringStream archiveCommand;

        archiveCommand
          << "dpkg-deb --build --root-owner-group "
          << paths.pathPackage.string()
          << " "
          << (paths.platformSpecificOutputPath).string();

        if (debugEnv || verboseEnv) {
          logVerbose(archiveCommand.str());
        }

        auto r = exec(archiveCommand.str());

        if (r.exitCode != 0) {
          logError("Build packaging failed for Linux. Ensure you have necessary tools (dpkg, fakeroot) installed. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        Path debPath = paths.platformSpecificOutputPath / (paths.pathPackage.filename().string() + ".deb");
        if (flagShouldSignPackages) {
          signArtifactWithGpg(debPath, signKeyId, debugEnv, verboseEnv);
        }
      } else if (packageFormat == "rpm") {
        Path pathSymLinks = {
          paths.pathPackage /
            "usr" /
            "local" /
            "bin"
        };

        auto linuxExecPath = Path {
          Path("/opt") /
            settings["build_name"] /
            settings["build_name"]
        };

        fs::create_directories(pathSymLinks);
        fs::create_symlink(
          linuxExecPath,
          pathSymLinks / settings["build_name"]
        );

        if (settings["linux_rpm_release"].size() == 0) {
          settings["linux_rpm_release"] = "1";
        }

        if (settings["linux_rpm_arch"].size() == 0) {
          auto rpmArch = platform.arch;
          if (rpmArch == "arm64") {
            rpmArch = "aarch64";
          }
          settings["linux_rpm_arch"] = rpmArch;
        }

        if (settings["linux_rpm_requires"].size() == 0) {
          settings["linux_rpm_requires"] = "gtk3 webkit2gtk-4.1 dbus";
        }

        const auto specPath = paths.platformSpecificOutputPath / (settings["build_name"] + ".spec");
        writeFile(specPath, tmpl(gRpmSpec, settings));

        StringStream rpmCommand;
        rpmCommand
          << "rpmbuild -bb "
          << "\"" << specPath.string() << "\""
          << " --buildroot \"" << paths.pathPackage.string() << "\""
          << " --define '_rpmdir " << paths.platformSpecificOutputPath.string() << "'";

        if (debugEnv || verboseEnv) {
          logVerbose(rpmCommand.str());
        }

        auto r = exec(rpmCommand.str().c_str());
        if (r.exitCode != 0) {
          logError("Build packaging failed for Linux RPM. Ensure you have necessary tools (rpmbuild) installed. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        Path rpmPath;
        try {
          for (const auto& entry : fs::recursive_directory_iterator(paths.platformSpecificOutputPath)) {
            if (entry.path().extension() == ".rpm") {
              rpmPath = entry.path();
              break;
            }
          }
        } catch (const fs::filesystem_error& e) {
          logWarn("failed to locate RPM artifact: " + String(e.what()));
        }

        if (!rpmPath.empty()) {
          if (flagShouldSignPackages) {
            signArtifactWithGpg(rpmPath, signKeyId, debugEnv, verboseEnv);
          }
        } else {
          logWarn("RPM packaging succeeded but .rpm artifact was not found under output directory");
        }
      } else if (packageFormat == "zip") {
        StringStream zipCommand;

          zipCommand
            << "zip -r"
            << " " << (paths.platformSpecificOutputPath / (paths.pathPackage.filename().string() + ".zip")).string()
            << " " << paths.pathPackage.filename();

        if (debugEnv || verboseEnv) {
          logVerbose(zipCommand.str());
        }

        auto cwd = fs::current_path();
        fs::current_path(paths.platformSpecificOutputPath);
        auto r = exec(zipCommand.str());

        if (r.exitCode != 0) {
          logError("Build packaging failed for Linux zip format. Ensure you have necessary tools (zip) installed. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }
        fs::current_path(cwd);
        if (flagShouldSignPackages) {
          Path zipPath = paths.platformSpecificOutputPath / (paths.pathPackage.filename().string() + ".zip");
          signArtifactWithGpg(zipPath, signKeyId, debugEnv, verboseEnv);
        }
      } else if (packageFormat == "aur") {
        if (settings["linux_aur_pkgname"].size() == 0) {
          settings["linux_aur_pkgname"] = settings["build_name"];
        }

        if (settings["linux_aur_pkgrel"].size() == 0) {
          settings["linux_aur_pkgrel"] = "1";
        }

        if (settings["linux_aur_arch"].size() == 0) {
          auto aurArch = platform.arch;
          if (aurArch == "arm64") {
            aurArch = "aarch64";
          }
          settings["linux_aur_arch"] = aurArch;
        }

        if (settings["linux_aur_depends"].size() == 0) {
          settings["linux_aur_depends"] = "gtk3 webkit2gtk-4.1 dbus";
        }

        Path aurRoot = paths.platformSpecificOutputPath / "aur" / settings["build_name"];
        fs::create_directories(aurRoot);

        writeFile(aurRoot / "PKGBUILD", tmpl(gAurPKGBuild, settings));

        logInfo("wrote AUR PKGBUILD to '" + (aurRoot / "PKGBUILD").string() + "'");
      } else {
        const String cliName(gCliDisplayName);
        logError("Unknown package format given in '[build.linux.package] format = \"" + packageFormat + "\"'. See '" + cliName + " build --help' (Linux options)");
        exit(1);
      }
    }

    //
    // MacOS Stripping
    //
    if (platform.mac && isForDesktop && !isForExtensionOnly) {
      StringStream stripCommand;

      stripCommand
        << "strip"
        << " " << binaryPath
        << " -u";

      auto r = exec(stripCommand.str().c_str());
      if (r.exitCode != 0) {
        logError("strip failed");
        logDebug(r.output);
        exit(r.exitCode);
      }
    }

    //
    // MacOS Code Signing
    // ---
    //
    if (flagCodeSign && platform.mac && isForDesktop) {
      //
      // https://www.digicert.com/kb/code-signing/mac-os-codesign-tool.htm
      // https://developer.apple.com/forums/thread/128166
      // https://wiki.lazarus.freepascal.org/Code_Signing_for_macOS
      //
      Path pathBase = "Contents";
      StringStream signCommand;
      String entitlements = "";

      Map<> entitlementSettings;
      extendMap(entitlementSettings, settings);

      entitlementSettings["configured_entitlements"] += (
        "  <key>com.apple.security.network.server</key>\n"
        "  <true/>\n"
        "  <key>com.apple.security.network.client</key>\n"
        "  <true/>\n"
        "  <key>com.apple.security.cs.allow-jit</key>\n"
        "  <true/>\n"
        "  <key>com.apple.security.files.user-selected.read-write</key>\n"
        "  <true/>\n"
        "  <key>com.apple.security.files.bookmarks.app-scope</key>\n"
        "  <true/>\n"
        "  <key>com.apple.security.temporary-exception.files.absolute-path.read-write</key>\n"
        "  <true/>\n"
      );

      if (settings["meta_application_links"].size() > 0) {
        const auto links = parseStringList(trim(settings["meta_application_links"]), ' ');
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.developer.associated-domains</key>\n"
          "  <array>\n"
        );

        for (const auto link : links) {
          entitlementSettings["configured_entitlements"] += (
            "    <string>applinks:" + link + "</string>\n"
          );
        }

        entitlementSettings["configured_entitlements"] += (
          "  </array>\n"
        );
      }

      if (settings["permissions_allow_user_media"] != "false") {
        if (settings["permissions_allow_camera"] != "false") {
          entitlementSettings["configured_entitlements"] += (
            "  <key>com.apple.security.device.camera</key>\n"
            "  <true/>\n"
          );
        }

        if (settings["permissions_allow_microphone"] != "false") {
          entitlementSettings["configured_entitlements"] += (
            "  <key>com.apple.security.device.microphone</key>\n"
            "  <true/>\n"
            "  <key>com.apple.security.device.audio-input</key>\n"
            "  <true/>\n"
          );
        }
      }

      if (settings["permissions_allow_bluetooth"] != "false") {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.device.bluetooth</key>\n"
          "  <true/>\n"
        );
      }

      if (settings["permissions_allow_geolocation"] != "false") {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.personal-information.location</key>\n"
          "  <true/>\n"
        );
      }

      if (settings["apple_team_identifier"].size() > 0) {
        auto identifier = (
          settings["apple_team_identifier"] +
          "." +
          settings["meta_bundle_identifier"]
        );

        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.application-identifier</key>\n"
          "  <string>" + identifier  + "</string>\n"
        );
      }

      if (settings["mac_sandbox"] != "false") {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.app-sandbox</key>\n"
          "  <true/>\n"
          "  <key>com.apple.security.inherit</key>\n"
          "  <true/>\n"
        );

        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.temporary-exception.files.home-relative-path.read-write</key>\n"
          "  <array>\n"
        );

        for (const auto& tuple : settings) {
          if (tuple.first.starts_with("webview_navigator_mounts_")) {
            const auto key = replace(
              replace(tuple.first, "webview_navigator_mounts_", ""),
              "mac_",
              ""
            );

            if (
              key.starts_with("android") ||
              key.starts_with("ios") ||
              key.starts_with("linux") ||
              key.starts_with("win")
            ) {
              continue;
            }

            if (key.starts_with("$HOST_HOME") || key.starts_with("~")) {
              const auto path = replace(replace(key, "^(\\$HOST_HOME)", ""), "^(~)", "");
              entitlementSettings["configured_entitlements"] += (
                "    <string>" + (path.ends_with("/") ? path : path + "/") + "</string>\n"
              );
            }
          }
        }

        entitlementSettings["configured_entitlements"] += (
          "  </array>\n"
        );
      }

      if (flagDebugMode) {
        entitlementSettings["configured_entitlements"] += (
          "  <key>com.apple.security.cs.debugger</key>\n"
          "  <true/>\n "
        );
      }

      writeFile(
        paths.platformSpecificOutputPath / "oro.entitlements",
        tmpl(gXcodeEntitlements, entitlementSettings)
      );

      if (settings.count("mac_codesign_identity") == 0) {
        logError("'[mac.codesign] identity' key/value is required");
        exit(1);
      }

      StringStream archiveCommand;
      String destination = "platform=macOS";

      if (!settings.contains("mac_codesign_identity")) {
        settings["mac_codesign_identity"] = "";
      }

      const auto configuration = String(flagDebugMode ? "Debug" : "Release");
      const auto args = flagShouldPackage
        ? String("archive")
        : "CONFIGURATION_BUILD_DIR=" + paths.platformSpecificOutputPath.string();

      archiveCommand
        << "xcodebuild"
        << " build " << args
        << " -allowProvisioningUpdates"
        << " -scheme " << settings["build_name"]
        << " -destination '" << destination << "'"
        << " -configuration " << configuration;

      if (flagShouldPackage) {
        archiveCommand << " -archivePath build/" << settings["build_name"];
      }

      if (flagBuildForSimulator) {
        archiveCommand << " ARCHS=" << platform.arch << " ONLY_ACTIVE_ARCH=NO";
      }

      archiveCommand
        << " CODE_SIGN_IDENTITY=\"" + settings["mac_codesign_identity"] + "\""
        << " CODE_SIGN_ENTITLEMENTS=\"" + (paths.platformSpecificOutputPath.string() + "/oro.entitlements") +  "\"";

      // log(archiveCommand.str().c_str());
      auto rArchive = exec(archiveCommand.str().c_str());

      if (rArchive.exitCode != 0) {
        logError("failed to archive project");
        logDebug(rArchive.output);
        fs::current_path(oldCwd);
        exit(1);
      }

      StringStream commonFlags;

      commonFlags
        << " --force"
        << " --options runtime"
        << " --timestamp"
        << " --entitlements " << (paths.platformSpecificOutputPath / "oro.entitlements").string()
        << " --identifier '" << settings["meta_bundle_identifier"] << "'"
        << " --sign '" << settings["mac_codesign_identity"] << "'"
        << " ";

      if (settings["mac_codesign_paths"].size() > 0) {
        auto paths = split(settings["mac_codesign_paths"], ';');

        for (int i = 0; i < paths.size(); i++) {
          String prefix = (i > 0) ? ";" : "";

          signCommand
            << prefix
            << "codesign "
            << commonFlags.str()
            << (pathResources / paths[i]).string();
        }
        signCommand << "&& ";
      }

      signCommand
        << "codesign"
        << commonFlags.str()
        << binaryPath.string()

        << " && codesign"
        << commonFlags.str()
        << paths.pathPackage.string()

        << " && codesign"
        << commonFlags.str()
        << (paths.pathPackage / "Contents" / "MacOS" / "libomp.dylib").string();

      if (flagDebugMode || flagVerboseMode) {
        logDebug(signCommand.str());
      }

      auto r = exec(signCommand.str());

      if (r.output.size() > 0) {
        if (r.exitCode != 0) {
          logError("Unable to sign application with 'codesign'. Ensure signing identities/certificates are configured and unlocked. See README (Troubleshooting)");
          if (flagDebugMode || flagVerboseMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }
      }

      if (flagVerboseMode) {
        logVerbose(r.output);
      }

      logInfo("Successfully code signed app with 'codesign'");
    }

    if (platform.mac && isForDesktop) {
      auto packageFormat = settings["build_mac_package_format"];

      if (optionsWithValue["--package-format"].size()) {
        packageFormat = optionsWithValue["--package-format"];
      }

      if (packageFormat.size() == 0) {
        packageFormat = "zip";
        settings["build_mac_package_format"] = packageFormat;
      }

      pathToArchive = paths.platformSpecificOutputPath / (settings["build_name"] + "." + packageFormat);
      if (!flagShouldPackage && flagShouldNotarize && !fs::exists(pathToArchive)) {
        flagShouldPackage = true;
      }
    }

    //
    // MacOS Packaging
    // ---
    //
    if (flagShouldPackage && platform.mac && isForDesktop) {
      auto packageFormat = settings["build_mac_package_format"];

      if (optionsWithValue["--package-format"].size()) {
        packageFormat = optionsWithValue["--package-format"];
      }

      if (packageFormat == "zip") {
        StringStream zipCommand;
        zipCommand
          << "ditto"
          << " -c"
          << " -k"
          << " --sequesterRsrc"
          << " --keepParent"
          << " "
          << paths.pathPackage.string()
          << " "
          << pathToArchive.string();

        auto r = exec(zipCommand.str());

        if (r.exitCode != 0) {
          logError("Build packaging fails for macOS. Ensure codesign/notary setup is correct. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }
      } else if (packageFormat == "pkg") {
        StringStream productBuildCommand;
        auto identity = settings["mac_productbuild_identity"];
        auto productBuildInstallPath = settings["mac_productbuild_install_path"];

        if (identity.size() == 0) {
          const String cliName(gCliDisplayName);
          logError("Missing '[mac.productbuild] identity = ...' in your config (oro.toml or oro.ini). See '" + cliName + " build --help' (macOS options)");
          exit(1);
        }

        if (productBuildInstallPath.size() == 0) {
          productBuildInstallPath = "/Applications";
        }

        auto productBuildOutput = (paths.platformSpecificOutputPath / Path(settings["build_name"] + ".pkg")).string();
        productBuildCommand
          << "xcrun productbuild"
          << " --sign '" << identity << "'"
          << " --version '" << settings["meta_version"] << "'"
          << " --component '" << paths.pathPackage.string() << "'" << " " << productBuildInstallPath
          << " --identifier '" << settings["meta_bundle_identifier"] << "'"
          << " --timestamp"
          << " " << productBuildOutput;

        if (verboseEnv) {
          logVerbose(productBuildCommand.str());
        }

        auto r = exec(productBuildCommand.str());

        if (r.exitCode != 0) {
          logError("Failed to package macOS application with 'productbuild'. Ensure 'productbuild' (Xcode Command Line Tools) is installed and identities are valid. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        if (verboseEnv) {
          logVerbose(r.output);
        }

        r = exec(String("xcrun pkgutil --check-signature ") + productBuildOutput);

        if (r.exitCode != 0) {
          logError("Failed to verify macOS package signature with 'pkgutil'. Ensure signatures are valid and 'pkgutil' (Xcode Command Line Tools) is installed. See README (Troubleshooting)");
          if (flagDebugMode) {
            logDebug(r.output);
          }
          exit(r.exitCode);
        }

        if (verboseEnv) {
          logVerbose(r.output);
        }
      } else {
        const String cliName(gCliDisplayName);
        logError("Unknown package format given in '[build.mac.package] format = \"" + packageFormat + "\"'. See '" + cliName + " build --help' (macOS options)");
        exit(1);
      }

      logInfo("created package artifact");
    }

    //
    // MacOS Notarization
    // ---
    //
    if (flagShouldNotarize && platform.mac && isForDesktop) {
      StringStream notarizeCommand;
      String username = env::get("APPLE_ID");
      String password = env::get("APPLE_ID_PASSWORD");

      if (username.size() == 0) {
        username = settings["apple_identifier"];
      }

      if (username.size() == 0) {
        logError(
          "AppleID identifier could not be determined. "
          "Please set '[apple] identifier = ...' or 'APPLE_ID' environment variable."
        );
        exit(1);
      }

      if (password.size() == 0) {
        logError(
          "AppleID identifier could not be determined. "
          "Please set the 'APPLE_ID_PASSWORD' environment variable."
        );
        exit(1);
      }

      if (!fs::exists(pathToArchive)) {
        logError(
          "Cannot notarize application: Package archive does not exist."
        );
        exit(1);
      }

      notarizeCommand
        << "xcrun"
        << " notarytool submit"
        << " --wait"
        << " --apple-id \"" << username << "\""
        << " --password \"" << password << "\"";

      if (settings["apple_team_identifier"].size() > 0) {
        notarizeCommand << " --team-id " << settings["apple_team_identifier"];
      }

      notarizeCommand << " \"" << pathToArchive.string() << "\"";

      if (flagDebugMode) {
        logDebug(notarizeCommand.str());
      }

      auto r = exec(notarizeCommand.str().c_str());

      if (r.exitCode != 0) {
        logError("Unable to notarize macOS application. Ensure Xcode Command Line Tools are installed (xcode-select --install), credentials are configured (xcrun notarytool store-credentials), and team-id is set. See README (Troubleshooting)");

        if (flagDebugMode) {
          logDebug(r.output);
        }

        exit(r.exitCode);
      }

      std::regex re(R"(\nRequestUUID = (.+?)\n)");
      std::smatch match;
      String uuid;

      if (std::regex_search(r.output, match, re)) {
        uuid = match.str(1);
      }

      int requests = 0;

      logInfo("Polling for notarization");

      while (!uuid.empty()) {
        if (++requests > 1024) {
          logWarn("apple did not respond to the request for notarization");
          exit(1);
        }

        msleep(1024 * 6);
        StringStream notarizeStatusCommand;

        notarizeStatusCommand
          << "xcrun"
          << " altool"
          << " --notarization-info " << uuid
          << " -u " << username
          << " -p " << password;

        auto r = exec(notarizeStatusCommand.str().c_str());

        std::regex re(R"(\n *Status: (.+?)\n)");
        std::smatch match;
        String status;

        if (std::regex_search(r.output, match, re)) {
          status = match.str(1);
        }

        if (status.find("in progress") != -1) {
          logVerbose("Checking for updates from apple");
          continue;
        }

        auto lastStatus = r.output;

        if (status.find("invalid") != -1) {
          logError("apple rejected the request for notarization");

          logDebug(lastStatus);

          StringStream notarizeHistoryCommand;

          notarizeHistoryCommand
            << "xcrun"
            << " altool"
            << " --notarization-history 0"
            << " -u " << username
            << " -p " << password;

          auto r = exec(notarizeHistoryCommand.str().c_str());

          if (r.exitCode != 0) {
            logError("Unable to get notarization history");
            exit(r.exitCode);
          }

          logDebug(r.output);

          exit(1);
        }

        if (status.find("success") != -1) {
          logInfo("successfully notarized");
          break;
        }

        if (status.find("success") == -1) {
          logWarn("Apple was unable to notarize. Inspect notary log; ensure credentials, entitlements, and bundle identifiers are correct. See README (Troubleshooting)");
          break;
        }
      }

      logInfo("finished notarization");
    }

    //
    // Windows Packaging
    // ---
    //
    if (flagShouldPackage && platform.win && isForDesktop) {
      #ifdef _WIN32

      auto GetPackageWriter = [&](_In_ LPCWSTR outputFileName, _Outptr_ IAppxPackageWriter** writer) {
        HRESULT hr = S_OK;
        IStream* outputStream = NULL;
        IUri* hashMethod = NULL;
        APPX_PACKAGE_SETTINGS packageSettings = {0};
        IAppxFactory* appxFactory = NULL;

        hr = SHCreateStreamOnFileEx(
          outputFileName,
          STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
          0, // default file attributes
          TRUE, // create file if it does not exist
          NULL, // no template
          &outputStream
        );

        if (SUCCEEDED(hr)) {
          hr = CreateUri(
            L"http://www.w3.org/2001/04/xmlenc#sha256",
            Uri_CREATE_CANONICALIZE,
            0, // reserved parameter
            &hashMethod
          );
        }

        if (SUCCEEDED(hr)) {
            packageSettings.forceZip32 = TRUE;
            packageSettings.hashMethod = hashMethod;
        }

        // Create a new Appx factory
        if (SUCCEEDED(hr)) {
          hr = CoCreateInstance(
            __uuidof(AppxFactory),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IAppxFactory),
            (LPVOID*)(&appxFactory)
          );
        }

        // Create a new package writer using the factory
        if (SUCCEEDED(hr)) {
          hr = appxFactory->CreatePackageWriter(
            outputStream,
            &packageSettings,
            writer
          );
        }

        // Clean up allocated resources
        if (appxFactory != NULL) {
          appxFactory->Release();
          appxFactory = NULL;
        }

        if (hashMethod != NULL) {
          hashMethod->Release();
          hashMethod = NULL;
        }

        if (outputStream != NULL) {
          outputStream->Release();
          outputStream = NULL;
        }

        return hr;
      };

      WString appx(convertStringToWString(paths.pathPackage.string()) + L".appx");

      HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

      if (SUCCEEDED(hr)) {
        IAppxPackageWriter* packageWriter = NULL;

        hr = GetPackageWriter(appx.c_str(), &packageWriter);

        std::function<void(LPCWSTR, Path)> addFiles = [&](auto basePath, auto last) {
          for (const auto & entry : fs::directory_iterator(basePath)) {
            auto p = entry.path().filename().string();

            LPWSTR mime = 0;
            FindMimeFromData(NULL, entry.path().c_str(), NULL, 0, NULL, 0, &mime, 0);

            if (p.find("AppxManifest.xml") == 0) {
              continue;
            }

            if (fs::is_directory(entry.path())) {
              addFiles(entry.path().c_str(), Path { last / entry.path().filename() });
              continue;
            }

            auto composite = (Path { last / entry.path().filename() });

            if (!mime) {
              mime = (LPWSTR) L"application/octet-stream";
            }

            IStream* fileStream = NULL;
            hr = SHCreateStreamOnFileEx(
              entry.path().c_str(),
              STGM_READ | STGM_SHARE_EXCLUSIVE,
              0,
              FALSE,
              NULL,
              &fileStream
            );

            if (SUCCEEDED(hr)) {
              auto hr2 = packageWriter->AddPayloadFile(
                composite.c_str(),
                mime,
                APPX_COMPRESSION_OPTION_NONE,
                fileStream
              );

              if (SUCCEEDED(hr2)) {
              } else {
                logWarn("mimetype?: " + convertWStringToString(mime));
                logWarn("Could not add payload file: " + entry.path().string());
              }
            } else {
              logWarn("Could not add file: " + entry.path().string());
            }

            if (fileStream != NULL) {
              fileStream->Release();
              fileStream = NULL;
            }
          }
        };

        addFiles(paths.pathPackage.c_str(), Path {});

        IStream* manifestStream = NULL;

        if (SUCCEEDED(hr)) {
          auto p = Path({ paths.pathPackage / "AppxManifest.xml" });

          hr = SHCreateStreamOnFileEx(
            p.c_str(),
            STGM_READ | STGM_SHARE_EXCLUSIVE,
            0,
            FALSE,
            NULL,
            &manifestStream
          );
        } else {
          logWarn("Could not get package writer or add files");
        }

        if (SUCCEEDED(hr)) {
          hr = packageWriter->Close(manifestStream);
        } else {
          logWarn("Could not generate AppxManifest.xml");
          // log("Run MakeAppx.exe manually to see more errors");
        }

        if (manifestStream != NULL) {
          manifestStream->Release();
          manifestStream = NULL;
        }
        if (packageWriter != NULL) {
          packageWriter->Release();
          packageWriter = NULL;
        }

        CoUninitialize();
      } else {
        logError("Unable to initialize package writer");
      }

      if (SUCCEEDED(hr)) {
        logInfo("Package saved");
      } else {
        _com_error err(hr);
        String msg = convertWStringToString( err.ErrorMessage() );
        logError("Unable to save package; " + msg);

        exit(1);
      }

      #endif
    }


    //
    // Windows Code Signing
    //
    if (flagCodeSign && platform.win && isForDesktop) {
      //
      // https://www.digicert.com/kb/code-signing/signcode-signtool-command-line.htm
      //
      auto sdkRoot = Path("C:\\Program Files (x86)\\Windows Kits\\10\\bin");
      auto pathToSignTool = env::get("SIGNTOOL");

      if (pathToSignTool.size() == 0) {
        // TODO assumes the last dir that contains dot. posix doesnt guarantee
        // order, maybe windows does, but this should probably be smarter.
        for (const auto& entry : fs::directory_iterator(sdkRoot)) {
          auto p = entry.path().string();
          if (p.find(".") != -1) pathToSignTool = p;
        }

        pathToSignTool = Path {
          Path(pathToSignTool) /
          "x64" /
          "signtool.exe"
        }.string();
      }

      if (pathToSignTool.size() == 0) {
      logWarn("Can't find Windows 10 SDK; assuming signtool.exe is in PATH. Install Windows 10/11 SDK via Visual Studio Installer or 'choco install windows-sdk-10-version' and add SDK bin to PATH.");
        pathToSignTool = "signtool.exe";
      }

      StringStream signCommand;
      String password = env::get("CSC_KEY_PASSWORD");

      if (password.size() == 0) {
        logError("Env variable 'CSC_KEY_PASSWORD' is empty! Provide a password for code signing. See README (Troubleshooting)");
        exit(1);
      }

      signCommand
        << "\""
        << "\"" << pathToSignTool << "\""
        << " sign"
        << " /debug"
        << " /f " << settings["win_pfx"]
        << " /p \"" << password << "\""
        << " /v"
        << " /tr http://timestamp.digicert.com"
        << " /td sha256"
        << " /fd sha256"
        << " " << paths.pathPackage.string() << ".appx"
        << "\"";

      logVerbose(signCommand.str());
      auto r = exec(signCommand.str().c_str());

      if (r.exitCode != 0) {
        logError("Unable to sign");
        logDebug(paths.pathPackage.string());
        logDebug(r.output);
        exit(r.exitCode);
      }
    }

    if (flagShouldWatch) {
      Vector<String> sources;

      auto copyMapFiles = handleBuildPhaseForCopyMappedFiles(
        settings,
        targetPlatform,
        pathResourcesRelativeToUserBuild,
        false
      );

      if (settings.contains("build_watch_sources")) {
        const auto buildWatchSources = parseStringList(trim(settings["build_watch_sources"]), ' ');
        for (const auto& source : buildWatchSources) {
          sources.push_back((fs::current_path() / source).string());
        }
      }

      if (copyMapFiles.size() > 0) {
        for (const auto& file : copyMapFiles) {
          sources.push_back((fs::current_path() / file).string());
        }
      }

      // allow changes to config files to be observed
      sources.push_back((fs::current_path() / "oro.toml").string());
      sources.push_back((fs::current_path() / "oro.ini").string());

      sourcesWatcher = new filesystem::Watcher(sources);

      if (settings["build_watch_debounce_timeout"].size() > 0) {
        const auto timeout = settings["build_watch_debounce_timeout"];
        try {
          sourcesWatcher->options.debounce = std::atoi(timeout.c_str());
        } catch (Exception e) {
          logError(
            "Invalid value given for '[build.watch] debounce_timeout': '" + timeout + "'. " +
            "Expecting an integer value in milliseconds"
          );
          exit(1);
        }
      }

      auto watchingSources = sourcesWatcher->start([=](
        const String& path,
        const Vector<filesystem::Watcher::Event>& events,
        const filesystem::Watcher::Context& context
      ) {
        logVerbose("File '" + path + "' did change");

        auto settingsForSourcesWatcher = settings;

        Map<> reloadedConfig;
        const auto preferredConfigPath = targetPath / "oro.toml";
        const auto oroIniConfigPath = targetPath / "oro.ini";

        const bool hasPreferred = fileExists(preferredConfigPath);
        const bool hasOroIni = fileExists(oroIniConfigPath);

        Path reloadConfigPath;
        auto reloadFormat = UserConfigFormat::Ini;

        if (hasPreferred) {
          reloadConfigPath = preferredConfigPath;
          reloadFormat = UserConfigFormat::Toml;
        } else if (hasOroIni) {
          reloadConfigPath = oroIniConfigPath;
          reloadFormat = UserConfigFormat::Ini;
        }

        if (!reloadConfigPath.empty()) {
          try {
            reloadedConfig = parseUserConfigSource(
              readFile(reloadConfigPath),
              reloadFormat
            );
          } catch (const std::exception& error) {
            logError(
              "failed to parse configuration file '" +
              reloadConfigPath.string() +
              "' while reloading: " +
              String(error.what())
            );
            return;
          }
        }

        extendMap(settingsForSourcesWatcher, reloadedConfig);

        handleBuildPhaseForUser(
          settingsForSourcesWatcher,
          targetPlatform,
          pathResourcesRelativeToUserBuild,
          targetPath,
          false
        );

        handleBuildPhaseForCopyMappedFiles(
          settingsForSourcesWatcher,
          targetPlatform,
          pathResourcesRelativeToUserBuild
        );
      });

      if (!watchingSources) {
        logError("Unable to start watching");
        exit(1);
      }

      logInfo("Watching for changes in: " + join(sourcesWatcher->watchedPaths, ", "));
    #if defined(__linux__) && !defined(__ANDROID__)
      // `filesystem::Watcher` will use GTK event loop on linux to pump the
      // libuv event loop when `sourcesWatcher->loop == nullptr` when `start()`
      // is called. We use the runtime core libuv event loop in the `bridge` APIs
      sourcesWatcherSupportThread = new Thread([] () { gtk_main(); });
    #endif
    }

    int exitCode = 0;
    if (flagShouldRun) {
      run(targetPlatform, settings, paths, flagDebugMode, flagBuildHeadless || flagRunHeadless, argvForward, androidState);
    }

    exit(exitCode);
  });

  createSubcommand("run", runOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    String argvForward = "";
    bool flagRunHeadless = optionsWithoutValue.find("--headless") != optionsWithoutValue.end();
    bool flagTest = optionsWithoutValue.find("--test") != optionsWithoutValue.end() || optionsWithValue["--test"].size() > 0;
    String targetPlatform = optionsWithValue["--platform"];
    String testFile = optionsWithValue["--test"];
    bool isForIOS = false;
    bool isForAndroid = false;

    String tlsKeylogPath = optionsWithValue["--tls-keylog"];
    if (tlsKeylogPath.size() > 0) {
      env::set("ORO_TLS_KEYLOG", tlsKeylogPath);
    }

    if (flagTest && testFile.size() == 0) {
      logError("--test value is required." + helpHint("run"));
      exit(1);
    }

    if (testFile.size() > 0) {
      argvForward += " --test=" + testFile;
    }

    if (flagRunHeadless) {
      argvForward += " --headless";
    }

    if (optionsWithValue.count("--remote-debugging-port") > 0) {
      const auto port = trim(optionsWithValue["--remote-debugging-port"]);
      if (port.size() > 0) {
        argvForward += " --remote-debugging-port=" + port;
      }
    }

    if (targetPlatform.size() > 0) {
      if (targetPlatform == "ios" || targetPlatform == "ios-simulator") {
        isForIOS = true;
      } else if (targetPlatform == "android" || targetPlatform == "android-emulator") {
        isForAndroid = true;
      } else {
        logError("Unknown platform: " + targetPlatform + helpHint("run"));
        exit(1);
      }
    } else {
      targetPlatform = platform.os;
    }

    String devHost = "localhost";
    if (optionsWithValue.count("--host") > 0) {
      devHost = optionsWithValue["--host"];
    } else if (isForIOS || isForAndroid) {
      devHost = detectDevHostIP();
    }

    if (!devHost.starts_with("http")) {
      devHost = String("http://") + devHost;
    }

    settings.insert(std::make_pair("host", devHost));

    String devPort = "0";
    if (optionsWithValue.count("--port") > 0) {
      devPort = optionsWithValue["--port"];
    }
    settings.insert(std::make_pair("port", devPort));

    const bool debugEnv = (
      env::get("ORO_DEBUG").size() > 0 ||
      env::get("DEBUG").size() > 0
    );

    const bool verboseEnv = (
      env::get("ORO_VERBOSE").size() > 0 ||
      env::get("VERBOSE").size() > 0
    );

    Paths paths = getPaths(targetPlatform);

    auto devNull = ">" + String((!platform.win) ? "/dev/null" : "NUL") + (" 2>&1");
    String quote = !platform.win ? "'" : "\"";
    String slash = !platform.win ? "/" : "\\";

    auto androidPlatform = "android-37.0";
    AndroidCliState androidState;
    androidState.androidHome = getAndroidHome();
    androidState.verbose = flagVerboseMode || debugEnv || verboseEnv;
    androidState.devNull = devNull;
    androidState.targetPlatform = targetPlatform;
    androidState.platform = androidPlatform;
    androidState.appPath = paths.platformSpecificOutputPath / "app";
    androidState.quote = quote;
    androidState.slash = slash;

    run(targetPlatform, settings, paths, flagDebugMode, flagRunHeadless, argvForward, androidState);
  });

  // first flag indicating whether option is optional
  // second flag indicating whether option should be followed by a value
  CommandLineOptions setupOptions = {
    { { "--platform" }, true, true },
    { { "--yes", "-y" }, true, false },
    { { "--quiet", "-q" }, true, false },
    { { "--debug", "-D" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true },
  };

  createSubcommand("setup", setupOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    auto help = false;
    auto yes = optionsWithoutValue.find("--yes") != optionsWithoutValue.end();
    String yesArg;

    String targetPlatform = optionsWithValue["--platform"];
    auto targetAndroid = false;
    auto targetLinux = false;
    auto targetWindows = false;

    // Note that multiple --platforms aren't supported by createSubcommand()
    if (equal(targetPlatform, "android")) {
      targetAndroid = true;
    } else if (equal(targetPlatform, "windows")) {
      targetWindows = true;
    } else if (equal(targetPlatform, "linux")) {
      targetLinux = true;
    } else if (targetPlatform.size() > 0) {
      printHelp("setup");
      exit(1);
    }

    if (targetPlatform.size() == 0) {
      if (platform.win) {
        targetWindows = true;
      } else if (platform.linux) {
        targetLinux = true;
      }
    }

    if (!platform.win && targetWindows) {
      const String cliName(gCliDisplayName);
      logError("Windows build dependencies can only be installed on Windows. See '" + cliName + " setup --help'");
      exit(1);
    }

    if (!platform.linux && targetLinux) {
      const String cliName(gCliDisplayName);
      logError("Linux build dependencies can only be installed on Linux. See '" + cliName + " setup --help'");
      exit(1);
    }

    String argument;
    Path script;
    String scriptHost;

    if (platform.win) {
      scriptHost = "powershell.exe";
      script = prefixPath("bin") / "install.ps1";
      if (targetPlatform.size() > 0) {
        argument = "-fte:" + targetPlatform;
      } else {
        argument = "-fte:windows";
      }
      yesArg = yes ? "-yesdeps" : "";
    } else if (platform.linux || platform.mac) {
      scriptHost = "bash";
      script = prefixPath("bin") / "functions.sh";
      argument = "--fte " + targetPlatform;
    } else {
      argument = "--" + targetPlatform + "-fte";
      if (targetAndroid) {
        script = prefixPath("bin") / "android-functions.sh";
      }
      yesArg = yes ? "--yes-deps" : "";
    }


    if (!fs::exists(script)) {
      const String cliName(gCliDisplayName);
      logError("Install script not found: '" + script.string() + "'. Verify ORO_HOME and run '" + cliName + " --prefix' to locate the install path.");
      exit(1);
    }

    fs::current_path(prefixFile());

    if (targetPlatform.size() == 0) {
      if (platform.win) {
        targetPlatform = "windows";
      } else if (platform.linux) {
        targetPlatform = "linux";
      } else if (platform.mac) {
        targetPlatform = "darwin";
      }
    }

    if (optionsWithoutValue.find("--quiet") != optionsWithoutValue.end() || equal(rc["build_quiet"], "true")) {
      flagQuietMode = true;
    }

    logInfo("Running setup for platform '" + targetPlatform + "' in ORO_HOME (" + prefixFile() + ")");
    String command = scriptHost + " \"" + script.string() + "\" " + argument + " " + yesArg;
    auto r = std::system(command.c_str());

    exit(r);
  });

  // Configuration inspection helper.
  CommandLineOptions configOptions = {
    { { "--config" }, true, true },
    { { "--key" }, true, true },
    { { "--describe" }, true, true },
    { { "--format", "-f" }, true, true },
    { { "--json" }, true, false },
    { { "--list" }, true, false },
    { { "--strict" }, true, false },
    { { "--log-file" }, true, true }
  };

  createSubcommand("config", configOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    const auto keyName = optionsWithValue["--key"];
    const auto describeName = optionsWithValue["--describe"];
    String formatName = resolveCommandOutputFormat(
      "config",
      optionsWithValue,
      optionsWithoutValue,
      { "toml", "ini", "json" },
      ""
    );
    const bool listAll = optionsWithoutValue.find("--list") != optionsWithoutValue.end();
    const bool strictMode = optionsWithoutValue.find("--strict") != optionsWithoutValue.end();

    Map<> effectiveConfig;
    if (settingsSource.size() > 0) {
      try {
        effectiveConfig = oro::runtime::config::parseUserConfigSource(
          settingsSource,
          gEmbeddedConfigFormat
        );
      } catch (...) {
        // Leave effectiveConfig empty on parse error.
      }
    }

    if (formatName.size() > 0) {
      if (keyName.size() > 0 || describeName.size() > 0 || listAll) {
        logError(
          "config: '--format' and '--json' are only available for full configuration dumps, "
          "not with '--key', '--describe', or '--list'" + helpHint("config")
        );
        exit(1);
      }

      if (effectiveConfig.empty()) {
        logError("no active configuration is loaded; pass '--config=<path>' in a project directory" + helpHint("config"));
        exit(1);
      }

      // Filter out internal metadata keys (oroc_*) that are not part of
      // the user-facing configuration surface.
      Map<> filtered;
      for (const auto& tuple : effectiveConfig) {
        const auto& k = tuple.first;
        if (k.rfind("oroc_", 0) == 0) {
          continue;
        }
        filtered[k] = tuple.second;
      }

      // Shared resolver to derive a section name + key name from a flattened
      // config key. Prefers registry metadata, then falls back to heuristics.
      auto resolveSectionAndKey = [&](const String& flattenedKey, String& sectionName, String& keyName) {
        sectionName.clear();
        keyName.clear();

        if (const auto* meta = oro::runtime::config::findConfigKeyInfo(flattenedKey)) {
          if (meta->path != nullptr && String(meta->path).size() > 0) {
            const String path(meta->path);
            const auto segments = split(path, '.');
            if (segments.empty()) {
              keyName = path;
              return;
            }
            if (segments.size() == 1) {
              keyName = segments[0];
              return;
            }

            Vector<String> prefixSegments(segments.begin(), segments.end() - 1);
            sectionName = join(prefixSegments, ".");
            keyName = segments.back();
            return;
          }
        }

        // Dynamic prefix: attempt to resolve using the longest flattened
        // prefix that exists in the registry (for example,
        // "webview_navigator_mounts" -> "webview.navigator.mounts").
        bool resolved = false;
        size_t pos = flattenedKey.rfind('_');
        while (pos != String::npos) {
          const String prefixKey = flattenedKey.substr(0, pos);
          if (const auto* prefixMeta = oro::runtime::config::findConfigKeyInfo(prefixKey)) {
            if (prefixMeta->path != nullptr && String(prefixMeta->path).size() > 0) {
              sectionName = String(prefixMeta->path);
              keyName = flattenedKey.substr(pos + 1);
              resolved = true;
              break;
            }
          }
          if (pos == 0) break;
          pos = flattenedKey.rfind('_', pos - 1);
        }

        // Heuristic fallback: treat the substring before the first '_'
        // as the table/section name and the remainder as the key (for example,
        // "permissions_allow_service_worker" -> [permissions] allow_service_worker).
        if (!resolved) {
          const auto firstUnderscore = flattenedKey.find('_');
          if (firstUnderscore != String::npos) {
            sectionName = flattenedKey.substr(0, firstUnderscore);
            keyName = flattenedKey.substr(firstUnderscore + 1);
          } else {
            keyName = flattenedKey;
          }
        }
      };

      if (formatName == "ini") {
        // INI-style: reconstruct sections/keys using the same section/key
        // resolution as TOML output.
        INI::Document document;

        for (const auto& tuple : filtered) {
          const auto& flattenedKey = tuple.first;
          const auto& value = tuple.second;

          String sectionName;
          String keyName;
          resolveSectionAndKey(flattenedKey, sectionName, keyName);

          // Section "" is the root (no header).
          auto& section = document.section(sectionName);
          section.set(keyName, value, false, false);
        }

        std::cout << INI::serialize(document) << std::endl;
        exit(0);
      }

      if (formatName == "toml") {
        // TOML-style: reconstruct section tables using canonical paths
        // from the static registry when available.
        struct TableEntry {
          Map<String, String> keys;
        };

        Map<String, TableEntry> tables;
        Vector<String> tableOrder;

        auto ensureTable = [&](const String& name) -> TableEntry& {
          auto it = tables.find(name);
          if (it != tables.end()) {
            return it->second;
          }
          TableEntry entry;
          tables.insert_or_assign(name, entry);
          tableOrder.push_back(name);
          return tables[name];
        };

        for (const auto& tuple : filtered) {
          const auto& flattenedKey = tuple.first;
          const auto& value = tuple.second;

          String sectionName;
          String keyName;
          resolveSectionAndKey(flattenedKey, sectionName, keyName);

          TableEntry& table = ensureTable(sectionName);
          table.keys[keyName] = value;
        }

        // Root (no-section) keys first, then named tables.
        auto writeTable = [&](const String& name, const TableEntry& table) {
          if (name.size() > 0) {
            std::cout << "[" << name << "]\n";
          }
          for (const auto& kv : table.keys) {
            const auto& key = kv.first;
            const auto& value = kv.second;
            String printedKey = key;
            if (!isValidTomlBareKey(printedKey)) {
              printedKey = quoteTomlString(printedKey);
            }
            std::cout << printedKey << " = " << quoteTomlString(value) << "\n";
          }
          std::cout << "\n";
        };

        // Root table is represented by empty section name.
        if (tables.contains("")) {
          writeTable("", tables[""]);
        }
        for (const auto& section : tableOrder) {
          if (section.size() == 0) {
            continue;
          }
          writeTable(section, tables[section]);
        }

        exit(0);
      }

      // JSON-style: nested object keyed by TOML-style paths where available.
      auto jsonEscape = [](const String& in) {
        String out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
          switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
              if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
              } else {
                out.push_back(static_cast<char>(c));
              }
          }
        }
        return out;
      };

      struct JsonNode {
        Map<String, JsonNode> children;
        bool hasValue = false;
        String value;
      };

      JsonNode root;

      auto insertPath = [&](JsonNode& node, const Vector<String>& segments, const String& value) {
        if (segments.empty()) {
          node.hasValue = true;
          node.value = value;
          return;
        }

        JsonNode* current = &node;
        for (size_t i = 0; i + 1 < segments.size(); ++i) {
          const auto& segment = segments[i];
          auto it = current->children.find(segment);
          if (it == current->children.end()) {
            JsonNode child;
            current->children.insert_or_assign(segment, child);
            current = &current->children.at(segment);
          } else {
            current = &it->second;
          }
        }

        const auto& leafKey = segments.back();
        auto it = current->children.find(leafKey);
        if (it == current->children.end()) {
          JsonNode leaf;
          leaf.hasValue = true;
          leaf.value = value;
          current->children.insert_or_assign(leafKey, leaf);
        } else {
          it->second.hasValue = true;
          it->second.value = value;
        }
      };

      // Build a nested tree using ConfigKeyInfo.path when available.
      for (const auto& tuple : filtered) {
        const auto& flattenedKey = tuple.first;
        const auto& value = tuple.second;

        const auto* meta = oro::runtime::config::findConfigKeyInfo(flattenedKey);
        String path = meta && meta->path != nullptr && String(meta->path).size() > 0
          ? String(meta->path)
          : flattenedKey;

        const auto segments = split(path, '.');
        if (segments.empty()) {
          insertPath(root, {}, value);
        } else {
          insertPath(root, segments, value);
        }
      }

      std::function<void(const JsonNode&, int)> writeJsonNode;
      writeJsonNode = [&](const JsonNode& node, int indent) {
        const String indentStr(indent, ' ');
        const String childIndentStr(indent + 2, ' ');

        bool first = true;
        std::cout << "{";
        if (!node.children.empty()) {
          std::cout << "\n";
        }

        for (auto it = node.children.begin(); it != node.children.end(); ++it) {
          if (!first) {
            std::cout << ",\n";
          }
          first = false;

          const auto& key = it->first;
          const auto& child = it->second;

          std::cout << childIndentStr << "\"" << jsonEscape(key) << "\": ";
          if (!child.children.empty()) {
            writeJsonNode(child, indent + 2);
          } else {
            std::cout << "\"" << jsonEscape(child.value) << "\"";
          }
        }

        if (!node.children.empty()) {
          std::cout << "\n" << indentStr;
        }
        std::cout << "}";
      };

      writeJsonNode(root, 0);
      std::cout << std::endl;
      exit(0);
    }

    Map<> fileConfig;
    if (!gActiveConfigPath.empty()) {
      auto contents = readFile(gActiveConfigPath);
      if (contents.size() > 0) {
        auto format = detectConfigFormatForPath(gActiveConfigPath);
        try {
          fileConfig = oro::runtime::config::parseUserConfigSource(contents, format);
        } catch (...) {
          // Ignore file parse errors here; the main subcommand handling
          // would already have surfaced them.
        }
      }
    }

    auto lookupMeta = [&](const String& name) -> const ConfigKeyInfo* {
      if (name.size() == 0) {
        return nullptr;
      }
      const auto* meta = oro::runtime::config::findConfigKeyInfo(name);
      if (meta != nullptr) {
        return meta;
      }
      String fallback = name;
      for (auto& ch : fallback) {
        if (ch == '.') {
          ch = '_';
        }
      }
      if (fallback != name) {
        return oro::runtime::config::findConfigKeyInfo(fallback);
      }
      return nullptr;
    };

    auto printEntry = [&](const ConfigKeyInfo& info) {
      const String key(info.key ? info.key : "");
      const String path(info.path ? info.path : key);

      String value;
      const bool hasEffective = key.size() > 0 &&
        effectiveConfig.contains(key) &&
        effectiveConfig.at(key).size() > 0;
      const bool inFile = key.size() > 0 && fileConfig.contains(key);
      const bool inRc = key.size() > 0 && rc.contains(key);

      if (hasEffective) {
        value = effectiveConfig.at(key);
      } else if (info.defaultValue != nullptr && String(info.defaultValue).size() > 0) {
        value = info.defaultValue;
      } else {
        value = "";
      }

      std::cout << path << " = " << value;
      if (inFile) {
        std::cout << "  (source: config file)";
      } else if (inRc) {
        std::cout << "  (source: rc file)";
      } else if (hasEffective) {
        std::cout << "  (source: derived)";
      } else {
        std::cout << "  (source: default)";
      }
      if (info.deprecated) {
        std::cout << "  [deprecated]";
      }
      std::cout << std::endl;
    };

    if (listAll || (keyName.size() == 0 && describeName.size() == 0)) {
      const auto& registry = oro::runtime::config::getConfigRegistry();
      for (const auto& info : registry) {
        printEntry(info);
      }

      // Surface any additional keys present in the effective config that
      // are not part of the static registry.
      for (const auto& tuple : effectiveConfig) {
        const auto& k = tuple.first;
        if (oro::runtime::config::findConfigKeyInfo(k) != nullptr) {
          continue;
        }
        // Skip CLI-internal metadata keys that are not part of user config.
        if (k.rfind("oroc_", 0) == 0) {
          continue;
        }
        std::cout << k << " = " << tuple.second << "  (source: config, undocumented)" << std::endl;
      }
      exit(0);
    }

    const String query = describeName.size() > 0 ? describeName : keyName;
    const bool hasWildcard =
      query.find('*') != String::npos ||
      query.find('?') != String::npos;

    // Pattern query: support glob-style matching over config paths/keys.
    // Examples:
    //   oroc config "meta.a*"
    //   oroc config "*.llm"
    if (
      formatName.size() == 0 &&
      !listAll &&
      describeName.size() == 0 &&
      keyName.size() > 0 &&
      hasWildcard
    ) {
      auto buildGlobRegex = [](const String& pattern) -> std::regex {
        String re;
        re.reserve(pattern.size() * 2);
        for (const auto ch : pattern) {
          switch (ch) {
            case '*':
              re.append(".*");
              break;
            case '?':
              re.push_back('.');
              break;
            case '.': case '+': case '(': case ')':
            case '[': case ']': case '{': case '}':
            case '^': case '$': case '|': case '\\':
              re.push_back('\\');
              re.push_back(ch);
              break;
            default:
              re.push_back(ch);
              break;
          }
        }
        return std::regex(re, std::regex::ECMAScript);
      };

      const auto pattern = buildGlobRegex(query);
      const auto& registry = oro::runtime::config::getConfigRegistry();
      bool anyMatched = false;

      for (const auto& info : registry) {
        const String path(info.path ? info.path : "");
        const String key(info.key ? info.key : "");

        bool match = false;
        if (path.size() > 0 && std::regex_search(path, pattern)) {
          match = true;
        } else if (key.size() > 0 && std::regex_search(key, pattern)) {
          match = true;
        }

        if (!match) {
          continue;
        }

        anyMatched = true;
        printEntry(info);
      }

      // Also surface undocumented keys from the effective config whose
      // flattened key matches the pattern.
      for (const auto& tuple : effectiveConfig) {
        const auto& k = tuple.first;
        if (oro::runtime::config::findConfigKeyInfo(k) != nullptr) {
          continue;
        }
        if (k.rfind("oroc_", 0) == 0) {
          continue;
        }
        if (std::regex_search(k, pattern)) {
          anyMatched = true;
          std::cout << k << " = " << tuple.second << "  (source: config, undocumented)" << std::endl;
        }
      }

      if (!anyMatched && strictMode) {
        logError("no configuration keys match query '" + query + "'");
        exit(1);
      }

      exit(0);
    }

    const auto* meta = lookupMeta(query);

    // Group listing: when invoked as `oroc config meta` (or any other
    // section-like prefix) without an exact key match, list all known
    // configuration keys whose path begins with that prefix.
    if (
      formatName.size() == 0 &&
      !listAll &&
      describeName.size() == 0 &&
      keyName.size() > 0 &&
      meta == nullptr &&
      !hasWildcard
    ) {
      const auto& registry = oro::runtime::config::getConfigRegistry();
      Vector<const ConfigKeyInfo*> matched;

      auto hasPrefixMatch = [&](const ConfigKeyInfo& info) -> bool {
        const String path(info.path ? info.path : "");
        const String key(info.key ? info.key : "");

        if (path.size() > 0) {
          if (path == query) return true;
          if (path.starts_with(query + ".")) return true;
        }

        if (key.size() > 0) {
          if (key.starts_with(query + "_")) return true;
        }

        return false;
      };

      for (const auto& info : registry) {
        if (hasPrefixMatch(info)) {
          matched.push_back(&info);
        }
      }

      if (!matched.empty()) {
        for (const auto* infoPtr : matched) {
          printEntry(*infoPtr);
        }

        // Additionally surface any extra (undocumented) keys from the
        // effective config that share the same flattened prefix.
        for (const auto& tuple : effectiveConfig) {
          const auto& k = tuple.first;
          if (oro::runtime::config::findConfigKeyInfo(k) != nullptr) {
            continue;
          }
          if (k.rfind("oroc_", 0) == 0) {
            continue;
          }
          if (k.starts_with(query + "_")) {
            std::cout << k << " = " << tuple.second << "  (source: config, undocumented)" << std::endl;
          }
        }

        exit(0);
      }
    }

    if (describeName.size() > 0) {
      if (meta == nullptr) {
        logError("configuration key '" + query + "' is not recognized in the static registry");
        if (strictMode) {
          exit(1);
        } else {
          std::cout << std::endl;
          exit(0);
        }
      }

      const String key(meta->key ? meta->key : "");
      const String path(meta->path ? meta->path : key);
      const String desc(meta->description ? meta->description : "");
      String value;
      const bool hasEffective = key.size() > 0 &&
        effectiveConfig.contains(key) &&
        effectiveConfig.at(key).size() > 0;
      const bool inFile = key.size() > 0 && fileConfig.contains(key);
      const bool inRc = key.size() > 0 && rc.contains(key);

      if (hasEffective) {
        value = effectiveConfig.at(key);
      } else if (meta->defaultValue != nullptr && String(meta->defaultValue).size() > 0) {
        value = meta->defaultValue;
      }

      std::cout << path << std::endl;
      if (desc.size() > 0) {
        std::cout << "  " << desc << std::endl;
      }
      if (meta->defaultValue != nullptr && String(meta->defaultValue).size() > 0) {
        std::cout << "  default: " << meta->defaultValue << std::endl;
      }
      if (value.size() > 0) {
        std::cout << "  current: " << value;
        if (inFile) {
          std::cout << " (from config file)";
        } else if (inRc) {
          std::cout << " (from rc file)";
        } else if (hasEffective) {
          std::cout << " (derived)";
        } else {
          std::cout << " (default)";
        }
        std::cout << std::endl;
      }

      std::cout << "  type: ";
      switch (meta->type) {
        case ConfigValueType::Bool:
          std::cout << "bool";
          break;
        case ConfigValueType::Int:
          std::cout << "int";
          break;
        case ConfigValueType::Float:
          std::cout << "float";
          break;
        case ConfigValueType::String:
        default:
          std::cout << "string";
          break;
      }
      std::cout << std::endl;

      if (meta->deprecated) {
        std::cout << "  note: deprecated key" << std::endl;
      }

      exit(0);
    }

    // Value lookup: print only the value for scripting.
    String keyLookup = keyName;
    if (meta != nullptr && meta->key != nullptr && String(meta->key).size() > 0) {
      keyLookup = meta->key;
    }

    auto resolveKey = [&](const String& raw) -> String {
      if (effectiveConfig.contains(raw)) {
        return raw;
      }
      String flattened = raw;
      bool changed = false;
      for (auto& ch : flattened) {
        if (ch == '.') {
          ch = '_';
          changed = true;
        }
      }
      if (changed && effectiveConfig.contains(flattened)) {
        return flattened;
      }
      return raw;
    };

    const String effectiveKey = resolveKey(keyLookup);
    String value;
    if (effectiveConfig.contains(effectiveKey) && effectiveConfig.at(effectiveKey).size() > 0) {
      value = effectiveConfig.at(effectiveKey);
    } else if (meta != nullptr && meta->defaultValue != nullptr && String(meta->defaultValue).size() > 0) {
      value = meta->defaultValue;
    } else if (strictMode) {
      // In strict mode, missing keys or values are treated as errors.
      logError("configuration key '" + keyName + "' has no current value or registry default");
      exit(1);
    }

    std::cout << value << std::endl;
    exit(0);
  });

  CommandLineOptions envOptions = {
    { { "--format", "-f" }, true, true },
    { { "--json" }, true, false },
    { { "--verbose", "-V" }, true, false },
    { { "--log-file" }, true, true }
  };

  createSubcommand("env", envOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    auto envs = Map<>();
    const auto outputFormat = resolveCommandOutputFormat(
      "env",
      optionsWithValue,
      optionsWithoutValue,
      { "text", "json" },
      "text"
    );

    envs["DEBUG"] = env::get("DEBUG");
    envs["VERBOSE"] = env::get("VERBOSE");
    envs["ORO_DEBUG"] = env::get("ORO_DEBUG");
    envs["ORO_VERBOSE"] = env::get("ORO_VERBOSE");

    // runtime source-bootstrap target exclusions
    envs["NO_ANDROID"] = env::get("NO_ANDROID");
    envs["NO_IOS"] = env::get("NO_IOS");

    // runtime variables
    auto runtimeHome = getHomeHome(false);
    envs["ORO_HOME"] = runtimeHome;
    envs["ORO_HOME_API"] = env::get("ORO_HOME_API");

    // platform OS variables
    envs["PWD"] = env::get("PWD");
    envs["HOME"] = env::get("HOME");
    envs["LANG"] = env::get("LANG");
    envs["USER"] = env::get("USER");
    envs["SHELL"] = env::get("SHELL");
    envs["HOMEPATH"] = env::get("HOMEPATH");
    envs["LOCALAPPDATA"] = env::get("LOCALAPPDATA");
    envs["XDG_DATA_HOME"] = env::get("XDG_DATA_HOME");

    // compiler variables
    envs["CC"] = env::get("CC");
    envs["CXX"] = env::get("CXX");
    envs["CPP"] = env::get("CPP");
    envs["PREFIX"] = env::get("PREFIX");
    envs["CXXFLAGS"] = env::get("CXXFLAGS");

    // locale variables
    envs["LC_ALL"] = env::get("LC_ALL");
    envs["LC_CTYPE"] = env::get("LC_CTYPE");
    envs["LC_TERMINAL"] = env::get("LC_TERMINAL");
    envs["LC_TERMINAL_VERSION"] = env::get("LC_TERMINAL_VERSION");

    // platform dependency variables
    envs["JAVA_HOME"] = env::get("JAVA_HOME");
    envs["GRADLE_HOME"] = env::get("GRADLE_HOME");
    envs["ANDROID_HOME"] = getAndroidHome();
    envs["ANDROID_SUPPORTED_ABIS"] = env::get("ANDROID_SUPPORTED_ABIS");

    // apple specific platform variables
    envs["APPLE_ID"] = env::get("APPLE_ID");
    envs["APPLE_ID_PASSWORD"] = env::get("APPLE_ID_PASSWORD");

    // windows specific platform variables
    envs["SIGNTOOL"] = env::get("SIGNTOOL");
    envs["WIN_DEBUG_LIBS"] = env::get("WIN_DEBUG_LIBS");
    envs["CSC_KEY_PASSWORD"] = env::get("CSC_KEY_PASSWORD");

    // Oro CLI variables
    envs["ORO_CI"] = env::get("ORO_CI");
    envs["ORO_RC"] = env::get("ORO_RC");
    envs["ORO_RC_FILENAME"] = env::get("ORO_RC_FILENAME");
    envs["ORO_ENV_FILENAME"] = env::get("ORO_ENV_FILENAME");
    envs["ORO_UPDATE_MANIFEST_FILENAME"] = env::get("ORO_UPDATE_MANIFEST_FILENAME");
    envs["ORO_ALLOW_EXEC"] = env::get("ORO_ALLOW_EXEC");
    envs["ORO_ENABLE_SANITIZERS"] = env::get("ORO_ENABLE_SANITIZERS");
    envs["DETECTED_DEV_HOST"] = detectDevHostIP();

    // TLS-related variables (experimental / documented in TLS guides)
    envs["ORO_ENABLE_TLS"] = env::get("ORO_ENABLE_TLS");
    envs["ORO_TLS_PROVIDER"] = env::get("ORO_TLS_PROVIDER");
    envs["ORO_TLS_CFLAGS"] = env::get("ORO_TLS_CFLAGS");
    envs["ORO_TLS_LDFLAGS"] = env::get("ORO_TLS_LDFLAGS");
    envs["ORO_TLS_KEYLOG"] = env::get("ORO_TLS_KEYLOG");

    if (envs["ORO_HOME_API"].size() == 0) {
      auto apiPath = prefixPath("api").string();
      if (envs["ORO_HOME_API"].size() == 0) {
        envs["ORO_HOME_API"] = apiPath;
      }
    }

    // gather all environment variables relevant to project in the config file
    // file, which may overload the variables above
    for (const auto& entry : settings) {
      if (entry.first.starts_with("env_")) {
        auto key = entry.first.substr(4, entry.first.size() - 4);
        auto value = trim(entry.second);

        if (value.size() == 0) {
          value = trim(env::get(key));
        }

        envs[key] = value;
      } else if (entry.first == "env") {
        for (const auto& key : parseStringList(entry.second)) {
          auto value = trim(env::get(key));
          envs[key] = value;
        }
      }
    }

    // Gather all environment variables relevant to local `.ororc` files
    // file, which may overload the variables above
    for (const auto& entry : rc) {
      if (entry.first.starts_with("env_")) {
        auto key = entry.first.substr(4, entry.first.size() - 4);
        auto value = trim(entry.second);

        if (value.size() == 0) {
          value = trim(env::get(key));
        }

        envs[key] = value;
      } else if (entry.first == "env") {
        for (const auto& key : parseStringList(entry.second)) {
          auto value = trim(env::get(key));
          envs[key] = value;
        }
      }
    }

    std::map<String, String> visibleEnvs;

    // print all environment variables that have values
    for (const auto& entry : envs) {
      auto& key = entry.first;
      auto value = trim(entry.second);

      if (value.size() == 0) {
        value = trim(env::get(key));
      }

      if (value.size() > 0) {
        visibleEnvs[key] = value;
      }
    }

    if (equal(outputFormat, "json")) {
      JSON::Object object(visibleEnvs);
      std::cout << JSON::stringify(object) << std::endl;
    } else {
      for (const auto& entry : visibleEnvs) {
        std::cout << entry.first << "=" << entry.second << std::endl;
      }
    }

    exit(0);
  });

  CommandLineOptions mcpOptions = {
    { { "--stdio" }, true, false },
    { { "--http" }, true, false },
    { { "--host" }, true, true },
    { { "--port" }, true, true },
    { { "--endpoint" }, true, true },
    { { "--token" }, true, true },
    { { "--no-auth" }, true, false },
    { { "--workspace" }, true, true },
    { { "--config" }, true, true },
    { { "--allow-read-outside-workspace" }, true, false },
    { { "--read-workspace-only" }, true, false },
    { { "--replace-sse-stream" }, true, false }
  };

  createSubcommand("mcp", mcpOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    const bool hasHttp = optionsWithoutValue.find("--http") != optionsWithoutValue.end();
    const bool hasStdio = optionsWithoutValue.find("--stdio") != optionsWithoutValue.end();
    if (hasHttp && hasStdio) {
      logError("mcp: --http and --stdio are mutually exclusive");
      exit(1);
    }

    const bool useHttp = hasHttp;
    const bool useStdio = !useHttp;

    Path workspaceRoot = targetPath;
    if (optionsWithValue.count("--workspace") > 0 && optionsWithValue["--workspace"].size() > 0) {
      workspaceRoot = fs::absolute(optionsWithValue["--workspace"]).lexically_normal();
    }

    Path configPath;
    if (optionsWithValue.count("--config") > 0 && optionsWithValue["--config"].size() > 0) {
      configPath = Path(optionsWithValue["--config"]).lexically_normal();
    }
    if (!configPath.empty()) {
      if (configPath.is_absolute() || configPath.has_root_path() || configPath.has_root_name() || configPath.has_root_directory()) {
        logError("mcp: --config must be a path relative to the workspace root");
        exit(1);
      }
      for (const auto& part : configPath) {
        if (part == "..") {
          logError("mcp: --config must not contain '..'");
          exit(1);
        }
      }

      const auto configFilenameLower = toLowerCase(configPath.filename().string());
      if (configFilenameLower != "oro.toml" && configFilenameLower != "oro.ini") {
        logError("mcp: --config must point to an oro.toml or oro.ini file relative to the workspace root");
        exit(1);
      }
    }

    // Keep MCP side-effects scoped to the workspace by default.
    if (env::get("ORO_HOME").size() == 0) {
      const auto mcpHome = (workspaceRoot / ".oro_home").lexically_normal();
      env::set("ORO_HOME", mcpHome.string());
    }

    oro::cli::mcp::Options mcpRunOptions;
    mcpRunOptions.workspaceRoot = workspaceRoot;
    mcpRunOptions.configPath = configPath;
    mcpRunOptions.cliExecutable = cliInvocationPath;
    mcpRunOptions.cliDisplayName = gCliDisplayName;
    mcpRunOptions.useHttp = useHttp;

    const bool allowReadOutsideWorkspace =
      optionsWithoutValue.find("--allow-read-outside-workspace") != optionsWithoutValue.end();
    const bool readWorkspaceOnly =
      optionsWithoutValue.find("--read-workspace-only") != optionsWithoutValue.end();
    if (allowReadOutsideWorkspace && readWorkspaceOnly) {
      logError("mcp: --allow-read-outside-workspace and --read-workspace-only are mutually exclusive");
      exit(1);
    }
    mcpRunOptions.allowReadOutsideWorkspace = allowReadOutsideWorkspace;

    mcpRunOptions.replaceSseStreamOnReconnect =
      optionsWithoutValue.find("--replace-sse-stream") != optionsWithoutValue.end();

    const bool hostExplicit = optionsWithValue.count("--host") > 0 && optionsWithValue["--host"].size() > 0;
    const bool endpointExplicit = optionsWithValue.count("--endpoint") > 0 && optionsWithValue["--endpoint"].size() > 0;
    const bool portExplicit = optionsWithValue.count("--port") > 0 && optionsWithValue["--port"].size() > 0;
    const bool tokenExplicit = optionsWithValue.count("--token") > 0 && optionsWithValue["--token"].size() > 0;
    const bool noAuth = optionsWithoutValue.find("--no-auth") != optionsWithoutValue.end();

    if (useHttp) {
      const fs::path configRel = configPath.empty() ? fs::path("oro.toml") : fs::path(configPath);
      const auto configRelNorm = configRel.lexically_normal();
      if (!configRelNorm.empty() && configRelNorm.is_relative()) {
        std::error_code ec;
        const auto workspaceAbs = fs::weakly_canonical(workspaceRoot, ec);
        const auto configAbs = ec
          ? fs::path()
          : fs::weakly_canonical(workspaceAbs / configRelNorm, ec);
        const auto configRelative = ec
          ? fs::path()
          : fs::relative(configAbs, workspaceAbs, ec);
        bool configInsideWorkspace = !ec && !configRelative.is_absolute();
        for (const auto& part : configRelative) {
          if (part == "..") {
            configInsideWorkspace = false;
            break;
          }
        }

        if (configInsideWorkspace && fs::is_regular_file(configAbs, ec) && !ec) {
          try {
            const auto doc = runtime::TOML::parseFile(configAbs);
            if (doc.isTable()) {
              const auto& mcpTable = doc["mcp"];
              if (mcpTable.isTable()) {
                const auto& hostValue = mcpTable["host"];
                if (!hostExplicit && hostValue.is(runtime::TOML::Type::String) && hostValue.asString().size() > 0) {
                  mcpRunOptions.host = hostValue.asString();
                }

                const auto& endpointValue = mcpTable["endpoint"];
                if (!endpointExplicit && endpointValue.is(runtime::TOML::Type::String) && endpointValue.asString().size() > 0) {
                  mcpRunOptions.endpoint = endpointValue.asString();
                }

                const auto& portValue = mcpTable["port"];
                if (!portExplicit && portValue.is(runtime::TOML::Type::Integer)) {
                  const auto port = portValue.asInteger();
                  if (port > 0 && port <= 65535) {
                    mcpRunOptions.port = static_cast<int>(port);
                  }
                }

                const auto& tokenValue = mcpTable["token"];
                if (!tokenExplicit && tokenValue.is(runtime::TOML::Type::String) && tokenValue.asString().size() > 0) {
                  mcpRunOptions.token = tokenValue.asString();
                }
              }
            }
          } catch (...) {}
        }
      }
    }

    if (hostExplicit) {
      mcpRunOptions.host = optionsWithValue["--host"];
    }
    if (endpointExplicit) {
      mcpRunOptions.endpoint = optionsWithValue["--endpoint"];
    }

    if (portExplicit) {
      try {
        size_t consumed = 0;
        const auto port = std::stoi(optionsWithValue["--port"], &consumed);
        if (consumed != optionsWithValue["--port"].size() || port < 0 || port > 65535) {
          throw std::out_of_range("port");
        }
        mcpRunOptions.port = port;
      } catch (...) {
        logError("mcp: --port must be an integer from 0 through 65535");
        exit(1);
      }
    }

    if (tokenExplicit) {
      mcpRunOptions.token = optionsWithValue["--token"];
    }

    if (useHttp) {
      const String host = mcpRunOptions.host.size() > 0 ? mcpRunOptions.host : "127.0.0.1";
      const String normalizedHost = toLowerCase(host);
      const bool loopbackHost = normalizedHost == "localhost" ||
        normalizedHost == "localhost." ||
        normalizedHost.rfind("127.", 0) == 0 ||
        normalizedHost == "::1" ||
        normalizedHost == "[::1]";

      if (noAuth && !loopbackHost) {
        logError("mcp: --no-auth is only allowed with loopback hosts (127.0.0.1, localhost, ::1)");
        exit(1);
      }

      const bool tokenRequested = tokenExplicit || (mcpRunOptions.token.size() > 0);

      if (noAuth) {
        mcpRunOptions.token = "";
      } else if (!tokenRequested || mcpRunOptions.token.size() == 0) {
#if ORO_CLI_HAS_SODIUM
        if (sodium_init() < 0) {
          logError("mcp: failed to initialize secure token generation");
          exit(1);
        }
        std::array<unsigned char, 32> tokenBytes {};
        std::array<char, 65> tokenHex {};
        randombytes_buf(tokenBytes.data(), tokenBytes.size());
        sodium_bin2hex(
          tokenHex.data(),
          tokenHex.size(),
          tokenBytes.data(),
          tokenBytes.size()
        );
        mcpRunOptions.token = tokenHex.data();
#else
        logError("mcp: automatic token generation requires libsodium; pass --token or use --no-auth on loopback");
        exit(1);
#endif
      }
    }

    if (useStdio) {
      gMcpStdioMode = true;
      flagQuietMode = true;
      flagJsonMode = false;
      gLogJson = false;
      gLogFilePath = "";
      env::set("ORO_LOG_JSON", "");
      env::set("ORO_LOG_FILE", "");
      gLogLevel = CliLogLevel::WARN;
    }

    const auto code = oro::cli::mcp::run(mcpRunOptions);
    exit(code);
  });

  // Update tooling: key generation, signing, and verification for update manifests.
  CommandLineOptions updateInitOptions = {
    { { "--manifest-name" }, true, true },
    { { "--config" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-init", updateInitOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    String manifestName = optionsWithValue["--manifest-name"];
    if (manifestName.size() == 0) {
      manifestName = env::get("ORO_UPDATE_MANIFEST_FILENAME");
    }
    if (manifestName.size() == 0) {
      manifestName = DEFAULT_UPDATE_MANIFEST_FILENAME;
    }

    fs::path manifestPath = targetPath / manifestName;
    std::error_code ec;
    if (fs::exists(manifestPath, ec) && !ec) {
      logError("update-init: manifest file already exists at " + manifestPath.string());
      exit(1);
    }

    JSON::Array channels;
    String defaultChannel = settings["update_channel"];
    if (defaultChannel.size() == 0) {
      defaultChannel = "stable";
    }
    channels.push(defaultChannel);

    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc {};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &timeT);
#else
    gmtime_r(&timeT, &tmUtc);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc) == 0) {
      buf[0] = '\0';
    }

    String appId = settings["meta_bundle_identifier"];
    if (appId.size() == 0) {
      appId = "com.example.app";
    }

    String version = settings["meta_version"].size() > 0 ? settings["meta_version"] : "1.0.0";

    JSON::Array updates;
    JSON::Object::Entries updateEntry {
      { "id", defaultChannel + "-" + version },
      { "version", version },
      { "channel", defaultChannel },
      { "critical", false },
      { "targets", JSON::Array() }
    };
    updates.push(updateEntry);

    JSON::Object manifest = JSON::Object::Entries {
      { "schemaVersion", 1 },
      { "appId", appId },
      { "generatedAt", String(buf) },
      { "channels", channels },
      { "updates", updates }
    };

    writeFile(manifestPath, prettyJson(manifest.str()) + "\n");
    logInfo("wrote update manifest template to " + manifestPath.string());
    exit(0);
  });

  CommandLineOptions updateServerOptions = {
    { { "--root" }, true, true },
    { { "--host" }, true, true },
    { { "--port" }, true, true },
    { { "--manifest-name" }, true, true },
    { { "--tcp" }, true, false },
    { { "--udp" }, true, false },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-server", updateServerOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    String rootPathStr = optionsWithValue["--root"];
    if (rootPathStr.size() == 0) {
      rootPathStr = fs::current_path().string();
    }

    fs::path rootPath = fs::absolute(rootPathStr);
    std::error_code rootEc;
    if (!fs::exists(rootPath, rootEc) || !fs::is_directory(rootPath, rootEc)) {
      logError("update-server: --root must point to an existing directory: " + rootPath.string());
      exit(1);
    }

    // Resolve manifest filename used under each appId tree.
    String manifestName = optionsWithValue["--manifest-name"];
    if (manifestName.size() == 0) {
      manifestName = env::get("ORO_UPDATE_MANIFEST_FILENAME");
    }
    if (manifestName.size() == 0) {
      manifestName = DEFAULT_UPDATE_MANIFEST_FILENAME;
    }
    // Store global update server configuration used by HTTP/TCP/UDP bindings.
    gUpdateServerRootPath = rootPath;
    gUpdateServerManifestName = manifestName;

    const String host = optionsWithValue["--host"].size() > 0 ? optionsWithValue["--host"] : String("0.0.0.0");
    int port = 0;
    if (optionsWithValue["--port"].size() > 0) {
      try {
        port = std::stoi(optionsWithValue["--port"]);
      } catch (...) {
        logError("update-server: --port must be a valid integer");
        exit(1);
      }
      if (port <= 0 || port > 65535) {
        logError("update-server: --port must be between 1 and 65535");
        exit(1);
      }
    } else {
      port = 8080;
    }

    const bool tcpMode = optionsWithoutValue.find("--tcp") != optionsWithoutValue.end();
    const bool udpMode = optionsWithoutValue.find("--udp") != optionsWithoutValue.end();

    if (tcpMode && udpMode) {
      logError("update-server: --tcp and --udp modes are mutually exclusive");
      exit(1);
    }

    enum class UpdateServerMode {
      Http,
      Tcp,
      Udp
    };

    UpdateServerMode mode = UpdateServerMode::Http;
    if (tcpMode) {
      mode = UpdateServerMode::Tcp;
    } else if (udpMode) {
      mode = UpdateServerMode::Udp;
    }

    if (mode == UpdateServerMode::Http) {
#if !ORO_CLI_HAS_CPPHTTPLIB
      logError("update-server: HTTP server support is not available in this build (cpp-httplib missing)");
      exit(1);
#else
      httplib::Server server;

      server.set_keep_alive_max_count(100);
      server.set_keep_alive_timeout(10);
      server.new_task_queue = [] {
        return new httplib::ThreadPool(8);
      };

      server.Get("/health", [&](const httplib::Request& req, httplib::Response& res) {
        (void) req;
        JSON::Object health = JSON::Object::Entries {
          { "ok", true },
          { "mode", "http" },
          { "version", 1 }
        };
        res.set_content(health.str(), "application/json");
      });

      server.Post("/check", [&](const httplib::Request& req, httplib::Response& res) {
        try {
          auto body = String(req.body);
          JSON::Any any = JSON::parse(body);
          if (!any.isObject()) {
            res.status = 400;
            res.set_content("{\"error\":\"CHECK payload must be a JSON object\"}\n", "application/json");
            return;
          }

          auto& obj = any.as<JSON::Object>();
          if (!obj.contains("appId") || !obj.get("appId").isString()) {
            res.status = 400;
            res.set_content("{\"error\":\"CHECK payload must include string appId\"}\n", "application/json");
            return;
          }

          const String appId = obj.get("appId").str();
          if (appId.size() == 0) {
            res.status = 400;
            res.set_content("{\"error\":\"CHECK payload must include non-empty appId\"}\n", "application/json");
            return;
          }

          JSON::Object response = buildUpdateServerCheckResponse(appId);
          res.set_content(response.str(), "application/json");
          return;
        } catch (...) {
          res.status = 400;
          res.set_content("{\"error\":\"Invalid JSON in CHECK payload\"}\n", "application/json");
          return;
        }
      });

      // Serve any file under the root directory, mapping request path -> filesystem path.
      server.Get(R"(^/(.*)$)", [rootPath](const httplib::Request& req, httplib::Response& res) {
        String path = req.path;
        if (path.size() == 0 || path[0] != '/') {
          res.status = 404;
          return;
        }

        // Strip leading '/'
        path = path.substr(1, path.size() - 1);

        if (path.size() == 0) {
          res.status = 404;
          return;
        }

        auto parts = split(path, '/');
        for (const auto& part : parts) {
          if (part == ".." || part.find(':') != String::npos) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid path\"}\n", "application/json");
            return;
          }
        }

        fs::path local = rootPath;
        for (const auto& part : parts) {
          local /= part;
        }

        std::error_code ec;
        if (!fs::exists(local, ec) || !fs::is_regular_file(local, ec)) {
          res.status = 404;
          return;
        }

        String body = readFile(local);
        if (body.size() == 0) {
          res.status = 404;
          return;
        }

        String contentType = "application/octet-stream";
        auto ext = toLowerCase(local.extension().string());
        if (ext == ".json" || ext == ".sig") {
          contentType = "application/json";
        }

        res.set_content(body, contentType.c_str());
      });

      logInfo("update-server: starting HTTP server on " + host + ":" + std::to_string(port));
      if (!server.bind_to_port(host.c_str(), port)) {
        logError("update-server: failed to bind HTTP server on " + host + ":" + std::to_string(port));
        exit(1);
      }

      server.listen_after_bind();
      exit(0);
#endif
    }

    if (mode == UpdateServerMode::Tcp) {
      // TCP server implementing the binary CHECK/RESPONSE protocol.
      uv_loop_t loop;
      if (uv_loop_init(&loop) != 0) {
        logError("update-server: failed to initialize TCP event loop");
        exit(1);
      }

      uv_tcp_t serverHandle;
      if (uv_tcp_init(&loop, &serverHandle) != 0) {
        logError("update-server: failed to initialize TCP server handle");
        uv_loop_close(&loop);
        exit(1);
      }

      sockaddr_in addr;
      if (uv_ip4_addr(host.c_str(), port, &addr) != 0) {
        logError("update-server: TCP mode currently supports IPv4 listen addresses only");
        uv_close(reinterpret_cast<uv_handle_t*>(&serverHandle), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      if (uv_tcp_bind(&serverHandle, reinterpret_cast<const struct sockaddr*>(&addr), 0) != 0) {
        logError("update-server: failed to bind TCP server socket");
        uv_close(reinterpret_cast<uv_handle_t*>(&serverHandle), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      // Use a conservative backlog to remain portable across platforms.
      int listenResult = uv_listen(
        reinterpret_cast<uv_stream_t*>(&serverHandle),
        128,
        updateTcpOnConnection
      );

      if (listenResult != 0) {
        logError("update-server: failed to listen on TCP port");
        uv_close(reinterpret_cast<uv_handle_t*>(&serverHandle), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      logInfo("update-server: starting TCP server on " + host + ":" + std::to_string(port));
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
      exit(0);
    }

    if (mode == UpdateServerMode::Udp) {
      // UDP server implementing the binary CHECK/RESPONSE protocol.
      uv_loop_t loop;
      if (uv_loop_init(&loop) != 0) {
        logError("update-server: failed to initialize UDP event loop");
        exit(1);
      }

      uv_udp_t udpServer;
      if (uv_udp_init(&loop, &udpServer) != 0) {
        logError("update-server: failed to initialize UDP server handle");
        uv_loop_close(&loop);
        exit(1);
      }

      sockaddr_in addr;
      if (uv_ip4_addr(host.c_str(), port, &addr) != 0) {
        logError("update-server: UDP mode currently supports IPv4 listen addresses only");
        uv_close(reinterpret_cast<uv_handle_t*>(&udpServer), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      if (uv_udp_bind(&udpServer, reinterpret_cast<const struct sockaddr*>(&addr), 0) != 0) {
        logError("update-server: failed to bind UDP server socket");
        uv_close(reinterpret_cast<uv_handle_t*>(&udpServer), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      if (uv_udp_recv_start(&udpServer, updateUdpAlloc, updateUdpOnRecv) != 0) {
        logError("update-server: failed to start UDP receive");
        uv_close(reinterpret_cast<uv_handle_t*>(&udpServer), nullptr);
        uv_loop_close(&loop);
        exit(1);
      }

      logInfo("update-server: starting UDP server on " + host + ":" + std::to_string(port));
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
      exit(0);
    }

    exit(0);
  });

  CommandLineOptions updateInfoOptions = {
    { { "--config" }, true, true },
    { { "--transport" }, true, true },
    { { "--http" }, true, false },
    { { "--tcp" }, true, false },
    { { "--udp" }, true, false },
    { { "--follow-manifest" }, true, false },
    { { "--timeout-ms" }, true, true },
    { { "--host" }, true, true },
    { { "--port" }, true, true },
    { { "--manifest-url" }, true, true },
    { { "--signature-url" }, true, true },
    { { "--keys" }, true, true },
    { { "--public-key" }, true, true },
    { { "--app-id" }, true, true },
    { { "--channel" }, true, true },
    { { "--current-version" }, true, true },
    { { "--runtime-version" }, true, true },
    { { "--platform" }, true, true },
    { { "--arch" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-info", updateInfoOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    String transport = optionsWithValue["--transport"];
    const bool httpFlag = optionsWithoutValue.find("--http") != optionsWithoutValue.end();
    const bool tcpFlag = optionsWithoutValue.find("--tcp") != optionsWithoutValue.end();
    const bool udpFlag = optionsWithoutValue.find("--udp") != optionsWithoutValue.end();
    const bool followManifest = optionsWithoutValue.find("--follow-manifest") != optionsWithoutValue.end();

    const int flagCount = (httpFlag ? 1 : 0) + (tcpFlag ? 1 : 0) + (udpFlag ? 1 : 0);
    if (flagCount > 1) {
      logError("update-info: --http, --tcp, and --udp are mutually exclusive");
      exit(1);
    }

    if (transport.size() > 0) {
      transport = toLowerCase(transport);
      if (transport != "http" && transport != "tcp" && transport != "udp") {
        logError("update-info: --transport must be 'http', 'tcp', or 'udp'");
        exit(1);
      }
      if (httpFlag && transport != "http") {
        logError("update-info: --http conflicts with --transport");
        exit(1);
      }
      if (tcpFlag && transport != "tcp") {
        logError("update-info: --tcp conflicts with --transport");
        exit(1);
      }
      if (udpFlag && transport != "udp") {
        logError("update-info: --udp conflicts with --transport");
        exit(1);
      }
    } else {
      if (tcpFlag) {
        transport = "tcp";
      } else if (udpFlag) {
        transport = "udp";
      } else {
        transport = "http";
      }
    }

    enum class UpdateInfoTransport {
      Http,
      Tcp,
      Udp
    };

    UpdateInfoTransport mode = UpdateInfoTransport::Http;
    if (transport == "tcp") {
      mode = UpdateInfoTransport::Tcp;
    } else if (transport == "udp") {
      mode = UpdateInfoTransport::Udp;
    }

    uint64_t timeoutMs = 0;
    if (optionsWithValue["--timeout-ms"].size() > 0) {
      try {
        auto parsed = std::stoll(optionsWithValue["--timeout-ms"]);
        if (parsed < 0) {
          logError("update-info: --timeout-ms must be non-negative");
          exit(1);
        }
        timeoutMs = static_cast<uint64_t>(parsed);
      } catch (...) {
        logError("update-info: --timeout-ms must be an integer");
        exit(1);
      }
    }

    String host = optionsWithValue["--host"];
    if (host.size() == 0) {
      host = String("127.0.0.1");
    }

    int port = 0;
    if (optionsWithValue["--port"].size() > 0) {
      try {
        port = std::stoi(optionsWithValue["--port"]);
      } catch (...) {
        logError("update-info: --port must be a valid integer");
        exit(1);
      }
      if (port <= 0 || port > 65535) {
        logError("update-info: --port must be between 1 and 65535");
        exit(1);
      }
    } else {
      port = 8080;
    }

    const bool needsCheckRequestDefaults =
      !(mode == UpdateInfoTransport::Http && optionsWithValue["--manifest-url"].size() > 0);

    String appId = optionsWithValue["--app-id"];
    if (needsCheckRequestDefaults &&
        (appId.size() == 0 || optionsWithValue["--channel"].size() == 0 || optionsWithValue["--current-version"].size() == 0)) {
      Path configPath;
      bool shouldLoadDefaults = false;

      if (optionsWithValue["--config"].size() > 0) {
        auto provided = fs::absolute(optionsWithValue["--config"]).lexically_normal();
        if (!fileExists(provided)) {
          logError("update-info: --config expects a path to an existing oro.toml or oro.ini file");
          exit(1);
        }

        const auto configFilenameLower = toLowerCase(provided.filename().string());
        if (configFilenameLower != "oro.toml" && configFilenameLower != "oro.ini") {
          logError("update-info: --config expects a path to an existing oro.toml or oro.ini file");
          exit(1);
        }

        configPath = provided;
        shouldLoadDefaults = true;
      } else {
        const auto preferredConfigPath = targetPath / "oro.toml";
        const auto oroIniConfigPath = targetPath / "oro.ini";
        const bool hasPreferred = fileExists(preferredConfigPath);
        const bool hasOroIni = fileExists(oroIniConfigPath);

        if (hasPreferred && hasOroIni) {
          logWarn("detected both oro.toml and oro.ini; defaulting to oro.toml");
        }

        if (hasPreferred) {
          configPath = preferredConfigPath;
          shouldLoadDefaults = true;
        } else if (hasOroIni) {
          configPath = oroIniConfigPath;
          shouldLoadDefaults = true;
        }
      }

      if (shouldLoadDefaults && !configPath.empty()) {
        const auto configFormat = detectConfigFormatForPath(configPath);
        const auto configDocument = readFile(configPath);
        if (configDocument.size() > 0) {
          try {
            const auto renderedConfig = tmpl(configDocument, Map<> {
              {"platform.arch", platform.arch},
              {"platform.arch.short", replace(platform.arch, "x86_64", "x64")},
              {"platform.os", platform.os},
              {"platform.os.short", replace(platform.os, "win32", "win")}
            });

            const auto loadedConfig = parseUserConfigSource(renderedConfig, configFormat);
            if (appId.size() == 0) {
              const auto it = loadedConfig.find("meta_bundle_identifier");
              if (it != loadedConfig.end()) {
                appId = it->second;
              }
            }
            if (optionsWithValue["--channel"].size() == 0) {
              const auto it = loadedConfig.find("update_channel");
              if (it != loadedConfig.end() && it->second.size() > 0) {
                optionsWithValue["--channel"] = it->second;
              }
            }
            if (optionsWithValue["--current-version"].size() == 0) {
              const auto it = loadedConfig.find("meta_version");
              if (it != loadedConfig.end()) {
                optionsWithValue["--current-version"] = it->second;
              }
            }
          } catch (const std::exception& error) {
            logError("update-info: failed to parse config '" + configPath.string() + "': " + String(error.what()));
            exit(1);
          }
        }
      }
    }

    String channel = optionsWithValue["--channel"];
    if (channel.size() == 0) {
      channel = settings["update_channel"].size() > 0 ? settings["update_channel"] : "stable";
    }

    String currentVersion = optionsWithValue["--current-version"];
    if (currentVersion.size() == 0) {
      currentVersion = settings["meta_version"];
    }

    String runtimeVersion = optionsWithValue["--runtime-version"];
    String platformName = optionsWithValue["--platform"];
    String archName = optionsWithValue["--arch"];
    const String keysPath = optionsWithValue["--keys"];
    const String publicKeyArg = optionsWithValue["--public-key"];

    const bool hasVerifyKeys = keysPath.size() > 0 || publicKeyArg.size() > 0;
    if (hasVerifyKeys) {
      const String manifestUrlForVerify = optionsWithValue["--manifest-url"];
      const bool staticHttp = (mode == UpdateInfoTransport::Http && manifestUrlForVerify.size() > 0);
      const bool willFollow = followManifest;
      if (!staticHttp && !willFollow) {
        logError("update-info: --keys/--public-key are only supported with --manifest-url in HTTP static mode or together with --follow-manifest");
        exit(1);
      }
    }

    if (mode == UpdateInfoTransport::Http) {
      String manifestUrl = optionsWithValue["--manifest-url"];
      String signatureUrlOverride = optionsWithValue["--signature-url"];
      const bool wantVerify = keysPath.size() > 0 || publicKeyArg.size() > 0;

      // Static hosting: explicit manifest URL.
      if (manifestUrl.size() > 0) {
#if !ORO_CLI_HAS_CPPHTTPLIB
        logError("update-info: HTTP client support is not available in this build (cpp-httplib missing)");
        exit(1);
#else
        if (wantVerify && !ORO_CLI_HAS_SODIUM) {
          logError("update-info: manifest verification requires libsodium; rebuild with libsodium headers and libraries available.");
          exit(1);
        }

        URL url(manifestUrl);
        if (url.scheme.size() == 0 || url.hostname.size() == 0) {
          logError("update-info: --manifest-url must include a scheme and hostname");
          exit(1);
        }

        String schemeLower = toLowerCase(url.scheme);
        if (schemeLower != "http" && schemeLower != "https") {
          logError("update-info: --manifest-url must use http or https");
          exit(1);
        }

        int urlPort = 0;
        if (url.port.size() > 0) {
          try {
            urlPort = std::stoi(url.port);
          } catch (...) {
            urlPort = 0;
          }
        }
        if (urlPort <= 0) {
          urlPort = schemeLower == "https" ? 443 : 80;
        }

        String path = url.pathname.size() > 0 ? url.pathname : String("/");
        if (!url.search.empty()) {
          path += url.search;
        }

        httplib::Result res;
        if (schemeLower == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
          httplib::SSLClient client(url.hostname.c_str(), urlPort);
          client.enable_server_certificate_verification(true);
          client.set_follow_location(true);
          res = client.Get(path.c_str());
#else
          logError("update-info: HTTPS support is not enabled in this build");
          exit(1);
#endif
        } else {
          httplib::Client client(url.hostname.c_str(), urlPort);
          client.set_follow_location(true);
          res = client.Get(path.c_str());
        }

        if (!res) {
          logError("update-info: failed to fetch manifest from " + manifestUrl);
          exit(1);
        }

        if (res->status < 200 || res->status >= 300) {
          logError("update-info: HTTP " + std::to_string(res->status) + " while fetching manifest from " + manifestUrl);
          exit(1);
        }

        String manifestText(res->body);

        bool signatureFound = false;
        String signatureBody;

        if (signatureUrlOverride.size() > 0) {
          URL sigUrl(signatureUrlOverride);
          if (sigUrl.scheme.size() == 0 || sigUrl.hostname.size() == 0) {
            logError("update-info: --signature-url must include a scheme and hostname");
            exit(1);
          }
          String sigSchemeLower = toLowerCase(sigUrl.scheme);
          if (sigSchemeLower != "http" && sigSchemeLower != "https") {
            logError("update-info: --signature-url must use http or https");
            exit(1);
          }
          int sigPort = 0;
          if (sigUrl.port.size() > 0) {
            try {
              sigPort = std::stoi(sigUrl.port);
            } catch (...) {
              sigPort = 0;
            }
          }
          if (sigPort <= 0) {
            sigPort = sigSchemeLower == "https" ? 443 : 80;
          }
  String sigPath = sigUrl.pathname.size() > 0 ? sigUrl.pathname : String("/");
  if (!sigUrl.search.empty()) {
    sigPath += sigUrl.search;
  }

  httplib::Result sigRes;
  if (sigSchemeLower == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient client(sigUrl.hostname.c_str(), sigPort);
    client.enable_server_certificate_verification(true);
    client.set_follow_location(true);
    sigRes = client.Get(sigPath.c_str());
#else
    logError("update-info: HTTPS support is not enabled in this build");
    exit(1);
#endif
  } else {
            httplib::Client client(sigUrl.hostname.c_str(), sigPort);
            client.set_follow_location(true);
            sigRes = client.Get(sigPath.c_str());
          }

          if (sigRes && sigRes->status >= 200 && sigRes->status < 300) {
            signatureFound = true;
            signatureBody = sigRes->body;
          }
        } else {
          String manifestPath = url.pathname;
          String filename = manifestPath;
          String prefix;
          auto slashPos = manifestPath.rfind('/');
          if (slashPos != String::npos) {
            prefix = manifestPath.substr(0, slashPos + 1);
            filename = manifestPath.substr(slashPos + 1);
          } else {
            prefix = "/";
          }

          fs::path filenamePath(filename.c_str());
          String stem = filenamePath.stem().string();
          if (stem.size() == 0) {
            stem = filename;
          }

          String sigPath = prefix + stem + ".sig";

  httplib::Result sigRes;
  if (schemeLower == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient client(url.hostname.c_str(), urlPort);
    client.enable_server_certificate_verification(true);
    client.set_follow_location(true);
    sigRes = client.Get(sigPath.c_str());
#else
    logError("update-info: HTTPS support is not enabled in this build");
    exit(1);
#endif
  } else {
            httplib::Client client(url.hostname.c_str(), urlPort);
            client.set_follow_location(true);
            sigRes = client.Get(sigPath.c_str());
          }

          if (sigRes && sigRes->status >= 200 && sigRes->status < 300) {
            signatureFound = true;
            signatureBody = sigRes->body;
          }
        }

        if (signatureFound) {
          logInfo("update-info: found signature file alongside manifest");
        } else {
          logInfo("update-info: signature file was not found or not fetched");
        }

        if (wantVerify) {
#if !ORO_CLI_HAS_SODIUM
          logError("update-info: manifest verification requires libsodium; rebuild with libsodium headers and libraries available.");
          exit(1);
#else
          if (!signatureFound || signatureBody.size() == 0) {
            logError("update-info: cannot verify manifest without a signature file; provide --signature-url if needed");
            exit(1);
          }

          String manifestBytes = manifestText;

          String signatureText = signatureBody;
          JSON::Any sigAny;
          try {
            sigAny = JSON::parse(signatureText);
          } catch (const JSON::Error& error) {
            logError("update-info: manifest signature JSON is invalid: " + String(error.what()));
            exit(1);
          } catch (const std::exception& error) {
            logError("update-info: manifest signature JSON is invalid: " + String(error.what()));
            exit(1);
          }

          if (!sigAny.isObject()) {
            logError("update-info: manifest signature JSON must be an object");
            exit(1);
          }

          auto& sigObj = sigAny.as<JSON::Object>();
          if (sigObj.contains("algorithm") && sigObj.get("algorithm").str() != "ed25519") {
            logError("update-info: unsupported signature algorithm in manifest.sig; expected 'ed25519'");
            exit(1);
          }
          if (!sigObj.contains("signature")) {
            logError("update-info: manifest signature JSON missing 'signature' field");
            exit(1);
          }
          String sigHex = sigObj.get("signature").str();

          const String sigDecoded = oro::runtime::bytes::decodeHexString(sigHex);
          Vector<uint8_t> signature(sigDecoded.begin(), sigDecoded.end());

          if (signature.size() != crypto_sign_BYTES) {
            logError("update-info: manifest signature length does not match Ed25519 detached signature size");
            exit(1);
          }

          Vector<uint8_t> publicKey;
          if (keysPath.size() > 0) {
            const String jsonText = readFile(keysPath);
            if (jsonText.size() == 0) {
              logError("update-info: keys file is empty or unreadable: " + keysPath);
              exit(1);
            }

            try {
              auto any = JSON::parse(jsonText);
              if (!any.isObject()) {
                logError("update-info: keys file must contain a JSON object");
                exit(1);
              }

              auto& obj = any.as<JSON::Object>();
              String pkHex;
              if (obj.contains("publicKey")) {
                pkHex = obj.at("publicKey").str();
              } else if (obj.contains("key")) {
                pkHex = obj.at("key").str();
              }

              if (pkHex.size() == 0) {
                logError("update-info: keys file must include 'publicKey' or 'key'");
                exit(1);
              }

              const String decoded = oro::runtime::bytes::decodeHexString(pkHex);
              publicKey.assign(decoded.begin(), decoded.end());
            } catch (const JSON::Error& error) {
              logError("update-info: failed to parse keys file as JSON: " + String(error.what()));
              exit(1);
            } catch (const std::exception& error) {
              logError("update-info: failed to parse keys file as JSON: " + String(error.what()));
              exit(1);
            }
          } else {
            const String decoded = oro::runtime::bytes::decodeHexString(publicKeyArg);
            publicKey.assign(decoded.begin(), decoded.end());
          }

          if (publicKey.size() != crypto_sign_PUBLICKEYBYTES) {
            logError("update-info: public key must be an Ed25519 public key (hex-encoded)");
            exit(1);
          }

          if (sodium_init() < 0) {
            logError("update-info: unable to initialize libsodium for manifest verification");
            exit(1);
          }

          const int ok = crypto_sign_verify_detached(
            signature.data(),
            reinterpret_cast<const unsigned char*>(manifestBytes.data()),
            manifestBytes.size(),
            publicKey.data()
          );

          if (ok != 0) {
            logError("update-info: manifest signature verification FAILED");
            exit(1);
          }

          logInfo("update-info: manifest signature is VALID for the provided public key");
#endif
        }

        JSON::Any manifestAny;
        try {
          manifestAny = JSON::parse(manifestText);
        } catch (const JSON::Error& error) {
          logError("update-info: manifest at " + manifestUrl + " is not valid JSON: " + String(error.what()));
          exit(1);
        } catch (const std::exception& error) {
          logError("update-info: manifest at " + manifestUrl + " is not valid JSON: " + String(error.what()));
          exit(1);
        }

        if (!manifestAny.isObject()) {
          logError("update-info: manifest at " + manifestUrl + " must be a JSON object");
          exit(1);
        }

        String validationError;
        if (!validateUpdateManifest(manifestAny, validationError)) {
          logError("update-info: " + validationError);
          exit(1);
        }

        auto& manifestObj = manifestAny.as<JSON::Object>();
        String manifestAppId = manifestObj.contains("appId") ? manifestObj.get("appId").str() : "";
        size_t updateCount = 0;
        if (manifestObj.contains("updates") && manifestObj.get("updates").isArray()) {
          updateCount = manifestObj.get("updates").as<JSON::Array>().size();
        }

        logInfo("update-info: fetched manifest for appId '" + manifestAppId + "' with " + std::to_string(updateCount) + " update(s)");

        std::cout << prettyJson(manifestAny.str()) << std::endl;
        exit(0);
#endif
      }

      // HTTP CHECK/RESPONSE against an update server when no manifest-url is provided.
#if !ORO_CLI_HAS_CPPHTTPLIB
      logError("update-info: HTTP client support is not available in this build (cpp-httplib missing)");
      exit(1);
#else
      if (appId.size() == 0) {
        appId = settings["meta_bundle_identifier"];
      }
      if (appId.size() == 0) {
        logError("update-info: --app-id is required when contacting HTTP update servers without --manifest-url");
        exit(1);
      }

      JSON::Object::Entries payloadEntriesHttp {
        { "schemaVersion", 1 },
        { "appId", appId }
      };
      if (currentVersion.size() > 0) {
        payloadEntriesHttp.emplace("currentVersion", currentVersion);
      }
      if (channel.size() > 0) {
        payloadEntriesHttp.emplace("channel", channel);
      }
      if (platformName.size() > 0) {
        payloadEntriesHttp.emplace("platform", platformName);
      }
      if (archName.size() > 0) {
        payloadEntriesHttp.emplace("arch", archName);
      }
      if (runtimeVersion.size() > 0) {
        payloadEntriesHttp.emplace("runtimeVersion", runtimeVersion);
      }

      JSON::Object payloadHttp(payloadEntriesHttp);
      String body = payloadHttp.str();

      httplib::Client client(host.c_str(), port);
      client.set_follow_location(true);

      httplib::Result res = client.Post("/check", body, "application/json");
      if (!res) {
        logError("update-info: failed to POST /check to " + host + ":" + std::to_string(port));
        exit(1);
      }

      if (res->status < 200 || res->status >= 300) {
        logError("update-info: HTTP " + std::to_string(res->status) + " while calling /check on " + host + ":" + std::to_string(port));
        exit(1);
      }

      String respBody(res->body);
      JSON::Any respAny;
      try {
        respAny = JSON::parse(respBody);
      } catch (const JSON::Error& error) {
        logError("update-info: invalid JSON in HTTP RESPONSE body: " + String(error.what()));
        exit(1);
      } catch (const std::exception& error) {
        logError("update-info: invalid JSON in HTTP RESPONSE body: " + String(error.what()));
        exit(1);
      }

      if (!respAny.isObject()) {
        logError("update-info: RESPONSE body must be a JSON object");
        exit(1);
      }

      auto& respObj = respAny.as<JSON::Object>();
      bool hasUpdate = false;
      if (respObj.contains("hasUpdate") && respObj.get("hasUpdate").isBoolean()) {
        hasUpdate = respObj.get("hasUpdate").as<JSON::Boolean>().value();
      }
      String selectedUpdateId = respObj.contains("selectedUpdateId") ? respObj.get("selectedUpdateId").str() : "";
      String manifestUrlResp = respObj.contains("manifestUrl") ? respObj.get("manifestUrl").str() : "";

      logInfo(
        "update-info: HTTP server RESPONSE: hasUpdate=" +
        String(hasUpdate ? "true" : "false") +
        (selectedUpdateId.size() > 0 ? String(" selectedUpdateId='") + selectedUpdateId + "'" : String("")) +
        (manifestUrlResp.size() > 0 ? String(" manifestUrl='") + manifestUrlResp + "'" : String(""))
      );

      if (followManifest) {
        if (manifestUrlResp.size() == 0) {
          logError("update-info: --follow-manifest was requested, but server RESPONSE did not include 'manifestUrl'");
          exit(1);
        }
        fetchManifestFromUrlAndMaybeVerify(manifestUrlResp, keysPath, publicKeyArg, appId);
      }

      std::cout << prettyJson(respAny.str()) << std::endl;
      exit(0);
#endif
    }

    if (appId.size() == 0) {
      logError("update-info: --app-id is required when contacting TCP/UDP update servers");
      exit(1);
    }

    // TCP and UDP use the binary OUP framing and JSON CHECK/RESPONSE payloads.
    JSON::Object::Entries payloadEntries {
      { "schemaVersion", 1 },
      { "appId", appId }
    };
    if (currentVersion.size() > 0) {
      payloadEntries.emplace("currentVersion", currentVersion);
    }
    if (channel.size() > 0) {
      payloadEntries.emplace("channel", channel);
    }
    if (platformName.size() > 0) {
      payloadEntries.emplace("platform", platformName);
    }
    if (archName.size() > 0) {
      payloadEntries.emplace("arch", archName);
    }
    if (runtimeVersion.size() > 0) {
      payloadEntries.emplace("runtimeVersion", runtimeVersion);
    }

    JSON::Object payload(payloadEntries);
    String payloadText = payload.str();

    String responseBody;
    String errorText;

    if (mode == UpdateInfoTransport::Tcp) {
      if (!runUpdateInfoTcp(host, port, payloadText, responseBody, errorText, timeoutMs)) {
        if (errorText.size() > 0) {
          logError("update-info: TCP CHECK failed: " + errorText);
        } else {
          logError("update-info: TCP CHECK failed");
        }
        exit(1);
      }
    } else if (mode == UpdateInfoTransport::Udp) {
      if (!runUpdateInfoUdp(host, port, payloadText, responseBody, errorText, timeoutMs)) {
        if (errorText.size() > 0) {
          logError("update-info: UDP CHECK failed: " + errorText);
        } else {
          logError("update-info: UDP CHECK failed");
        }
        exit(1);
      }
    }

    JSON::Any respAny;
    try {
      respAny = JSON::parse(responseBody);
    } catch (const JSON::Error& error) {
      logError("update-info: invalid JSON in binary RESPONSE payload: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-info: invalid JSON in binary RESPONSE payload: " + String(error.what()));
      exit(1);
    }

    if (!respAny.isObject()) {
      logError("update-info: RESPONSE payload must be a JSON object");
      exit(1);
    }

    auto& respObj = respAny.as<JSON::Object>();
    bool hasUpdate = false;
    if (respObj.contains("hasUpdate") && respObj.get("hasUpdate").isBoolean()) {
      hasUpdate = respObj.get("hasUpdate").as<JSON::Boolean>().value();
    }
    String selectedUpdateId = respObj.contains("selectedUpdateId") ? respObj.get("selectedUpdateId").str() : "";
    String manifestUrlResp = respObj.contains("manifestUrl") ? respObj.get("manifestUrl").str() : "";

    const char* modeLabel =
      mode == UpdateInfoTransport::Tcp ? "TCP" :
      mode == UpdateInfoTransport::Udp ? "UDP" : "UNKNOWN";

    logInfo(
      "update-info: " + String(modeLabel) + " server RESPONSE: hasUpdate=" +
      String(hasUpdate ? "true" : "false") +
      (selectedUpdateId.size() > 0 ? String(" selectedUpdateId='") + selectedUpdateId + "'" : String("")) +
      (manifestUrlResp.size() > 0 ? String(" manifestUrl='") + manifestUrlResp + "'" : String(""))
    );

    if (followManifest) {
      if (manifestUrlResp.size() == 0) {
        logError("update-info: --follow-manifest was requested, but server RESPONSE did not include 'manifestUrl'");
        exit(1);
      }
      fetchManifestFromUrlAndMaybeVerify(manifestUrlResp, keysPath, publicKeyArg, appId);
    }

    std::cout << prettyJson(respAny.str()) << std::endl;
    exit(0);
  });

  CommandLineOptions updateKeygenOptions = {
    { { "--out" }, true, true },
    { { "--key-id" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-keygen", updateKeygenOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
#if !ORO_CLI_HAS_SODIUM
    logError("update-keygen requires libsodium; rebuild with libsodium headers and libraries available.");
    exit(1);
#else
    if (sodium_init() < 0) {
      logError("update-keygen: unable to initialize libsodium");
      exit(1);
    }

    Vector<uint8_t> publicKey(crypto_sign_PUBLICKEYBYTES);
    Vector<uint8_t> privateKey(crypto_sign_SECRETKEYBYTES);

    if (crypto_sign_keypair(publicKey.data(), privateKey.data()) != 0) {
      logError("update-keygen: crypto_sign_keypair failed");
      exit(1);
    }

    String keyId = optionsWithValue["--key-id"];
    if (keyId.size() == 0) {
      keyId = "pk-1";
    }

    String publicKeyHex = oro::runtime::bytes::encodeHexString(publicKey);
    String privateKeyHex = oro::runtime::bytes::encodeHexString(privateKey);

    JSON::Object::Entries jsonEntries {
      { "keyId", keyId },
      { "publicKey", publicKeyHex },
      { "privateKey", privateKeyHex }
    };

    JSON::Object json(jsonEntries);
    const String outPath = optionsWithValue["--out"];

    if (outPath.size() > 0) {
      writeFile(outPath, prettyJson(json.str()) + "\n");
      logInfo("wrote update signing keypair to " + outPath);
    } else {
      std::cout << prettyJson(json.str()) << std::endl;
    }

    exit(0);
#endif
  });

  CommandLineOptions updateSignOptions = {
    { { "--manifest" }, true, true },
    { { "--manifest-name" }, true, true },
    { { "--keys" }, true, true },
    { { "--private-key" }, true, true },
    { { "--key-id" }, true, true },
    { { "--out" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-sign", updateSignOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
#if !ORO_CLI_HAS_SODIUM
    logError("update-sign requires libsodium; rebuild with libsodium headers and libraries available.");
    exit(1);
#else
    if (sodium_init() < 0) {
      logError("update-sign: unable to initialize libsodium");
      exit(1);
    }

    String manifestPath = optionsWithValue["--manifest"];
    String manifestNameOverride = optionsWithValue["--manifest-name"];
    if (manifestPath.size() == 0) {
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = env::get("ORO_UPDATE_MANIFEST_FILENAME");
      }
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = DEFAULT_UPDATE_MANIFEST_FILENAME;
      }
      manifestPath = manifestNameOverride;
    }

    const String keysPath = optionsWithValue["--keys"];
    const String privateKeyArg = optionsWithValue["--private-key"];

    if (keysPath.size() == 0 && privateKeyArg.size() == 0) {
      logError("update-sign: expected --keys=<file> or --private-key=<hex>");
      exit(1);
    }

    String manifestBytes = readFile(manifestPath);
    if (manifestBytes.size() == 0) {
      logError("update-sign: manifest file is empty or unreadable: " + manifestPath);
      exit(1);
    }

    // Validate manifest structure against the update manifest schema shape.
    try {
      JSON::Any manifestAny = JSON::parse(manifestBytes);
      String validationError;
      if (!validateUpdateManifest(manifestAny, validationError)) {
        logError("update-sign: " + validationError);
        exit(1);
      }
    } catch (const JSON::Error& error) {
      logError("update-sign: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-sign: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    }

    Vector<uint8_t> privateKey;
    String keyId = optionsWithValue["--key-id"];

    if (keysPath.size() > 0) {
      const String jsonText = readFile(keysPath);
      if (jsonText.size() == 0) {
        logError("update-sign: keys file is empty or unreadable: " + keysPath);
        exit(1);
      }

      try {
        auto any = JSON::parse(jsonText);
        if (!any.isObject()) {
          logError("update-sign: keys file must contain a JSON object");
          exit(1);
        }

        auto& obj = any.as<JSON::Object>();
        String pkHex;
        if (obj.contains("privateKey")) {
          pkHex = obj.at("privateKey").str();
        } else if (obj.contains("secretKey")) {
          pkHex = obj.at("secretKey").str();
        }

        if (pkHex.size() == 0) {
          logError("update-sign: keys file must include 'privateKey' or 'secretKey'");
          exit(1);
        }

        if (keyId.size() == 0 && obj.contains("keyId")) {
          keyId = obj.at("keyId").str();
        }

        const String decoded = oro::runtime::bytes::decodeHexString(pkHex);
        privateKey.assign(decoded.begin(), decoded.end());
      } catch (const JSON::Error& error) {
        logError("update-sign: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      } catch (const std::exception& error) {
        logError("update-sign: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      }
    } else {
      const String decoded = oro::runtime::bytes::decodeHexString(privateKeyArg);
      privateKey.assign(decoded.begin(), decoded.end());
    }

    if (privateKey.size() != crypto_sign_SECRETKEYBYTES) {
      logError(
        "update-sign: private key must be an Ed25519 secret key (hex-encoded); "
        "for keys generated via 'oroc update keygen', pass the JSON file via --keys=<file>"
      );
      exit(1);
    }

    if (keyId.size() == 0) {
      keyId = "pk-1";
    }

    Vector<uint8_t> signature(crypto_sign_BYTES);
    if (crypto_sign_detached(
          signature.data(),
          nullptr,
          reinterpret_cast<const unsigned char*>(manifestBytes.data()),
          manifestBytes.size(),
          privateKey.data()) != 0) {
      logError("update-sign: crypto_sign_detached failed");
      exit(1);
    }

    const String sigHex = oro::runtime::bytes::encodeHexString(signature);

    JSON::Object::Entries sigJsonEntries {
      { "schemaVersion", 1 },
      { "algorithm", "ed25519" },
      { "keyId", keyId },
      { "signature", sigHex }
    };

    JSON::Object sigJson(sigJsonEntries);

    String outPath = optionsWithValue["--out"];
    if (outPath.size() == 0) {
      fs::path p(manifestPath);
      const auto stem = p.stem().string();
      outPath = (p.parent_path() / (stem + ".sig")).string();
    }

    writeFile(outPath, prettyJson(sigJson.str()) + "\n");
    logInfo("wrote manifest signature to " + outPath);
    exit(0);
#endif
  });

  // Build a tar archive containing the contents of a directory.
  CommandLineOptions updateBundleOptions = {
    { { "--input" }, true, true },
    { { "--output" }, true, true },
    { { "--manifest" }, true, true },
    { { "--manifest-name" }, true, true },
    { { "--channel" }, true, true },
    { { "--update-id" }, true, true },
    { { "--platform" }, true, true },
    { { "--arch" }, true, true },
    { { "--artifact-url" }, true, true },
    { { "--hash-algorithm" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-bundle", updateBundleOptions, true, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    (void) optionsWithoutValue;

    String inputPathStr = optionsWithValue["--input"];
    String outputPathStr = optionsWithValue["--output"];

    if (inputPathStr.size() == 0) {
      // Default to the project directory (app source) rather than a platform-specific build output.
      inputPathStr = targetPath.string();
    }

    if (outputPathStr.size() == 0) {
      String buildName = settings["build_name"];
      String version = settings["meta_version"];
      String fileName;
      if (buildName.size() > 0 && version.size() > 0) {
        fileName = buildName + "-" + version + ".tar";
      } else {
        fileName = "update-bundle.tar";
      }
      fs::path defaultOutput = targetPath / fileName;
      outputPathStr = defaultOutput.string();
    }

    fs::path inputPath(inputPathStr);
    fs::path outputPath(outputPathStr);

    std::error_code ec;
    if (!fs::exists(inputPath, ec) || !fs::is_directory(inputPath, ec)) {
      logError("update-bundle: input path is not a directory: " + inputPath.string());
      exit(1);
    }

    // Ensure parent directory for output exists.
    if (outputPath.has_parent_path()) {
      fs::create_directories(outputPath.parent_path(), ec);
      if (ec) {
        logError("update-bundle: failed to create parent directory for output: " + outputPath.parent_path().string());
        exit(1);
      }
    }

    // Create tar writer.
    auto sink = std::make_unique<oro::runtime::tar::FileSink>(outputPath.string());
    if (!sink->ok()) {
      logError("update-bundle: failed to open output archive for writing: " + outputPath.string());
      exit(1);
    }

    oro::runtime::tar::ArchiveWriter writer(std::move(sink));

    const auto now = static_cast<uint64_t>(std::time(nullptr));

    for (auto it = fs::recursive_directory_iterator(inputPath, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (ec) {
        logError("update-bundle: error while traversing input directory: " + ec.message());
        exit(1);
      }

      const auto& entry = *it;
      const auto& path = entry.path();

      if (fs::is_directory(path, ec)) {
        // Ensure directory entries are present so empty dirs are preserved.
        auto rel = fs::relative(path, inputPath, ec);
        if (ec) continue;
        auto relStr = rel.generic_string();
        if (relStr.empty()) continue;
        if (relStr.back() != '/') relStr.push_back('/');

        const uint64_t size = 0;
        const uint32_t mode = 0755;
        const uint64_t mtime = now;
        if (!writer.beginEntry(relStr, size, mode, mtime, '5')) {
          logError("update-bundle: failed to begin directory entry: " + relStr);
          exit(1);
        }
        if (!writer.endEntry()) {
          logError("update-bundle: failed to finalize directory entry: " + relStr);
          exit(1);
        }
        continue;
      }

      if (!fs::is_regular_file(path, ec)) {
        continue;
      }

      auto rel = fs::relative(path, inputPath, ec);
      if (ec) continue;
      const auto relStr = rel.generic_string();
      if (relStr.empty()) {
        continue;
      }

      const auto fileSize = static_cast<uint64_t>(fs::file_size(path, ec));
      if (ec) continue;

      uint32_t mode = 0644;
#if defined(_WIN32)
      mode = 0644;
#else
      struct stat st;
      if (::stat(path.c_str(), &st) == 0) {
        mode = static_cast<uint32_t>(st.st_mode & 0777);
      }
#endif

      uint64_t mtime = now;
      auto ftime = fs::last_write_time(path, ec);
      if (!ec) {
        using namespace std::chrono;
        const auto s = time_point_cast<seconds>(ftime).time_since_epoch().count();
        if (s > 0) mtime = static_cast<uint64_t>(s);
      }

      if (!writer.beginEntry(relStr, fileSize, mode, mtime, '0')) {
        logError("update-bundle: failed to begin file entry: " + relStr);
        exit(1);
      }

      std::ifstream file(path, std::ios::binary);
      if (!file) {
        logError("update-bundle: failed to open file for reading: " + path.string());
        exit(1);
      }

      std::vector<char> buffer(64 * 1024);
      uint64_t remaining = fileSize;
      while (remaining > 0 && file) {
        const auto toRead = static_cast<std::streamsize>(std::min<uint64_t>(remaining, buffer.size()));
        file.read(buffer.data(), toRead);
        const auto got = file.gcount();
        if (got <= 0) break;
        if (!writer.writeData(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(got))) {
          logError("update-bundle: failed to write tar entry data for: " + relStr);
          exit(1);
        }
        remaining -= static_cast<uint64_t>(got);
      }

      if (!writer.endEntry()) {
        logError("update-bundle: failed to finalize file entry: " + relStr);
        exit(1);
      }
    }

    if (!writer.finish()) {
      logError("update-bundle: failed to finalize archive");
      exit(1);
    }

    logInfo("wrote update bundle to " + outputPath.string());

    // Optionally update an existing manifest with a new target entry that
    // describes this bundle (length + hash + artifactUrl).
    String manifestPathStr = optionsWithValue["--manifest"];
    String manifestNameOverride = optionsWithValue["--manifest-name"];

    if (manifestPathStr.size() == 0 && manifestNameOverride.size() == 0 && env::get("ORO_UPDATE_MANIFEST_FILENAME").size() == 0) {
      // No manifest hints provided; skip manifest updates.
      exit(0);
    }

    if (manifestPathStr.size() == 0) {
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = env::get("ORO_UPDATE_MANIFEST_FILENAME");
      }
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = DEFAULT_UPDATE_MANIFEST_FILENAME;
      }
      manifestPathStr = manifestNameOverride;
    }

    fs::path manifestPath(manifestPathStr);
    if (!manifestPath.is_absolute()) {
      manifestPath = targetPath / manifestPath;
    }
    const String manifestBytes = readFile(manifestPath);
    if (manifestBytes.size() == 0) {
      logError("update-bundle: manifest file is empty or unreadable: " + manifestPath.string());
      exit(1);
    }

    JSON::Any manifestAny;
    try {
      manifestAny = JSON::parse(manifestBytes);
    } catch (const JSON::Error& error) {
      logError("update-bundle: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-bundle: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    }

    String validationError;
    if (!validateUpdateManifest(manifestAny, validationError)) {
      logError("update-bundle: " + validationError);
      exit(1);
    }

    auto& manifestObj = manifestAny.as<JSON::Object>();

    String channel = optionsWithValue["--channel"];
    if (channel.size() == 0) {
      channel = settings["update_channel"].size() > 0 ? settings["update_channel"] : "stable";
    }

    String version = settings["meta_version"].size() > 0 ? settings["meta_version"] : "1.0.0";
    String updateId = optionsWithValue["--update-id"];
    if (updateId.size() == 0) {
      updateId = channel + "-" + version;
    }

    // Compute bundle length.
    std::error_code bundleEc;
    auto bundleSize = fs::file_size(outputPath, bundleEc);
    if (bundleEc) {
      logError("update-bundle: failed to stat bundle for manifest update: " + outputPath.string());
      exit(1);
    }

    // Select hash algorithm for the bundle payload.
    String hashAlgorithm = optionsWithValue["--hash-algorithm"];
    if (hashAlgorithm.size() == 0) {
#if ORO_CLI_HAS_SODIUM
      hashAlgorithm = "sha256";
#else
      hashAlgorithm = "sha1";
#endif
    }
    hashAlgorithm = toLowerCase(hashAlgorithm);
    if (hashAlgorithm != "sha256" && hashAlgorithm != "sha1") {
      logError("update-bundle: --hash-algorithm must be 'sha256' or 'sha1'");
      exit(1);
    }

    String hashHex;
    {
      std::ifstream bundleFile(outputPath, std::ios::binary);
      if (!bundleFile) {
        logError("update-bundle: failed to open bundle for hashing: " + outputPath.string());
        exit(1);
      }

      std::vector<char> buf(64 * 1024);

      if (hashAlgorithm == "sha1") {
        crypto::SHA1 sha1Ctx;
        while (bundleFile) {
          bundleFile.read(buf.data(), static_cast<std::streamsize>(buf.size()));
          std::streamsize got = bundleFile.gcount();
          if (got <= 0) break;
          sha1Ctx.update(reinterpret_cast<const unsigned char*>(buf.data()), static_cast<size_t>(got));
        }
        hashHex = toLowerCase(sha1Ctx.str());
      } else if (hashAlgorithm == "sha256") {
#if !ORO_CLI_HAS_SODIUM
        logError("update-bundle: --hash-algorithm=sha256 requires libsodium; rebuild with libsodium or choose sha1");
        exit(1);
#else
        if (sodium_init() < 0) {
          logError("update-bundle: unable to initialize libsodium for hashing");
          exit(1);
        }

        crypto_generichash_state state;
        if (crypto_generichash_init(&state, nullptr, 0, 32) != 0) {
          logError("update-bundle: crypto_generichash_init failed");
          exit(1);
        }

        while (bundleFile) {
          bundleFile.read(buf.data(), static_cast<std::streamsize>(buf.size()));
          std::streamsize got = bundleFile.gcount();
          if (got <= 0) break;
          if (crypto_generichash_update(
                &state,
                reinterpret_cast<const unsigned char*>(buf.data()),
                static_cast<unsigned long long>(got)
              ) != 0) {
            logError("update-bundle: crypto_generichash_update failed");
            exit(1);
          }
        }

        unsigned char out[32];
        if (crypto_generichash_final(&state, out, sizeof(out)) != 0) {
          logError("update-bundle: crypto_generichash_final failed");
          exit(1);
        }

        Vector<uint8_t> digest(out, out + sizeof(out));
        hashHex = toLowerCase(oro::runtime::bytes::encodeHexString(digest));
#endif
      }
    }

    String artifactUrl = optionsWithValue["--artifact-url"];
    if (artifactUrl.size() == 0) {
      artifactUrl = outputPath.filename().string();
    }

    // Locate or create the update entry.
    auto& updates = manifestObj.get("updates").as<JSON::Array>();
    JSON::Object* targetUpdate = nullptr;

    for (size_t i = 0; i < updates.size(); i++) {
      auto& uAny = updates.get(static_cast<unsigned int>(i));
      if (!uAny.isObject()) continue;
      auto& uObj = uAny.as<JSON::Object>();
      if (uObj.contains("id") && uObj.get("id").isString() && uObj.get("id").str() == updateId) {
        targetUpdate = &uObj;
        break;
      }
      if (uObj.contains("version") && uObj.contains("channel") &&
          uObj.get("version").isString() && uObj.get("channel").isString() &&
          uObj.get("version").str() == version && uObj.get("channel").str() == channel) {
        targetUpdate = &uObj;
        break;
      }
    }

    if (targetUpdate == nullptr) {
      JSON::Object::Entries newUpdateEntries {
        { "id", updateId },
        { "version", version },
        { "channel", channel },
        { "critical", false },
        { "targets", JSON::Array() }
      };
      JSON::Object newUpdate(newUpdateEntries);
      updates.push(newUpdate);
      // updates.get(updates.size() - 1) now refers to newUpdate.
      auto& anyRef = updates.get(static_cast<unsigned int>(updates.size() - 1));
      targetUpdate = &anyRef.as<JSON::Object>();
    }

    auto& targets = targetUpdate->get("targets").as<JSON::Array>();

    String platformName = optionsWithValue["--platform"];
    if (platformName.size() == 0) {
      platformName = "source";
    }
    String archName = optionsWithValue["--arch"];
    if (archName.size() == 0) {
      archName = "any";
    }

    JSON::Object::Entries targetEntries {
      { "platform", platformName },
      { "arch", archName },
      { "artifactUrl", artifactUrl },
      { "length", static_cast<uint64_t>(bundleSize) },
      { "hashAlgorithm", hashAlgorithm },
      { "hash", hashHex }
    };

    JSON::Object targetObj(targetEntries);
    targets.push(targetObj);

    writeFile(manifestPath, prettyJson(manifestAny.str()) + "\n");
    logInfo("update-bundle: updated manifest '" + manifestPath.string() + "' with new bundle target for update '" + updateId + "'");
    exit(0);
  });

  CommandLineOptions updateExtractOptions = {
    { { "--bundle" }, true, true },
    { { "--dest" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-extract", updateExtractOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    const String bundlePathStr = optionsWithValue["--bundle"];
    const String destPathStr = optionsWithValue["--dest"];

    if (bundlePathStr.size() == 0 || destPathStr.size() == 0) {
      logError("update-extract: --bundle=<file> and --dest=<dir> are required");
      exit(1);
    }

    fs::path bundlePath(bundlePathStr);
    fs::path destPath(destPathStr);

    std::error_code ec;
    if (!fs::exists(bundlePath, ec) || !fs::is_regular_file(bundlePath, ec)) {
      logError("update-extract: bundle path is not a file: " + bundlePath.string());
      exit(1);
    }

    // Ensure destination directory exists.
    fs::create_directories(destPath, ec);
    if (ec) {
      logError("update-extract: failed to create destination directory: " + destPath.string());
      exit(1);
    }

    // Open tar archive for reading.
    auto source = std::make_unique<oro::runtime::tar::FileSource>(
      bundlePath.string(),
      false
    );

    if (!source->ok()) {
      logError("update-extract: failed to open bundle archive: " + bundlePath.string());
      exit(1);
    }

    oro::runtime::tar::ArchiveReader reader(std::move(source));
    if (!reader.buildIndex()) {
      logError("update-extract: failed to parse tar archive: " + bundlePath.string());
      exit(1);
    }

    const auto& entries = reader.entries();

    auto isSafeTarPath = [](const String& path) -> bool {
      if (path.size() == 0) return false;
      if (path[0] == '/') return false;
      if (path.find('\\') != String::npos) return false;
      auto parts = split(path, '/');
      for (const auto& part : parts) {
        if (part == ".." || part.find(':') != String::npos) {
          return false;
        }
      }
      return true;
    };

    for (const auto& entry : entries) {
      if (!isSafeTarPath(entry.path)) {
        logError("update-extract: refusing to extract unsafe path in archive: " + entry.path);
        exit(1);
      }

      fs::path outPath = destPath / fs::path(entry.path).make_preferred();

      if (entry.isDirectory()) {
        fs::create_directories(outPath, ec);
        if (ec) {
          logError("update-extract: failed to create directory: " + outPath.string());
          exit(1);
        }
        continue;
      }

      if (!entry.isFile()) {
        // Skip special entries (symlinks, devices, etc.) for now.
        continue;
      }

      if (entry.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        logError("update-extract: archive entry is too large to extract safely: " + entry.path);
        exit(1);
      }

      const auto parent = outPath.parent_path();
      if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
          logError("update-extract: failed to create parent directory: " + parent.string());
          exit(1);
        }
      }

      Vector<uint8_t> buffer;
      if (!reader.read(entry, 0, static_cast<size_t>(entry.size), buffer)) {
        logError("update-extract: failed to read archive entry: " + entry.path);
        exit(1);
      }

      std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
      if (!file) {
        logError("update-extract: failed to open output file for writing: " + outPath.string());
        exit(1);
      }

      if (!buffer.empty()) {
        file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
      }
      file.close();
    }

    logInfo("extracted update bundle '" + bundlePath.string() + "' to '" + destPath.string() + "'");
    exit(0);
  });

  CommandLineOptions updateVerifyOptions = {
    { { "--manifest" }, true, true },
    { { "--manifest-name" }, true, true },
    { { "--signature" }, true, true },
    { { "--keys" }, true, true },
    { { "--public-key" }, true, true },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-verify", updateVerifyOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
#if !ORO_CLI_HAS_SODIUM
    logError("update-verify requires libsodium; rebuild with libsodium headers and libraries available.");
    exit(1);
#else
    if (sodium_init() < 0) {
      logError("update-verify: unable to initialize libsodium");
      exit(1);
    }

    String manifestPath = optionsWithValue["--manifest"];
    String signaturePath = optionsWithValue["--signature"];
    String manifestNameOverride = optionsWithValue["--manifest-name"];
    const String keysPath = optionsWithValue["--keys"];
    const String publicKeyArg = optionsWithValue["--public-key"];

    if (manifestPath.size() == 0) {
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = env::get("ORO_UPDATE_MANIFEST_FILENAME");
      }
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = DEFAULT_UPDATE_MANIFEST_FILENAME;
      }
      manifestPath = manifestNameOverride;
    }

    if (signaturePath.size() == 0) {
      fs::path p(manifestPath);
      const auto stem = p.stem().string();
      signaturePath = (p.parent_path() / (stem + ".sig")).string();
    }

    if (keysPath.size() == 0 && publicKeyArg.size() == 0) {
      logError("update-verify: expected --keys=<file> or --public-key=<hex>");
      exit(1);
    }

    const String manifestBytes = readFile(manifestPath);
    if (manifestBytes.size() == 0) {
      logError("update-verify: manifest file is empty or unreadable: " + manifestPath);
      exit(1);
    }

    // Validate manifest structure against the update manifest schema shape.
    try {
      JSON::Any manifestAny = JSON::parse(manifestBytes);
      String validationError;
      if (!validateUpdateManifest(manifestAny, validationError)) {
        logError("update-verify: " + validationError);
        exit(1);
      }
    } catch (const JSON::Error& error) {
      logError("update-verify: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-verify: manifest is not valid JSON: " + String(error.what()));
      exit(1);
    }

    const String signatureText = readFile(signaturePath);
    if (signatureText.size() == 0) {
      logError("update-verify: signature file is empty or unreadable: " + signaturePath);
      exit(1);
    }

    Vector<uint8_t> publicKey;

    if (keysPath.size() > 0) {
      const String jsonText = readFile(keysPath);
      if (jsonText.size() == 0) {
        logError("update-verify: keys file is empty or unreadable: " + keysPath);
        exit(1);
      }

      try {
      auto any = JSON::parse(jsonText);
      if (!any.isObject()) {
        logError("update-verify: keys file must contain a JSON object");
        exit(1);
      }

      auto& obj = any.as<JSON::Object>();
        String pkHex;
        if (obj.contains("publicKey")) {
          pkHex = obj.at("publicKey").str();
        } else if (obj.contains("key")) {
          pkHex = obj.at("key").str();
        }

        if (pkHex.size() == 0) {
          logError("update-verify: keys file must include 'publicKey' or 'key'");
          exit(1);
        }

        const String decoded = oro::runtime::bytes::decodeHexString(pkHex);
        publicKey.assign(decoded.begin(), decoded.end());
      } catch (const JSON::Error& error) {
        logError("update-verify: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      } catch (const std::exception& error) {
        logError("update-verify: failed to parse keys file as JSON: " + String(error.what()));
        exit(1);
      }
    } else {
      const String decoded = oro::runtime::bytes::decodeHexString(publicKeyArg);
      publicKey.assign(decoded.begin(), decoded.end());
    }

    if (publicKey.size() != crypto_sign_PUBLICKEYBYTES) {
      logError("update-verify: public key must be an Ed25519 public key (hex-encoded)");
      exit(1);
    }

    // Parse signature JSON
    String sigHex;
    try {
      auto any = JSON::parse(signatureText);
      if (!any.isObject()) {
        logError("update-verify: signature file must contain a JSON object");
        exit(1);
      }

      auto& obj = any.as<JSON::Object>();
      if (obj.contains("algorithm") && obj.at("algorithm").str() != "ed25519") {
        logError("update-verify: unsupported signature algorithm; expected 'ed25519'");
        exit(1);
      }

      if (!obj.contains("signature")) {
        logError("update-verify: signature file missing 'signature' field");
        exit(1);
      }

      sigHex = obj.at("signature").str();
    } catch (const JSON::Error& error) {
      logError("update-verify: failed to parse signature file as JSON: " + String(error.what()));
      exit(1);
    } catch (const std::exception& error) {
      logError("update-verify: failed to parse signature file as JSON: " + String(error.what()));
      exit(1);
    }

    const String sigDecoded = oro::runtime::bytes::decodeHexString(sigHex);
    Vector<uint8_t> signature(sigDecoded.begin(), sigDecoded.end());

    if (signature.size() != crypto_sign_BYTES) {
      logError("update-verify: signature length does not match Ed25519 detached signature size");
      exit(1);
    }

    const int ok = crypto_sign_verify_detached(
      signature.data(),
      reinterpret_cast<const unsigned char*>(manifestBytes.data()),
      manifestBytes.size(),
      publicKey.data()
    );

    if (ok != 0) {
      logError("Signature verification FAILED");
      exit(1);
    }

    logInfo("Signature is VALID for this manifest and public key");
    exit(0);
#endif
  });

  CommandLineOptions updateValidateOptions = {
    { { "--manifest" }, true, true },
    { { "--manifest-name" }, true, true },
    { { "--strict" }, true, false },
    { { "--json" }, true, false },
    { { "--log-file" }, true, true }
  };

  createSubcommand("update-validate", updateValidateOptions, false, [&](Map<> optionsWithValue, std::unordered_set<String> optionsWithoutValue) -> void {
    const bool strict = optionsWithoutValue.find("--strict") != optionsWithoutValue.end();
    const bool jsonMode = optionsWithoutValue.find("--json") != optionsWithoutValue.end();

    if (jsonMode && gLogJson) {
      logError(
        "update-validate: structured command output cannot be combined with global JSON logs. "
        "Unset ORO_LOG_JSON or remove the global '--json' flag, and use "
        "'--log-file=<path>' if you also need logs." + helpHint("update-validate")
      );
      exit(1);
    }

    String manifestPath = optionsWithValue["--manifest"];
    String manifestNameOverride = optionsWithValue["--manifest-name"];

    auto fail = [&](const String& errorText) -> void {
      if (jsonMode) {
        JSON::Object::Entries entries {
          { "ok", false },
          { "manifest", manifestPath },
          { "strict", strict },
          { "error", errorText }
        };
        JSON::Object obj(entries);
        std::cout << obj.str() << std::endl;
        exit(1);
      }
      logError("update-validate: " + errorText);
      exit(1);
    };

    if (manifestPath.size() == 0) {
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = env::get("ORO_UPDATE_MANIFEST_FILENAME");
      }
      if (manifestNameOverride.size() == 0) {
        manifestNameOverride = DEFAULT_UPDATE_MANIFEST_FILENAME;
      }
      manifestPath = manifestNameOverride;
    }

    const String manifestBytes = readFile(manifestPath);
    if (manifestBytes.size() == 0) {
      fail("manifest file is empty or unreadable: " + manifestPath);
    }

    try {
      JSON::Any manifestAny = JSON::parse(manifestBytes);
      String validationError;
      const bool ok = strict
        ? validateUpdateManifestStrict(manifestAny, validationError)
        : validateUpdateManifest(manifestAny, validationError);
      if (!ok) {
        fail(validationError);
      }
    } catch (const JSON::Error& error) {
      fail(String("manifest is not valid JSON: ") + String(error.what()));
    } catch (const std::exception& error) {
      fail(String("manifest is not valid JSON: ") + String(error.what()));
    }

    if (jsonMode) {
      JSON::Object::Entries entries {
        { "ok", true },
        { "manifest", manifestPath },
        { "strict", strict }
      };
      JSON::Object obj(entries);
      std::cout << obj.str() << std::endl;
      exit(0);
    }

    logInfo("update-validate: manifest '" + manifestPath + "' is structurally valid");
    exit(0);
  });

    logError("subcommand '" + String(gCliDisplayName) + " " + effectiveSubcommand + "' is not supported.");
    printHelp(gCliDisplayName);
    exit(1);
  } catch (const std::exception& error) {
    logError("unexpected internal error: " + String(error.what()));
    return 1;
  } catch (...) {
    logError("unexpected internal error.");
    return 1;
  }
}
