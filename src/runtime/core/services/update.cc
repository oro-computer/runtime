#include "update.hh"

#include "../../debug.hh"
#include "../../http.hh"
#include "../../json.hh"
#include "../../string.hh"
#include "../../url.hh"
#include "../../crypto.hh"
#include "../../semver.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <cstring>

#if __has_include(<sodium.h>)
#include <sodium.h>
#define ORO_RUNTIME_HAS_SODIUM 1
#else
#define ORO_RUNTIME_HAS_SODIUM 0
#endif

#if __has_include(<cpp-httplib/httplib.h>)
#include <cpp-httplib/httplib.h>
#define ORO_RUNTIME_HAS_CPPHTTPLIB 1
#else
#define ORO_RUNTIME_HAS_CPPHTTPLIB 0
#endif

using oro::runtime::bytes::decodeHexString;
using oro::runtime::bytes::base64::decode;
using oro::runtime::string::split;
using oro::runtime::string::toLowerCase;
using oro::runtime::string::trim;

namespace oro::runtime::core::services {
  namespace {
    static inline JSON::Object::Entries makeError (
      const char* source,
      const String& code,
      const String& message
    ) {
      return JSON::Object::Entries {
        {"source", source},
        {"err", JSON::Object::Entries {
          {"code", code},
          {"message", message}
        }}
      };
    }

    static inline JSON::Object::Entries makeError (
      const char* source,
      const String& code,
      const String& message,
      const JSON::Object::Entries& extras
    ) {
      JSON::Object::Entries err {
        {"code", code},
        {"message", message}
      };

      for (const auto& entry : extras) {
        err.emplace(entry);
      }

      return JSON::Object::Entries {
        {"source", source},
        {"err", err}
      };
    }

    static inline bool isHex (const String& value) {
      if (value.size() == 0 || (value.size() % 2) != 0) {
        return false;
      }

      for (const auto ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
          return false;
        }
      }

      return true;
    }

    static bool decodeEncodedStringToBytes (
      const String& input,
      Vector<uint8_t>& out,
      String& encoding
    ) {
      auto value = trim(input);
      if (value.size() == 0) {
        return false;
      }

      if (isHex(value)) {
        const auto decoded = decodeHexString(value);
        out.assign(decoded.begin(), decoded.end());
        encoding = "hex";
        return true;
      }

      String normalised = value;
      bool isURL = false;

      for (auto& ch : normalised) {
        if (ch == '-') {
          ch = '+';
          isURL = true;
        } else if (ch == '_') {
          ch = '/';
          isURL = true;
        }
      }

      while ((normalised.size() % 4) != 0) {
        normalised.push_back('=');
      }

      const auto decoded = decode(normalised);
      if (!value.empty() && decoded.empty()) {
        return false;
      }

      out.assign(decoded.begin(), decoded.end());
      encoding = isURL ? "base64url" : "base64";
      return true;
    }

    static int compareSemVer (const String& a, const String& b) {
      bool ok = false;
      auto cmp = oro::runtime::semver::compare(a, b, &ok);

      if (!ok) {
        // Fall back to a conservative comparison that treats unparsable
        // versions as equal. This preserves the defined behavior where non-SemVer
        // version strings were effectively treated as "0.0.0" and avoids
        // rejecting manifests outright when a single descriptor is malformed.
        return 0;
      }

      if (cmp == oro::runtime::semver::Compare::Less) {
        return -1;
      }
      if (cmp == oro::runtime::semver::Compare::Greater) {
        return 1;
      }
      return 0;
    }

    static String normalizeHashAlgorithm (const String& algorithmRaw) {
      if (algorithmRaw.empty()) {
        return "sha256";
      }

      auto upper = algorithmRaw;
      std::transform(
        upper.begin(),
        upper.end(),
        upper.begin(),
        [](unsigned char c) {
          return static_cast<char>(std::toupper(c));
        }
      );

      if (upper == "SHA256" || upper == "SHA-256") {
        return "sha256";
      }

      // Future: support sha384 / sha512 once exposed consistently.
      return "";
    }

    static bool computeDigest (
      const String& algorithm,
      const Vector<uint8_t>& bytes,
      Vector<uint8_t>& out
    ) {
#if !ORO_RUNTIME_HAS_SODIUM
      (void) algorithm;
      (void) bytes;
      (void) out;
      return false;
#else
      const auto normalised = normalizeHashAlgorithm(algorithm);
      if (normalised != "sha256") {
        return false;
      }

      out.resize(crypto_hash_sha256_BYTES);
      if (crypto_hash_sha256(out.data(), bytes.data(), bytes.size()) != 0) {
        return false;
      }

      return true;
#endif
    }

    static bool httpGetToBytes (
      const char* source,
      const String& urlString,
      const Vector<std::pair<String, String>>& extraHeaders,
      uint64_t maxBytes,
      Vector<uint8_t>& out,
      JSON::Object::Entries& errorOut
    ) {
      const auto url = oro::runtime::URL(urlString);
      if (url.scheme.size() == 0) {
        errorOut = makeError(
          source,
          "ERR_URL",
          "Manifest or artifact URL is missing a scheme"
        );
        return false;
      }

      const auto scheme = toLowerCase(url.scheme);
      if (scheme != "http" && scheme != "https") {
        errorOut = makeError(
          source,
          "ERR_URL",
          "Only http and https URLs are supported"
        );
        return false;
      }

      if (url.hostname.size() == 0) {
        errorOut = makeError(
          source,
          "ERR_URL",
          "URL is missing hostname"
        );
        return false;
      }

#if !ORO_RUNTIME_HAS_CPPHTTPLIB
      errorOut = makeError(
        source,
        "ERR_HTTP_UNAVAILABLE",
        "HTTP client support is not available in this build"
      );
      return false;
#else
      int port = 0;
      if (url.port.size() > 0) {
        try {
          port = std::stoi(url.port);
        } catch (...) {
          port = 0;
        }
      }

      if (port <= 0) {
        port = scheme == "https" ? 443 : 80;
      }

      String path = url.pathname;
      if (!url.search.empty()) {
        path += url.search;
      }

      httplib::Headers headers;
      for (const auto& kv : extraHeaders) {
        headers.emplace(kv.first, kv.second);
      }

      httplib::Result res;

      if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient client(url.hostname.c_str(), port);
        client.enable_server_certificate_verification(true);
        client.set_follow_location(true);
        res = client.Get(path.c_str(), headers);
#else
        errorOut = makeError(
          source,
          "ERR_HTTPS_UNAVAILABLE",
          "HTTPS support is not enabled in this build"
        );
        return false;
#endif
      } else {
        httplib::Client client(url.hostname.c_str(), port);
        client.set_follow_location(true);
        res = client.Get(path.c_str(), headers);
      }

      if (!res) {
        errorOut = makeError(
          source,
          "ERR_HTTP_REQUEST",
          "HTTP request failed"
        );
        return false;
      }

      const auto status = res->status;
      if (status < 200 || status >= 300) {
        JSON::Object::Entries extras {
          {"status", std::to_string(status)}
        };
        errorOut = makeError(
          source,
          "ERR_HTTP_STATUS",
          "HTTP request failed with non-success status code",
          extras
        );
        return false;
      }

      const auto lengthHeader = res->get_header_value("Content-Length");
      if (maxBytes > 0 && !lengthHeader.empty()) {
        try {
          const auto declared = std::stoull(lengthHeader);
          if (declared > maxBytes) {
            JSON::Object::Entries extras {
              {"length", std::to_string(declared)},
              {"maxBytes", std::to_string(maxBytes)}
            };
            errorOut = makeError(
              source,
              "ERR_CONTENT_TOO_LARGE",
              "HTTP response exceeds configured maximum size",
              extras
            );
            return false;
          }
        } catch (...) {
          // ignore parse errors; we will enforce using body size instead
        }
      }

      const auto& body = res->body;
      if (maxBytes > 0 && body.size() > maxBytes) {
        JSON::Object::Entries extras {
          {"length", std::to_string(body.size())},
          {"maxBytes", std::to_string(maxBytes)}
        };
        errorOut = makeError(
          source,
          "ERR_CONTENT_TOO_LARGE",
          "HTTP response exceeds configured maximum size",
          extras
        );
        return false;
      }

      out.assign(body.begin(), body.end());
      return true;
#endif
    }
  }

  void Update::check (
    const String& seq,
    const ManifestCheckOptions& manifest,
    const SelectionOptions& selection,
    const DownloadOptions& download,
    const Callback callback
  ) {
    (void) download;

    this->queue.push([=, this]() {
      const char* source = "application.update.check";

      if (manifest.manifestUrl.size() == 0) {
        auto json = makeError(
          source,
          "ERR_MANIFEST_URL_REQUIRED",
          "manifestUrl is required"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (manifest.publicKeys.empty()) {
        auto json = makeError(
          source,
          "ERR_PUBLIC_KEY_REQUIRED",
          "publicKey or publicKeys is required"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

#if !ORO_RUNTIME_HAS_SODIUM
      auto json = makeError(
        source,
        "ERR_SODIUM_UNAVAILABLE",
        "libsodium is not available in this build"
      );
      this->loop.dispatch([=, this]() {
        callback(seq, json, QueuedResponse{});
      });
      return;
#else
      if (sodium_init() < 0) {
        auto json = makeError(
          source,
          "ERR_SODIUM_INIT",
          "Unable to initialize libsodium"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }
#endif

      Vector<uint8_t> manifestBytes;
      Vector<uint8_t> signatureBytes;
      JSON::Object::Entries error;

      const auto signatureUrl = manifest.signatureUrl.size() > 0
        ? manifest.signatureUrl
        : manifest.manifestUrl + ".sig";

      if (!httpGetToBytes(
            source,
            manifest.manifestUrl,
            manifest.headers,
            manifest.maxManifestBytes,
            manifestBytes,
            error
          )) {
        if (error.size() > 0) {
          JSON::Object errObj(error);
          if (errObj.has("err")) {
            auto& inner = errObj.get("err").as<JSON::Object>();
            if (inner.has("code") &&
                inner.get("code").str() == "ERR_CONTENT_TOO_LARGE") {
              inner.set("code", JSON::Any("ERR_MANIFEST_TOO_LARGE"));
            }
          }
          this->loop.dispatch([=, this]() {
            callback(seq, JSON::Any(errObj), QueuedResponse{});
          });
          return;
        }

        this->loop.dispatch([=, this]() {
          callback(seq, JSON::Any(JSON::Object(error)), QueuedResponse{});
        });
        return;
      }

      if (!httpGetToBytes(
            source,
            signatureUrl,
            manifest.headers,
            0,
            signatureBytes,
            error
          )) {
        this->loop.dispatch([=, this]() {
          callback(seq, JSON::Any(JSON::Object(error)), QueuedResponse{});
        });
        return;
      }

      if (manifest.maxManifestBytes > 0 &&
          manifestBytes.size() > manifest.maxManifestBytes) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_MANIFEST_TOO_LARGE",
          "Manifest exceeds maxManifestBytes limit",
          JSON::Object::Entries {
            {"length", std::to_string(manifestBytes.size())},
            {"maxManifestBytes", std::to_string(manifest.maxManifestBytes)}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const String manifestText(
        reinterpret_cast<const char*>(manifestBytes.data()),
        manifestBytes.size()
      );

      const String signatureText(
        reinterpret_cast<const char*>(signatureBytes.data()),
        signatureBytes.size()
      );

      JSON::Any signatureAny;
      try {
        signatureAny = JSON::parse(signatureText);
      } catch (const JSON::Error& e) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Invalid manifest signature JSON",
          JSON::Object::Entries {
            {"detail", String(e.what())}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
      } catch (const std::exception& e) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Invalid manifest signature JSON",
          JSON::Object::Entries {
            {"detail", String(e.what())}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (signatureAny.type != JSON::Type::Object) {
        auto json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Manifest signature JSON must be an object"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      auto& sigObj = signatureAny.as<JSON::Object>();
      if (!sigObj.has("signature")) {
        auto json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Manifest signature JSON missing \"signature\" field"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const auto signatureValue = sigObj.get("signature").str();
      const auto algorithmRaw = sigObj.has("algorithm")
        ? sigObj.get("algorithm").str()
        : String("ed25519");
      auto algorithmLower = toLowerCase(algorithmRaw);

      if (algorithmLower != "ed25519") {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_UNSUPPORTED_SIGNATURE_ALGORITHM",
          "Unsupported manifest signature algorithm",
          JSON::Object::Entries {
            {"algorithm", algorithmRaw}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      Vector<uint8_t> signatureDecoded;
      String signatureEncoding;
      if (!decodeEncodedStringToBytes(
            signatureValue,
            signatureDecoded,
            signatureEncoding
          )) {
        auto json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Failed to decode manifest signature"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

#if ORO_RUNTIME_HAS_SODIUM
      if (signatureDecoded.size() != crypto_sign_BYTES) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_SIGNATURE_JSON",
          "Manifest signature length does not match Ed25519 detached signature size"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }
#endif

      Vector<Vector<uint8_t>> publicKeys;
      publicKeys.reserve(manifest.publicKeys.size());

      for (const auto& key : manifest.publicKeys) {
        Vector<uint8_t> decoded;
        String encoding;
        if (!decodeEncodedStringToBytes(key, decoded, encoding)) {
          auto json = makeError(
            source,
            "ERR_PUBLIC_KEY_DECODE",
            "Failed to decode public key"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }

#if ORO_RUNTIME_HAS_SODIUM
        if (decoded.size() != crypto_sign_PUBLICKEYBYTES) {
          auto json = makeError(
            source,
            "ERR_PUBLIC_KEY_DECODE",
            "Public key must be an Ed25519 public key"
          );
          this->loop.dispatch([=, this]() {
            callback(seq, json, QueuedResponse{});
          });
          return;
        }
#endif

        publicKeys.push_back(std::move(decoded));
      }

      bool verified = false;

#if ORO_RUNTIME_HAS_SODIUM
      for (const auto& pk : publicKeys) {
        const auto ok = crypto_sign_verify_detached(
          signatureDecoded.data(),
          manifestBytes.data(),
          manifestBytes.size(),
          pk.data()
        );
        if (ok == 0) {
          verified = true;
          break;
        }
      }
#endif

      if (!verified) {
        auto json = makeError(
          source,
          "ERR_SIGNATURE_VERIFICATION",
          "Manifest signature verification failed"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      JSON::Any manifestAny;
      try {
        manifestAny = JSON::parse(manifestText);
      } catch (const JSON::Error& e) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_MANIFEST_JSON",
          "Invalid manifest JSON",
          JSON::Object::Entries {
            {"detail", String(e.what())}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
      } catch (const std::exception& e) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_MANIFEST_JSON",
          "Invalid manifest JSON",
          JSON::Object::Entries {
            {"detail", String(e.what())}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (manifestAny.type != JSON::Type::Object) {
        auto json = makeError(
          source,
          "ERR_MANIFEST_JSON",
          "Manifest JSON must be an object"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      auto& manifestObj = manifestAny.as<JSON::Object>();

      if (!manifestObj.has("schemaVersion") ||
          manifestObj.get("schemaVersion").type != JSON::Type::Number) {
        auto json = makeError(
          source,
          "ERR_MANIFEST_SCHEMA",
          "Manifest is missing numeric schemaVersion"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const auto schemaVersion =
        manifestObj.get("schemaVersion").as<JSON::Number>().value();
      if (schemaVersion != 1) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_MANIFEST_SCHEMA",
          "Unsupported manifest schemaVersion",
          JSON::Object::Entries {
            {"schemaVersion", std::to_string(static_cast<int64_t>(schemaVersion))}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (!manifestObj.has("appId") ||
          manifestObj.get("appId").type != JSON::Type::String ||
          manifestObj.get("appId").str().empty()) {
        auto json = makeError(
          source,
          "ERR_MANIFEST_SCHEMA",
          "Manifest is missing non-empty appId"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const auto appId = manifestObj.get("appId").str();
      if (manifest.expectedAppId.size() > 0 &&
          appId != manifest.expectedAppId) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_APP_ID_MISMATCH",
          "Manifest appId mismatch",
          JSON::Object::Entries {
            {"expectedAppId", manifest.expectedAppId},
            {"appId", appId}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (!manifestObj.has("updates") ||
          manifestObj.get("updates").type != JSON::Type::Array) {
        auto json = JSON::Object::Entries {
          {"source", source},
          {"data", JSON::Object::Entries {
            {"updateAvailable", false},
            {"manifest", manifestObj},
            {"signature", JSON::Object::Entries {
              {"schemaVersion", sigObj.has("schemaVersion")
                ? sigObj.get("schemaVersion")
                : JSON::Any(1)
              },
              {"algorithm", algorithmRaw},
              {"keyId", sigObj.has("keyId") ? sigObj.get("keyId") : JSON::Any()},
              {"encoding", signatureEncoding},
              {"signature", signatureValue}
            }}
          }}
        };

        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      auto& updatesArray = manifestObj.get("updates").as<JSON::Array>();

      const auto channel = selection.channel.size() > 0
        ? selection.channel
        : String("stable");
      const auto platform = selection.platform;
      const auto arch = selection.arch;
      const auto runtimeVersion = selection.runtimeVersion;
      const auto currentVersion = selection.currentVersion;

      struct Candidate {
        size_t updateIndex = 0;
        size_t targetIndex = 0;
        String version;
        bool critical = false;
        bool valid = false;
      };

      Candidate best;

      for (size_t i = 0; i < updatesArray.size(); ++i) {
        auto& updateAny = updatesArray[i];
        if (updateAny.type != JSON::Type::Object) {
          continue;
        }

        auto& updateObj = updateAny.as<JSON::Object>();

        const auto updateChannel = updateObj.has("channel")
          ? updateObj.get("channel").str()
          : String();

        if (updateChannel.size() > 0 && updateChannel != channel) {
          continue;
        }

        if (!updateObj.has("targets") ||
            updateObj.get("targets").type != JSON::Type::Array) {
          continue;
        }

        auto& targetsArray = updateObj.get("targets").as<JSON::Array>();
        if (targetsArray.size() == 0) {
          continue;
        }

        Vector<size_t> matchingTargets;
        matchingTargets.reserve(targetsArray.size());

        for (size_t j = 0; j < targetsArray.size(); ++j) {
          auto& targetAny = targetsArray[j];
          if (targetAny.type != JSON::Type::Object) {
            continue;
          }

          auto& targetObj = targetAny.as<JSON::Object>();

          const auto targetPlatform = targetObj.has("platform")
            ? targetObj.get("platform").str()
            : String();
          const auto targetArch = targetObj.has("arch")
            ? targetObj.get("arch").str()
            : String();

          if (platform.size() > 0 &&
              targetPlatform.size() > 0 &&
              targetPlatform != platform) {
            continue;
          }

          if (arch.size() > 0 &&
              targetArch.size() > 0 &&
              targetArch != arch) {
            continue;
          }

          matchingTargets.push_back(j);
        }

        if (matchingTargets.empty()) {
          continue;
        }

        const auto minRuntimeVersion = updateObj.has("minRuntimeVersion")
          ? updateObj.get("minRuntimeVersion").str()
          : String();

        if (minRuntimeVersion.size() > 0 &&
            runtimeVersion.size() > 0 &&
            compareSemVer(runtimeVersion, minRuntimeVersion) < 0) {
          continue;
        }

        const auto version = updateObj.has("version")
          ? updateObj.get("version").str()
          : String("0.0.0");

        if (currentVersion.size() > 0 &&
            compareSemVer(version, currentVersion) <= 0) {
          continue;
        }

        const bool critical = updateObj.has("critical") &&
          updateObj.get("critical").type == JSON::Type::Boolean &&
          updateObj.get("critical").as<JSON::Boolean>().value();

        if (!best.valid) {
          best.updateIndex = i;
          best.targetIndex = matchingTargets[0];
          best.version = version;
          best.critical = critical;
          best.valid = true;
        } else {
          const auto cmp = compareSemVer(version, best.version);
          if (cmp > 0 ||
              (cmp == 0 && critical && !best.critical)) {
            best.updateIndex = i;
            best.targetIndex = matchingTargets[0];
            best.version = version;
            best.critical = critical;
          }
        }
      }

      JSON::Object::Entries signatureOut {
        {"schemaVersion", sigObj.has("schemaVersion")
          ? sigObj.get("schemaVersion")
          : JSON::Any(1)
        },
        {"algorithm", algorithmRaw},
        {"keyId", sigObj.has("keyId") ? sigObj.get("keyId") : JSON::Any()},
        {"encoding", signatureEncoding},
        {"signature", signatureValue}
      };

      JSON::Object::Entries data;
      data.emplace("updateAvailable", best.valid);
      data.emplace("manifest", manifestObj);
      data.emplace("signature", signatureOut);

      if (best.valid) {
        auto& updateAny = updatesArray[best.updateIndex];
        auto& updateObj = updateAny.as<JSON::Object>();
        auto& targetsArray = updateObj.get("targets").as<JSON::Array>();
        auto& targetAny = targetsArray[best.targetIndex];

        data.emplace("update", updateObj);
        data.emplace("target", targetAny);
      }

      JSON::Object::Entries json {
        {"source", source},
        {"data", data}
      };

      this->loop.dispatch([=, this]() {
        callback(seq, json, QueuedResponse{});
      });
      return;
    });
  }

  void Update::download (
    const String& seq,
    const String& artifactUrl,
    const String& hashAlgorithm,
    const String& hash,
    uint64_t expectedLength,
    const DownloadOptions& options,
    const Callback callback
  ) {
    this->queue.push([=, this]() {
      const char* source = "application.update.download";

      if (artifactUrl.size() == 0) {
        auto json = makeError(
          source,
          "ERR_ARTIFACT_URL",
          "artifactUrl is required"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (hash.size() == 0) {
        auto json = makeError(
          source,
          "ERR_ARTIFACT_HASH",
          "Target is missing hash"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

#if !ORO_RUNTIME_HAS_SODIUM
      auto json = makeError(
        source,
        "ERR_SODIUM_UNAVAILABLE",
        "libsodium is not available in this build"
      );
      this->loop.dispatch([=, this]() {
        callback(seq, json, QueuedResponse{});
      });
      return;
#else
      if (sodium_init() < 0) {
        auto json = makeError(
          source,
          "ERR_SODIUM_INIT",
          "Unable to initialize libsodium"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }
#endif

      Vector<uint8_t> artifactBytes;
      JSON::Object::Entries error;

      Vector<std::pair<String, String>> headers;

      if (!httpGetToBytes(
            source,
            artifactUrl,
            headers,
            options.maxArtifactBytes,
            artifactBytes,
            error
          )) {
        // Map generic size error into artifact-specific codes for JS parity.
        if (error.size() > 0) {
          JSON::Object errObj(error);
          if (errObj.has("err")) {
            auto& inner = errObj.get("err").as<JSON::Object>();
            if (inner.has("code") &&
                inner.get("code").str() == "ERR_CONTENT_TOO_LARGE") {
              inner.set("code", JSON::Any("ERR_ARTIFACT_TOO_LARGE"));
            }
          }
          this->loop.dispatch([=, this]() {
            callback(seq, JSON::Any(errObj), QueuedResponse{});
          });
          return;
        }

        this->loop.dispatch([=, this]() {
          callback(seq, JSON::Any(JSON::Object(error)), QueuedResponse{});
        });
        return;
      }

      if (options.maxArtifactBytes > 0 &&
          artifactBytes.size() > options.maxArtifactBytes) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_ARTIFACT_TOO_LARGE",
          "Artifact exceeds maxArtifactBytes limit",
          JSON::Object::Entries {
            {"length", std::to_string(artifactBytes.size())},
            {"maxArtifactBytes", std::to_string(options.maxArtifactBytes)}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (expectedLength > 0 &&
          artifactBytes.size() != expectedLength) {
        JSON::Object::Entries json = makeError(
          source,
          "ERR_ARTIFACT_LENGTH",
          "Artifact length mismatch",
          JSON::Object::Entries {
            {"expected", std::to_string(expectedLength)},
            {"actual", std::to_string(artifactBytes.size())}
          }
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      Vector<uint8_t> digest;
      if (!computeDigest(hashAlgorithm, artifactBytes, digest)) {
        auto json = makeError(
          source,
          "ERR_HASH_ALGORITHM",
          "Unsupported hash algorithm"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      // Compare digest with declared hash (hex).
      Vector<uint8_t> expectedDigest;
      String digestEncoding;
      if (!decodeEncodedStringToBytes(hash, expectedDigest, digestEncoding)) {
        auto json = makeError(
          source,
          "ERR_ARTIFACT_HASH",
          "Failed to decode artifact hash"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      if (expectedDigest.size() != digest.size() ||
          !std::equal(
            digest.begin(),
            digest.end(),
            expectedDigest.begin()
          )) {
        auto json = makeError(
          source,
          "ERR_ARTIFACT_HASH",
          "Artifact hash verification failed"
        );
        this->loop.dispatch([=, this]() {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      const auto length = artifactBytes.size();
      auto shared = std::make_shared<unsigned char[]>(length);
      if (length > 0) {
        std::memcpy(shared.get(), artifactBytes.data(), length);
      }

      http::Headers responseHeaders;
      responseHeaders.set("content-type", "application/octet-stream");
      responseHeaders.set("content-length", static_cast<int64_t>(length));

      QueuedResponse queuedResponse;
      queuedResponse.id = oro::runtime::crypto::rand64();
      queuedResponse.body = shared;
      queuedResponse.length = length;
      queuedResponse.headers = responseHeaders.str();

      JSON::Object::Entries json {
        {"source", source},
        {"data", JSON::Object::Entries {}}
      };

      this->loop.dispatch([=, this]() {
        callback(seq, json, queuedResponse);
      });
      return;
    });
  }
}
