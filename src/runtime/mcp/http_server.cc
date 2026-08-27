#include "http_server.hh"

#include "../debug.hh"
#include "../string.hh"
#include "../url.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <uv.h>
#include <vector>

namespace oro::runtime::mcp {
  namespace {
    static constexpr size_t kMaxOAuthArtifacts = 4096;

    bool isValidSessionIdToken(const String& value) {
      if (value.empty()) return false;
      if (value.size() > 128) return false;
      for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_') continue;
        return false;
      }
      return true;
    }

    bool decodeMirroredHeader(const String& value, String& decoded) {
      static const String prefix = "=?base64?";
      static const String suffix = "?=";
      const bool hasPrefix = value.rfind(prefix, 0) == 0;
      const bool hasSuffix = value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
      if (!hasPrefix || !hasSuffix) {
        if (!value.empty() &&
            (value.front() == ' ' ||
             value.front() == '\t' ||
             value.back() == ' ' ||
             value.back() == '\t')) {
          return false;
        }
        for (const unsigned char character : value) {
          if ((character < 0x20 && character != '\t') || character > 0x7e) {
            return false;
          }
        }
        decoded = value;
        return true;
      }
      if (value.size() < prefix.size() + suffix.size()) {
        return false;
      }

      const auto encoded = value.substr(
        prefix.size(),
        value.size() - prefix.size() - suffix.size()
      );
      if (encoded.size() % 4 != 0) {
        return false;
      }
      for (size_t index = 0; index < encoded.size(); ++index) {
        const unsigned char character = encoded[index];
        const bool padding = character == '=';
        const bool alphaNumeric =
          (character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9');
        if (padding && index + 2 < encoded.size()) {
          return false;
        }
        if (!padding &&
            !alphaNumeric &&
            character != '+' &&
            character != '/') {
          return false;
        }
      }

      decoded = bytes::base64::decode(encoded);
      return bytes::base64::encode(decoded) == encoded;
    }

    bool numericHeaderMatches(const String& header, const String& expected) {
      try {
        size_t headerEnd = 0;
        size_t expectedEnd = 0;
        const auto headerValue = std::stod(header, &headerEnd);
        const auto expectedValue = std::stod(expected, &expectedEnd);
        return headerEnd == header.size() &&
          expectedEnd == expected.size() &&
          std::isfinite(headerValue) &&
          std::floor(headerValue) == headerValue &&
          headerValue == expectedValue;
      } catch (...) {
        return false;
      }
    }

    String randomToken(size_t length) {
      static constexpr char alphabet[] = "0123456789abcdef";
      std::vector<uint8_t> bytes((length + 1) / 2);
      if (uv_random(
            nullptr,
            nullptr,
            bytes.data(),
            bytes.size(),
            0,
            nullptr) != 0) {
        throw std::runtime_error("Secure random number generation failed");
      }

      String token;
      token.reserve(length);
      for (const auto byte : bytes) {
        token.push_back(alphabet[(byte >> 4) & 0x0f]);
        if (token.size() < length) {
          token.push_back(alphabet[byte & 0x0f]);
        }
      }
      return token;
    }

    String randomSessionId() {
      return String("session-") + randomToken(32);
    }

    String normalizePath(String path) {
      if (path.empty()) {
        return String("/");
      }

      auto trimWhitespace = [](String value) {
        auto begin = value.begin();
        while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
          ++begin;
        }
        auto end = value.end();
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
          --end;
        }
        return String(begin, end);
      };

      path = trimWhitespace(std::move(path));
      if (path.empty()) {
        return String("/");
      }

      if (path.front() != '/') {
        path.insert(path.begin(), '/');
      }

      while (path.size() > 1 && path[0] == '/' && path[1] == '/') {
        path.erase(path.begin() + 1);
      }

      while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
      }

      if (path.empty()) {
        return String("/");
      }

      return path;
    }

    bool hasPrefix(const String& value, const String& prefix) {
      if (prefix.empty() || prefix == "/") {
        return value == "/" || !value.empty();
      }
      if (value.size() < prefix.size()) {
        return false;
      }
      if (value.compare(0, prefix.size(), prefix) != 0) {
        return false;
      }
      if (value.size() == prefix.size()) {
        return true;
      }
      return value[prefix.size()] == '/';
    }

    std::vector<String> expandOAuthRoutes(const String& baseEndpoint, const String& configuredPath) {
      std::vector<String> routes;

      auto appendRoute = [&routes](String route) {
        if (route.empty()) {
          return;
        }

        route = normalizePath(std::move(route));
        if (std::find(routes.begin(), routes.end(), route) == routes.end()) {
          routes.push_back(route);
        }
      };

      const String primary = normalizePath(configuredPath.empty() ? String("/") : configuredPath);
      appendRoute(primary);

      const String base = normalizePath(baseEndpoint);
      if (base == "/" || base.empty()) {
        return routes;
      }

      if (hasPrefix(primary, base)) {
        String truncated = primary.substr(base.size());
        if (truncated.empty()) {
          truncated = String("/");
        } else if (truncated.front() != '/') {
          truncated.insert(truncated.begin(), '/');
        }
        appendRoute(truncated);
      } else {
        if (primary == "/") {
          appendRoute(base);
        } else {
          String combined = base;
          if (!combined.empty() && combined.back() != '/') {
            combined.push_back('/');
          }
          if (!primary.empty() && primary.front() == '/') {
            combined += primary.substr(1);
          } else {
            combined += primary;
          }
          appendRoute(combined);
        }
      }

      return routes;
    }

    inline unsigned char hexValue(char c) {
      if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
      if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
      if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
      return 0;
    }

    std::string urlEncode(const std::string& value) {
      std::ostringstream oss;
      oss << std::hex << std::uppercase;
      for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
          oss << c;
        } else if (c == ' ') {
          oss << '+';
        } else {
          oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
      }
      return oss.str();
    }

    std::string urlDecode(const std::string& value) {
      std::string result;
      result.reserve(value.size());
      for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '+') {
          result.push_back(' ');
        } else if (c == '%' && i + 2 < value.size()) {
          const auto hi = hexValue(value[i + 1]);
          const auto lo = hexValue(value[i + 2]);
          result.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
        } else {
          result.push_back(c);
        }
      }
      return result;
    }

    std::string appendQueryParameter(const std::string& uri,
                                     const std::string& key,
                                     const std::string& value) {
      const auto fragmentPos = uri.find('#');
      std::string base = fragmentPos == std::string::npos ? uri : uri.substr(0, fragmentPos);
      const std::string fragment = fragmentPos == std::string::npos ? "" : uri.substr(fragmentPos);
      base += (base.find('?') == std::string::npos) ? '?' : '&';
      base += key;
      base += '=';
      base += urlEncode(value);
      return base + fragment;
    }

    std::string htmlEscape(const std::string& value) {
      std::string result;
      result.reserve(value.size());
      for (char c : value) {
        switch (c) {
          case '&': result += "&amp;"; break;
          case '<': result += "&lt;"; break;
          case '>': result += "&gt;"; break;
          case '"': result += "&quot;"; break;
          case '\'': result += "&#39;"; break;
          default: result.push_back(c); break;
        }
      }
      return result;
    }

    String toLower(String value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return value;
    }

    String normalizeSessionIdValue(String value) {
      value = ::oro::runtime::string::trim(std::move(value));
      if (value.empty()) {
        return value;
      }

      const auto comma = value.find(',');
      if (comma != String::npos) {
        value = ::oro::runtime::string::trim(value.substr(0, comma));
      }

      return value;
    }

    uint32_t rotr(uint32_t value, uint32_t amount) {
      return (value >> amount) | (value << (32 - amount));
    }

    std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size) {
      static constexpr uint32_t kInit[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
      };

      static constexpr uint32_t kConstants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
      };

      uint32_t state[8];
      std::copy(std::begin(kInit), std::end(kInit), std::begin(state));

      std::vector<uint8_t> buffer(data, data + size);
      buffer.push_back(0x80);
      while ((buffer.size() % 64) != 56) {
        buffer.push_back(0x00);
      }

      const uint64_t bitLength = static_cast<uint64_t>(size) * 8ULL;
      for (int i = 7; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>((bitLength >> (static_cast<uint64_t>(i) * 8ULL)) & 0xffU));
      }

      uint32_t w[64];

      for (size_t offset = 0; offset < buffer.size(); offset += 64) {
        for (size_t i = 0; i < 16; ++i) {
          const size_t index = offset + i * 4;
          w[i] = (static_cast<uint32_t>(buffer[index]) << 24U) |
                 (static_cast<uint32_t>(buffer[index + 1]) << 16U) |
                 (static_cast<uint32_t>(buffer[index + 2]) << 8U) |
                 (static_cast<uint32_t>(buffer[index + 3]));
        }

        for (size_t i = 16; i < 64; ++i) {
          const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
          const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
          w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];

        for (size_t i = 0; i < 64; ++i) {
          const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
          const uint32_t ch = (e & f) ^ ((~e) & g);
          const uint32_t temp1 = h + S1 + ch + kConstants[i] + w[i];
          const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
          const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
          const uint32_t temp2 = S0 + maj;

          h = g;
          g = f;
          f = e;
          e = d + temp1;
          d = c;
          c = b;
          b = a;
          a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
      }

      std::array<uint8_t, 32> digest = {0};
      for (size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>((state[i] >> 24U) & 0xffU);
        digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16U) & 0xffU);
        digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8U) & 0xffU);
        digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xffU);
      }

      return digest;
    }

    std::array<uint8_t, 32> sha256(const std::string& input) {
      const auto* data = reinterpret_cast<const uint8_t*>(input.data());
      return sha256(data, input.size());
    }

    std::string base64UrlEncode(const std::vector<uint8_t>& data) {
      if (data.empty()) {
        return {};
      }

      const String source(reinterpret_cast<const char*>(data.data()), data.size());
      String encoded = bytes::base64::encode(source);
      std::string result(encoded.begin(), encoded.end());
      std::replace(result.begin(), result.end(), '+', '-');
      std::replace(result.begin(), result.end(), '/', '_');
      while (!result.empty() && result.back() == '=') {
        result.pop_back();
      }
      return result;
    }

    std::string base64UrlEncode(const std::array<uint8_t, 32>& data) {
      const String source(reinterpret_cast<const char*>(data.data()), data.size());
      String encoded = bytes::base64::encode(source);
      std::string result(encoded.begin(), encoded.end());
      std::replace(result.begin(), result.end(), '+', '-');
      std::replace(result.begin(), result.end(), '/', '_');
      while (!result.empty() && result.back() == '=') {
        result.pop_back();
      }
      return result;
    }

    bool verifyCodeChallenge(const String& verifier,
                             const String& challenge,
                             const String& method) {
      if (challenge.empty()) {
        return false;
      }

      const auto normalizedMethod = toLower(method);
      if (normalizedMethod.empty() || normalizedMethod == "plain") {
        return verifier == challenge;
      }

      if (normalizedMethod == "s256") {
        const auto digest = sha256(verifier);
        const auto encoded = base64UrlEncode(digest);
        return encoded == challenge;
      }

      return false;
    }

    bool containsUnsafeRedirectCharacters(const String& value) {
      for (char c : value) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
          return true;
        }
      }
      return false;
    }

    bool isDisallowedRedirectScheme(const String& scheme) {
      const auto normalizedScheme = toLower(scheme);
      return normalizedScheme == "javascript" ||
             normalizedScheme == "data" ||
             normalizedScheme == "vbscript";
    }

    bool isValidRedirectUri(const String& uri) {
      if (uri.empty() || containsUnsafeRedirectCharacters(uri)) {
        return false;
      }

      const auto components = ::oro::runtime::url::URL::Components::parse(uri);
      if (components.scheme.empty()) {
        return false;
      }

      if (isDisallowedRedirectScheme(components.scheme)) {
        return false;
      }

      if (!components.fragment.empty()) {
        return false;
      }

      const auto scheme = toLower(components.scheme);
      if ((scheme == "http" || scheme == "https") && components.authority.empty()) {
        return false;
      }

      return true;
    }

    bool isLoopbackHost(const String& host) {
      const auto normalized = toLower(host);
      return normalized == "localhost" ||
        normalized == "localhost." ||
        normalized == "127.0.0.1" ||
        normalized.rfind("127.", 0) == 0 ||
        normalized == "::1" ||
        normalized == "[::1]";
    }

    bool isValidOAuthHttpUri(const String& uri, bool allowQuery) {
      if (uri.empty() || uri.size() > 2048 || containsUnsafeRedirectCharacters(uri)) {
        return false;
      }
      try {
        const ::oro::runtime::url::URL parsed(uri, false);
        const auto scheme = toLower(parsed.scheme);
        if ((scheme != "http" && scheme != "https") ||
            parsed.hostname.empty() ||
            !parsed.username.empty() ||
            !parsed.password.empty() ||
            !parsed.hash.empty() ||
            (!allowQuery && !parsed.search.empty())) {
          return false;
        }
        return scheme == "https" || isLoopbackHost(parsed.hostname);
      } catch (...) {
        return false;
      }
    }

    bool isValidOAuthIssuerUri(const String& uri) {
      if (!isValidOAuthHttpUri(uri, false)) {
        return false;
      }
      try {
        const ::oro::runtime::url::URL parsed(uri, false);
        return parsed.pathname.empty() || parsed.pathname == "/";
      } catch (...) {
        return false;
      }
    }

    bool isValidPkceValue(const String& value) {
      if (value.size() < 43 || value.size() > 128) {
        return false;
      }
      for (const unsigned char character : value) {
        if (std::isalnum(character) ||
            character == '-' ||
            character == '.' ||
            character == '_' ||
            character == '~') {
          continue;
        }
        return false;
      }
      return true;
    }

    bool isValidScopeValue(const String& value) {
      for (const unsigned char character : value) {
        if (character == ' ' ||
            character == 0x21 ||
            (character >= 0x23 && character <= 0x5b) ||
            (character >= 0x5d && character <= 0x7e)) {
          continue;
        }
        return false;
      }
      return true;
    }

    bool isScopeSubset(const String& requested, const String& supported) {
      std::vector<String> supportedScopes;
      std::istringstream supportedStream(supported);
      String scope;
      while (supportedStream >> scope) {
        supportedScopes.push_back(scope);
      }

      std::istringstream requestedStream(requested);
      while (requestedStream >> scope) {
        if (std::find(
              supportedScopes.begin(),
              supportedScopes.end(),
              scope) == supportedScopes.end()) {
          return false;
        }
      }
      return true;
    }

    String publicBaseUrl(const HTTPServer::Config& config) {
      String host = config.host.empty() ? String("127.0.0.1") : config.host;
      std::ostringstream base;
      base << "http://";
      if (host.find(':') != String::npos && host.front() != '[') {
        base << '[' << host << ']';
      } else {
        base << host;
      }
      if (config.port > 0 && config.port != 80) {
        base << ':' << config.port;
      }
      return base.str();
    }

    String oauthIssuer(const HTTPServer::Config& config) {
      return config.oauth.issuer.empty()
        ? publicBaseUrl(config)
        : config.oauth.issuer;
    }

    String oauthResource(const HTTPServer::Config& config) {
      if (!config.oauth.resource.empty()) {
        return config.oauth.resource;
      }
      const auto endpoint = normalizePath(config.endpoint);
      return publicBaseUrl(config) + (endpoint == "/" ? String("") : endpoint);
    }

    String protectedResourceMetadataPath(const String& resource) {
      try {
        const ::oro::runtime::url::URL parsed(resource, false);
        String resourcePath = parsed.pathname;
        if (resourcePath.empty() || resourcePath == "/") {
          resourcePath.clear();
        } else if (resourcePath.front() != '/') {
          resourcePath.insert(resourcePath.begin(), '/');
        }
        return String("/.well-known/oauth-protected-resource") + resourcePath;
      } catch (...) {
        return "/.well-known/oauth-protected-resource";
      }
    }

    String protectedResourceMetadataUrl(const HTTPServer::Config& config) {
      const auto resource = oauthResource(config);
      try {
        const ::oro::runtime::url::URL parsed(resource, false);
        return parsed.origin + protectedResourceMetadataPath(resource) + parsed.search;
      } catch (...) {
        return publicBaseUrl(config) + protectedResourceMetadataPath(resource);
      }
    }

    String comparableResourceIdentifier(const String& resource) {
      try {
        const auto components = ::oro::runtime::url::URL::Components::parse(resource);
        return toLower(components.scheme) + "://" + toLower(components.authority) +
          components.pathname +
          (components.query.empty() ? String("") : String("?") + components.query);
      } catch (...) {
        return resource;
      }
    }

    bool resourcesMatch(const String& left, const String& right) {
      return comparableResourceIdentifier(left) == comparableResourceIdentifier(right);
    }
  }

  HTTPServer::EventDispatcher::EventDispatcher(size_t maxEvents, size_t maxBytes)
    : maxEvents(maxEvents),
      maxBytes(maxBytes) {}

  bool HTTPServer::EventDispatcher::wait(httplib::DataSink& sink) {
    String payload;
    {
      UniqueLock lock(this->mutex);
      if (this->closed) {
        return false;
      }

      if (this->queue.empty()) {
        this->cond.wait_for(lock, std::chrono::milliseconds(250));
      }

      if (this->closed) {
        return false;
      }

      if (this->queue.empty()) {
        return true;
      }

      payload = std::move(this->queue.front());
      this->queue.pop();
      this->queuedBytes -= payload.size();
    }

    try {
      if (!sink.write(payload.c_str(), payload.size())) {
        return false;
      }
      sink.os.flush();
    } catch (...) {
      return false;
    }

    return true;
  }

  bool HTTPServer::EventDispatcher::waitOnce(httplib::DataSink& sink) {
    String payload;
    {
      UniqueLock lock(this->mutex);
      this->cond.wait(lock, [this]() {
        return this->closed || !this->queue.empty();
      });

      if (this->closed || this->queue.empty()) {
        return false;
      }

      payload = std::move(this->queue.front());
      this->queue.pop();
      this->queuedBytes -= payload.size();
    }

    try {
      if (!sink.write(payload.c_str(), payload.size())) {
        return false;
      }
      sink.os.flush();
    } catch (...) {
      return false;
    }

    return false;
  }

  bool HTTPServer::EventDispatcher::send(const String& payload) {
    {
      Lock lock(this->mutex);
      if (this->closed) {
        return false;
      }
      if (payload.size() > this->maxBytes ||
          this->queue.size() >= this->maxEvents ||
          this->queuedBytes > this->maxBytes - payload.size()) {
        this->closed = true;
        this->queuedBytes = 0;
        while (!this->queue.empty()) {
          this->queue.pop();
        }
        this->cond.notify_all();
        return false;
      }
      this->queue.push(payload);
      this->queuedBytes += payload.size();
    }
    this->cond.notify_one();
    return true;
  }

  void HTTPServer::EventDispatcher::close() {
    {
      Lock lock(this->mutex);
      this->closed = true;
      this->queuedBytes = 0;
      while (!this->queue.empty()) {
        this->queue.pop();
      }
    }
    this->cond.notify_all();
  }

  HTTPServer::HTTPServer() = default;

  HTTPServer::~HTTPServer() {
    this->stop();
  }

  bool HTTPServer::start(const Config& cfg, Delegate* delegatePtr) {
    if (this->isRunning()) {
      return true;
    }

    if (cfg.maxRequestBytes == 0 ||
        cfg.maxSessions == 0 ||
        cfg.maxQueuedEvents == 0 ||
        cfg.maxQueuedBytes == 0) {
      debug("MCP HTTP server limits must be greater than zero");
      return false;
    }

    if (!isLoopbackHost(cfg.host) && cfg.token.empty() && !cfg.oauth.enabled) {
      debug("MCP HTTP server requires authentication on non-loopback binds");
      return false;
    }

    if (cfg.oauth.enabled) {
      if (cfg.oauth.defaultClientId.empty() ||
          cfg.oauth.defaultClientId.size() > 512 ||
          containsUnsafeRedirectCharacters(cfg.oauth.defaultClientId)) {
        debug("MCP OAuth requires a valid pre-registered defaultClientId");
        return false;
      }
      if (cfg.oauth.redirectUris.empty() ||
          std::any_of(cfg.oauth.redirectUris.begin(), cfg.oauth.redirectUris.end(), [](const String& uri) {
            return !isValidRedirectUri(uri) || uri.size() > 2048;
          })) {
        debug("MCP OAuth requires at least one valid pre-registered redirect URI");
        return false;
      }
      if ((!cfg.oauth.issuer.empty() && !isValidOAuthIssuerUri(cfg.oauth.issuer)) ||
          (!cfg.oauth.resource.empty() && !isValidOAuthHttpUri(cfg.oauth.resource, true)) ||
          cfg.oauth.defaultScope.size() > 4096 ||
          !isValidScopeValue(cfg.oauth.defaultScope)) {
        debug("MCP OAuth issuer, resource, or scope configuration is invalid");
        return false;
      }
      if (!isLoopbackHost(cfg.host) &&
          (cfg.oauth.issuer.empty() || cfg.oauth.resource.empty())) {
        debug("MCP OAuth on a non-loopback bind requires explicit issuer and resource URLs");
        return false;
      }
    }

    String endpointNormalized = normalizePath(cfg.endpoint);
    if (endpointNormalized == "/" && ::oro::runtime::string::trim(cfg.endpoint).empty()) {
      endpointNormalized = "/mcp";
    }

    this->server = std::make_unique<httplib::Server>();
    this->server->set_payload_max_length(std::max<size_t>(1, cfg.maxRequestBytes));
    // cpp-httplib defaults to SO_REUSEPORT when available, which can allow multiple
    // processes to bind the same host/port. MCP sessions are in-memory and must
    // remain sticky to a single server instance, so force SO_REUSEADDR instead.
    this->server->set_socket_options([](socket_t sock) {
      httplib::detail::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#ifdef _WIN32
      httplib::detail::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#endif
    });

    {
      Lock lock(this->mutex);
      this->delegate = delegatePtr;
      this->config = cfg;
      this->config.endpoint = endpointNormalized;
      if (this->config.oauth.enabled) {
        this->config.oauth.protectedResourceMetadataPath =
          protectedResourceMetadataPath(oauthResource(this->config));
      }
    }

    this->server->set_exception_handler([](const auto&, auto& res, const std::exception_ptr& ep) {
      try {
        if (ep) {
          std::rethrow_exception(ep);
        }
      } catch (const std::exception& err) {
        res.status = 500;
        res.set_content(err.what(), "text/plain");
      }
    });

    std::vector<String> endpoints;
    endpoints.push_back(endpointNormalized);
    if (endpointNormalized != "/") {
      endpoints.push_back(endpointNormalized + "/");
    }

    for (const auto& endpoint : endpoints) {
      this->server->Post(endpoint.c_str(), [this](const auto& req, auto& res) {
        this->handleMessage(req, res);
      });
      debug("MCP HTTP route registered: POST %s", endpoint.c_str());

      this->server->Get(endpoint.c_str(), [this](const auto& req, auto& res) {
        this->handleSSE(req, res);
      });
      debug("MCP HTTP route registered: GET %s", endpoint.c_str());

      this->server->Delete(endpoint.c_str(), [this](const auto& req, auto& res) {
        this->handleDelete(req, res);
      });
      debug("MCP HTTP route registered: DELETE %s", endpoint.c_str());

      this->server->Options(endpoint.c_str(), [this](const auto&, auto& res) {
        this->setCorsHeaders(res);
        res.status = 204;
      });
      debug("MCP HTTP route registered: OPTIONS %s", endpoint.c_str());
    }

    if (cfg.oauth.enabled) {
      const String authorizePath = cfg.oauth.authorizePath.empty()
        ? String("/oauth/authorize")
        : cfg.oauth.authorizePath;
      const String tokenPath = cfg.oauth.tokenPath.empty()
        ? String("/oauth/token")
        : cfg.oauth.tokenPath;
      const String metadataPath = cfg.oauth.metadataPath.empty()
        ? String("/.well-known/oauth-authorization-server")
        : cfg.oauth.metadataPath;

      auto registerAuthorizeHandlers = [this](const String& route) {
        debug("MCP HTTP route registered: GET %s (oauth authorize)", route.c_str());
        this->server->Get(route.c_str(), [this](const auto& req, auto& res) {
          this->handleOAuthAuthorize(req, res);
        });
        debug("MCP HTTP route registered: POST %s (oauth authorize)", route.c_str());
        this->server->Post(route.c_str(), [this](const auto& req, auto& res) {
          this->handleOAuthAuthorize(req, res);
        });
        debug("MCP HTTP route registered: OPTIONS %s (oauth authorize)", route.c_str());
        this->server->Options(route.c_str(), [this](const auto&, auto& res) {
          this->setCorsHeaders(res);
          res.status = 204;
        });
      };

      auto registerTokenHandlers = [this](const String& route) {
        debug("MCP HTTP route registered: POST %s (oauth token)", route.c_str());
        this->server->Post(route.c_str(), [this](const auto& req, auto& res) {
          this->handleOAuthToken(req, res);
        });
        debug("MCP HTTP route registered: OPTIONS %s (oauth token)", route.c_str());
        this->server->Options(route.c_str(), [this](const auto&, auto& res) {
          this->setCorsHeaders(res);
          res.status = 204;
        });
      };

      auto registerMetadataHandlers = [this](const String& route) {
        debug("MCP HTTP route registered: GET %s (oauth metadata)", route.c_str());
        this->server->Get(route.c_str(), [this](const auto& req, auto& res) {
          this->handleOAuthMetadata(req, res);
        });
        debug("MCP HTTP route registered: OPTIONS %s (oauth metadata)", route.c_str());
        this->server->Options(route.c_str(), [this](const auto&, auto& res) {
          this->setCorsHeaders(res);
          res.status = 204;
        });
      };

      Config routeConfig = cfg;
      routeConfig.endpoint = endpointNormalized;
      const auto resource = oauthResource(routeConfig);
      const auto protectedMetadataPath = protectedResourceMetadataPath(resource);
      debug("MCP HTTP route registered: GET %s (oauth protected resource metadata)", protectedMetadataPath.c_str());
      this->server->Get(protectedMetadataPath.c_str(), [this](const auto& req, auto& res) {
        this->handleOAuthProtectedResourceMetadata(req, res);
      });
      this->server->Options(protectedMetadataPath.c_str(), [this](const auto&, auto& res) {
        this->setCorsHeaders(res);
        res.status = 204;
      });

      const auto authorizeRoutes = expandOAuthRoutes(endpointNormalized, authorizePath);
      for (const auto& route : authorizeRoutes) {
        debug("MCP OAuth authorize route list includes: %s", route.c_str());
      }
      for (const auto& route : authorizeRoutes) {
        registerAuthorizeHandlers(route);
      }

      const auto tokenRoutes = expandOAuthRoutes(endpointNormalized, tokenPath);
      for (const auto& route : tokenRoutes) {
        debug("MCP OAuth token route list includes: %s", route.c_str());
      }
      for (const auto& route : tokenRoutes) {
        registerTokenHandlers(route);
      }

      const auto metadataRoutes = expandOAuthRoutes(endpointNormalized, metadataPath);
      for (const auto& route : metadataRoutes) {
        debug("MCP OAuth metadata route list includes: %s", route.c_str());
      }
      for (const auto& route : metadataRoutes) {
        registerMetadataHandlers(route);
      }
    }

    auto started = std::make_shared<std::promise<int>>();
    auto ready = started->get_future();

    this->serverThread = std::make_unique<std::thread>([this, started, initial = cfg]() mutable {
      try {
        int boundPort = 0;
        if (initial.port <= 0) {
          boundPort = this->server->bind_to_any_port(initial.host.c_str());
        } else if (this->server->bind_to_port(initial.host.c_str(), initial.port)) {
          boundPort = initial.port;
        }

        if (boundPort <= 0) {
          try {
            started->set_value(0);
          } catch (...) {}
          debug("MCP HTTP server failed to bind on %s:%d", initial.host.c_str(), initial.port);
          return;
        }

        {
          Lock lock(this->mutex);
          this->config.port = boundPort;
        }

        try {
          started->set_value(boundPort);
        } catch (...) {}

        debug("MCP HTTP server listening on %s:%d", initial.host.c_str(), boundPort);
        this->server->listen_after_bind();
      } catch (...) {
        try {
          started->set_value(0);
        } catch (...) {}
        debug("MCP HTTP server encountered an exception during start");
      }
    });

    const auto boundPort = ready.get();
    if (boundPort <= 0) {
      this->stop();
      return false;
    }

    return true;
  }

  void HTTPServer::stop() {
    {
      Lock lock(this->mutex);
      for (auto& entry : this->sessions) {
        if (entry.second) {
          entry.second->dispatcher->close();
        }
      }
      this->sessions.clear();
      this->delegate = nullptr;
      this->config = Config();
      this->oauthAuthorizationCodes.clear();
      this->oauthAuthorizationRequests.clear();
      this->oauthAccessTokens.clear();
    }

    if (this->server) {
      this->server->stop();
      this->server.reset();
    }

    if (this->serverThread && this->serverThread->joinable()) {
      this->serverThread->join();
    }
    this->serverThread.reset();
  }

  bool HTTPServer::isRunning() const {
    return this->server != nullptr;
  }

  bool HTTPServer::sendEvent(const String& sessionId, const String& eventName, const String& data) {
    std::shared_ptr<Session> session;
    {
      Lock lock(this->mutex);
      auto it = this->sessions.find(sessionId);
      if (it == this->sessions.end()) {
        return false;
      }
      session = it->second;
    }

    if (!session || session->closed.load()) {
      return false;
    }
    if (!session->hasStream.load()) {
      return false;
    }

    const auto payload = String("event: ") + eventName + "\r\ndata: " + data + "\r\n\r\n";
    return session->dispatcher->send(payload);
  }

  HTTPServer::Config HTTPServer::getConfig() const {
    Lock lock(this->mutex);
    return this->config;
  }

  void HTTPServer::pruneExpiredSessions(std::chrono::steady_clock::time_point now) {
    std::vector<std::shared_ptr<Session>> expired;
    std::vector<String> expiredIds;
    Delegate* delegatePtr = nullptr;
    uint32_t ttlSeconds = 0;
    {
      Lock lock(this->mutex);
      ttlSeconds = this->config.sessionTtlSeconds;
      delegatePtr = this->delegate;
      if (ttlSeconds == 0 || this->sessions.empty()) {
        return;
      }

      const auto ttl = std::chrono::seconds(ttlSeconds);
      for (auto it = this->sessions.begin(); it != this->sessions.end();) {
        const auto& session = it->second;
        if (!session) {
          it = this->sessions.erase(it);
          continue;
        }
        if (session->hasStream.load()) {
          ++it;
          continue;
        }

        if (now - session->lastActivity <= ttl) {
          ++it;
          continue;
        }

        expired.push_back(session);
        expiredIds.push_back(session->id);
        it = this->sessions.erase(it);
      }
    }

    for (const auto& session : expired) {
      if (!session) continue;
      if (session->dispatcher) {
        session->dispatcher->close();
      }
      debug("MCP HTTP session expired: %s", session->id.c_str());
    }

    if (delegatePtr) {
      for (const auto& sessionId : expiredIds) {
        delegatePtr->onSessionStopped(sessionId);
      }
    }
  }

  void HTTPServer::handleMessage(const httplib::Request& req, httplib::Response& res) {
    if (!this->authorize(req, res)) {
      return;
    }

    if (this->delegate == nullptr) {
      res.status = 503;
      res.set_content("MCP server not ready", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    const auto contentType = toLower(req.get_header_value("Content-Type"));
    if (contentType.find("application/json") != 0) {
      res.status = 415;
      res.set_content("Content-Type must be application/json", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    this->pruneExpiredSessions(std::chrono::steady_clock::now());

    try {
      nlohmann::json parsed;
      try {
        parsed = nlohmann::json::parse(req.body);
      } catch (...) {
        res.status = 400;
        res.set_content("Invalid JSON", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      auto jsonRpcError = [this, &res, &parsed](int code, const String& message, const nlohmann::json& data = nullptr) {
        nlohmann::json response {
          {"jsonrpc", "2.0"},
          {"error", {
            {"code", code},
            {"message", message}
          }}
        };
        if (parsed.is_object() &&
            parsed.contains("id") &&
            (parsed["id"].is_string() || parsed["id"].is_number())) {
          response["id"] = parsed["id"];
        }
        if (!data.is_null()) {
          response["error"]["data"] = data;
        }
        res.status = 400;
        res.set_content(response.dump(), "application/json");
        this->setCorsHeaders(res);
      };

      if (!parsed.is_object() ||
          !parsed.contains("jsonrpc") ||
          !parsed["jsonrpc"].is_string() ||
          parsed["jsonrpc"].get<String>() != kJsonRpcVersion) {
        jsonRpcError(static_cast<int>(ErrorCode::InvalidRequest), "Invalid JSON-RPC 2.0 request");
        return;
      }

      const bool hasMethod = parsed.contains("method") && parsed["method"].is_string();
      const String method = hasMethod ? parsed["method"].get<std::string>() : "";
      const bool isInitialize = method == "initialize";
      const bool containsId = parsed.contains("id");
      if (containsId && !parsed["id"].is_string() && !parsed["id"].is_number()) {
        jsonRpcError(static_cast<int>(ErrorCode::InvalidRequest), "JSON-RPC request id must be a string or number");
        return;
      }
      const bool hasId = containsId;
      const bool isNotification = hasMethod && !hasId;
      const bool isRequest = hasMethod && hasId;

      if ((isInitialize || method == "server/discover") && !isRequest) {
        jsonRpcError(
          static_cast<int>(ErrorCode::InvalidRequest),
          method + " requires a JSON-RPC request id"
        );
        return;
      }

      if (parsed.contains("params") && !parsed["params"].is_object()) {
        jsonRpcError(static_cast<int>(ErrorCode::InvalidParams), "MCP request params must be an object");
        return;
      }

      String bodyProtocolVersion;
      nlohmann::json params = nlohmann::json::object();
      if (parsed.contains("params") && parsed["params"].is_object()) {
        params = parsed["params"];
        if (params.contains("_meta") && params["_meta"].is_object()) {
          const auto version = params["_meta"].find("io.modelcontextprotocol/protocolVersion");
          if (version != params["_meta"].end() && version->is_string()) {
            bodyProtocolVersion = version->get<String>();
          }
        }
      }

      const auto headerProtocolVersion = ::oro::runtime::string::trim(
        req.get_header_value("MCP-Protocol-Version")
      );
      const bool modern = method == "server/discover" ||
        !bodyProtocolVersion.empty() ||
        isModernProtocolVersion(headerProtocolVersion);
      const bool isSubscriptionListen = modern && method == "subscriptions/listen";

      if (modern) {
        if (bodyProtocolVersion.empty()) {
          jsonRpcError(static_cast<int>(ErrorCode::HeaderMismatch), "Request metadata is missing the MCP protocol version");
          return;
        }

        if (!isModernProtocolVersion(bodyProtocolVersion)) {
          jsonRpcError(
            static_cast<int>(ErrorCode::UnsupportedProtocolVersion),
            "Unsupported MCP protocol version",
            {{"supported", supportedProtocolVersions()},
             {"requested", bodyProtocolVersion}}
          );
          return;
        }

        if (headerProtocolVersion != bodyProtocolVersion) {
          jsonRpcError(static_cast<int>(ErrorCode::HeaderMismatch), "MCP-Protocol-Version does not match request metadata");
          return;
        }

        const auto methodHeader = ::oro::runtime::string::trim(req.get_header_value("Mcp-Method"));
        if (methodHeader != method) {
          jsonRpcError(static_cast<int>(ErrorCode::HeaderMismatch), "Mcp-Method does not match the JSON-RPC method");
          return;
        }

        const bool needsName = method == "tools/call" || method == "resources/read" || method == "prompts/get";
        if (needsName) {
          const char* targetField = method == "resources/read" ? "uri" : "name";
          if (!params.contains(targetField) || !params[targetField].is_string()) {
            jsonRpcError(static_cast<int>(ErrorCode::InvalidParams), String("Missing or invalid ") + targetField);
            return;
          }
          const String expectedName = params[targetField].get<String>();
          String nameHeader;
          if (!req.has_header("Mcp-Name") ||
              !decodeMirroredHeader(req.get_header_value("Mcp-Name"), nameHeader) ||
              nameHeader != expectedName) {
            jsonRpcError(static_cast<int>(ErrorCode::HeaderMismatch), "Mcp-Name does not match the request target");
            return;
          }
        }

        Vector<ToolHeader> expectedHeaders;
        String headerError;
        if (!this->delegate->getExpectedRequestHeaders(req.body, expectedHeaders, headerError)) {
          jsonRpcError(
            static_cast<int>(ErrorCode::HeaderMismatch),
            headerError.empty() ? String("Invalid MCP parameter header declaration") : headerError
          );
          return;
        }
        for (const auto& expected : expectedHeaders) {
          const bool supplied = req.has_header(expected.name);
          if (!expected.present) {
            if (supplied) {
              jsonRpcError(
                static_cast<int>(ErrorCode::HeaderMismatch),
                expected.name + " must be omitted when its argument is absent or null"
              );
              return;
            }
            continue;
          }
          String actual;
          if (!supplied ||
              !decodeMirroredHeader(req.get_header_value(expected.name), actual) ||
              (expected.integer
                ? !numericHeaderMatches(actual, expected.value)
                : actual != expected.value)) {
            jsonRpcError(
              static_cast<int>(ErrorCode::HeaderMismatch),
              expected.name + " does not match the tool argument"
            );
            return;
          }
        }

        if (!params["_meta"].contains("io.modelcontextprotocol/clientCapabilities") ||
            !params["_meta"]["io.modelcontextprotocol/clientCapabilities"].is_object()) {
          jsonRpcError(
            static_cast<int>(ErrorCode::MissingRequiredClientCapability),
            "Request metadata must include clientCapabilities",
            {{"requiredCapabilities", nlohmann::json::object()}}
          );
          return;
        }
      }

      if (!modern &&
          !headerProtocolVersion.empty() &&
          !isLegacyProtocolVersion(headerProtocolVersion)) {
        jsonRpcError(
          static_cast<int>(ErrorCode::UnsupportedProtocolVersion),
          "Unsupported MCP protocol version",
          {{"supported", supportedProtocolVersions()},
           {"requested", headerProtocolVersion}}
        );
        return;
      }

      std::optional<String> sessionId;
      bool createdLegacySession = false;
      if (modern) {
        sessionId = this->createSessionForRequest();
      } else {
        const auto clientSessionId = normalizeSessionIdValue(req.get_header_value("Mcp-Session-Id"));
        if (!clientSessionId.empty()) {
          sessionId = this->validateSessionId(req);
          if (!sessionId.has_value()) {
            res.status = 404;
            res.set_content("Unknown or expired Mcp-Session-Id", "text/plain");
            this->setCorsHeaders(res);
            return;
          }
        } else if (!isInitialize) {
          res.status = 400;
          res.set_content("Missing Mcp-Session-Id", "text/plain");
          this->setCorsHeaders(res);
          return;
        } else {
          sessionId = this->createSessionForRequest();
          createdLegacySession = sessionId.has_value();
        }
      }

      if (!sessionId.has_value()) {
        res.status = 503;
        res.set_content("MCP request capacity reached", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      std::shared_ptr<Session> session;
      String sessionProtocolVersion;
      bool hadStream = false;
      {
        Lock lock(this->mutex);
        const auto it = this->sessions.find(*sessionId);
        if (it != this->sessions.end() && it->second) {
          session = it->second;
          sessionProtocolVersion = session->protocolVersion;
        }
      }

      if (!session) {
        res.status = 500;
        res.set_content("MCP request context unavailable", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      if (!modern &&
          !isInitialize &&
          !headerProtocolVersion.empty() &&
          !sessionProtocolVersion.empty() &&
          headerProtocolVersion != sessionProtocolVersion) {
        jsonRpcError(
          static_cast<int>(ErrorCode::HeaderMismatch),
          "MCP-Protocol-Version does not match the negotiated session version"
        );
        return;
      }

      {
        Lock lock(this->mutex);
        const auto it = this->sessions.find(*sessionId);
        if (it == this->sessions.end() || it->second != session) {
          res.status = 404;
          res.set_content("Unknown or expired Mcp-Session-Id", "text/plain");
          this->setCorsHeaders(res);
          return;
        }
        hadStream = session->hasStream.load();
        if (!hadStream) {
          session->hasStream = true;
        }
        session->lastActivity = std::chrono::steady_clock::now();
      }

      auto immediate = this->delegate->onJsonRpcRequest(*sessionId, req.body);
      bool legacyInitializationSucceeded = !isInitialize;
      if (!modern && isInitialize && immediate.has_value()) {
        try {
          const auto response = nlohmann::json::parse(*immediate);
          if (response.contains("result") &&
              response["result"].is_object() &&
              response["result"].contains("protocolVersion") &&
              response["result"]["protocolVersion"].is_string()) {
            const auto negotiatedVersion = response["result"]["protocolVersion"].get<String>();
            if (isLegacyProtocolVersion(negotiatedVersion)) {
              legacyInitializationSucceeded = true;
              Lock lock(this->mutex);
              const auto it = this->sessions.find(*sessionId);
              if (it != this->sessions.end() && it->second == session) {
                session->protocolVersion = negotiatedVersion;
              }
            }
          }
        } catch (...) {}
      }
      if (!modern && isInitialize && !legacyInitializationSucceeded && createdLegacySession) {
        this->closeSession(*sessionId);
      }
      if (!modern && legacyInitializationSucceeded) {
        this->attachSessionHeader(res, *sessionId);
      }

      if (isSubscriptionListen && immediate.has_value()) {
        bool accepted = false;
        try {
          const auto response = nlohmann::json::parse(*immediate);
          accepted = response.is_object() &&
            response.value("method", "") == "notifications/subscriptions/acknowledged";
        } catch (...) {}
        if (accepted) {
          const auto response = *immediate;
          const auto payload = String("event: message\r\ndata: ") + response + "\r\n\r\n";
          if (session->dispatcher->send(payload)) {
            immediate.reset();
            this->delegate->onJsonRpcResponseQueued(*sessionId, response);
          }
        }
      }

      if (isNotification) {
        if (modern) {
          this->closeSession(*sessionId);
        } else if (!hadStream) {
          session->hasStream = false;
        }
        res.status = 202;
        this->setCorsHeaders(res);
        return;
      }

      if (!isRequest) {
        if (modern) {
          this->closeSession(*sessionId);
        }
        jsonRpcError(static_cast<int>(ErrorCode::InvalidRequest), "Expected a JSON-RPC request or notification");
        return;
      }

      if (immediate.has_value()) {
        if (modern) {
          this->closeSession(*sessionId);
        } else if (!hadStream) {
          session->hasStream = false;
        }
        res.status = 200;
        if (modern) {
          try {
            const auto response = nlohmann::json::parse(*immediate);
            if (response.contains("error") &&
                response["error"].is_object() &&
                response["error"].value("code", 0) == static_cast<int>(ErrorCode::MethodNotFound)) {
              res.status = 404;
            }
          } catch (...) {}
        }
        res.set_content(*immediate, "application/json");
        this->setCorsHeaders(res);
        return;
      }

      if (hadStream) {
        res.status = 202;
        this->setCorsHeaders(res);
        return;
      }

      const auto dispatcher = session->dispatcher;
      res.set_header("Cache-Control", "no-cache");
      res.set_header("Connection", "keep-alive");
      this->setCorsHeaders(res);
      res.set_chunked_content_provider("text/event-stream", [dispatcher, isSubscriptionListen](size_t, httplib::DataSink& sink) {
        if (isSubscriptionListen) {
          return dispatcher->wait(sink);
        }
        return dispatcher->waitOnce(sink);
      }, [this, session, dispatcher, modern](bool) {
        dispatcher->close();
        if (modern) {
          this->closeSession(session->id);
          return;
        }

        Lock lock(this->mutex);
        const auto it = this->sessions.find(session->id);
        if (it != this->sessions.end() && it->second == session) {
          session->dispatcher = std::make_shared<EventDispatcher>(
            this->config.maxQueuedEvents,
            this->config.maxQueuedBytes
          );
          session->hasStream = false;
          session->closed = false;
          session->lastActivity = std::chrono::steady_clock::now();
        }
      });
    } catch (const std::exception& err) {
      res.status = 500;
      res.set_content(err.what(), "text/plain");
      this->setCorsHeaders(res);
    }
  }

  void HTTPServer::handleSSE(const httplib::Request& req, httplib::Response& res) {
    if (!this->authorize(req, res)) {
      return;
    }

    this->pruneExpiredSessions(std::chrono::steady_clock::now());

    String requestedSessionId = normalizeSessionIdValue(req.get_header_value("Mcp-Session-Id"));
    if (requestedSessionId.empty()) {
      requestedSessionId = normalizeSessionIdValue(req.get_param_value("session_id"));
    }
    if (requestedSessionId.empty()) {
      res.status = 400;
      res.set_content("Missing Mcp-Session-Id", "text/plain");
      this->setCorsHeaders(res);
      return;
    }
    std::shared_ptr<Session> session;
    std::shared_ptr<EventDispatcher> dispatcher;

    {
      Lock lock(this->mutex);
      const auto now = std::chrono::steady_clock::now();
      const bool replaceOnReconnect = this->config.replaceSseStreamOnReconnect;
      if (!requestedSessionId.empty()) {
        auto it = this->sessions.find(requestedSessionId);
        if (it != this->sessions.end() && it->second) {
          session = it->second;

          const bool activeStream = session->hasStream.load() && !session->closed.load();
          if (activeStream) {
            if (!replaceOnReconnect) {
              res.status = 409;
              res.set_content("MCP session already has an active SSE stream", "text/plain");
              this->setCorsHeaders(res);
              return;
            }

            if (session->dispatcher) {
              session->dispatcher->close();
            }
          }

          if (!session->dispatcher || session->closed.load() || activeStream) {
            session->dispatcher = std::make_shared<EventDispatcher>(
              this->config.maxQueuedEvents,
              this->config.maxQueuedBytes
            );
          }
          dispatcher = session->dispatcher;
          session->closed = false;
          session->lastActivity = now;
        }
      }

      if (session) {
        session->closed = false;
        session->hasStream = true;
      }
    }

    if (!session) {
      res.status = 404;
      res.set_content("Unknown or expired Mcp-Session-Id", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    debug("MCP HTTP session started: %s", session->id.c_str());

    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    this->attachSessionHeader(res, session->id);
    this->setCorsHeaders(res);

    if (!dispatcher) {
      res.status = 500;
      res.set_content("MCP session stream unavailable", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    res.set_chunked_content_provider("text/event-stream", [dispatcher](size_t, httplib::DataSink& sink) {
      return dispatcher->wait(sink);
    }, [this, session, dispatcher](bool) {
      dispatcher->close();

      {
        Lock lock(this->mutex);
        if (session->dispatcher == dispatcher) {
          session->closed = true;
          session->hasStream = false;
        }

        auto it = this->sessions.find(session->id);
        if (it != this->sessions.end() && it->second) {
          it->second->lastActivity = std::chrono::steady_clock::now();
        }
      }
    });
  }

  void HTTPServer::handleDelete(const httplib::Request& req, httplib::Response& res) {
    if (!this->authorize(req, res)) {
      return;
    }

    const auto sessionId = this->validateSessionId(req);
    if (!sessionId.has_value()) {
      res.status = 404;
      res.set_content("Unknown or expired Mcp-Session-Id", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    this->closeSession(*sessionId);
    res.status = 204;
    this->setCorsHeaders(res);
  }

  void HTTPServer::handleOAuthAuthorize(const httplib::Request& req, httplib::Response& res) {
    debug("MCP OAuth authorize handler invoked for path %s method %s", req.path.c_str(), req.method.c_str());
    Config currentConfig;
    {
      Lock lock(this->mutex);
      currentConfig = this->config;
    }

    const auto& oauthConfig = currentConfig.oauth;
    if (!oauthConfig.enabled) {
      res.status = 404;
      res.set_content("Not Found", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (req.method != "GET" && req.method != "POST") {
      res.status = 405;
      res.set_header("Allow", "GET, POST");
      res.set_content("Method Not Allowed", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (req.method == "POST" &&
        toLower(req.get_header_value("Content-Type")).find(
          "application/x-www-form-urlencoded") != 0) {
      res.status = 415;
      res.set_content("Content-Type must be application/x-www-form-urlencoded", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    auto invalidRequest = [this, &res](const String& message) {
      res.status = 400;
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_content(message.c_str(), "text/plain");
      this->setCorsHeaders(res);
    };

    const auto issuer = oauthIssuer(currentConfig);
    const auto expectedResource = oauthResource(currentConfig);

    if (req.method == "POST") {
      if (req.params.count("authorization_request") != 1 ||
          req.params.count("decision") != 1) {
        invalidRequest("authorization_request and decision must each occur exactly once");
        return;
      }
      const auto requestId = ::oro::runtime::string::trim(
        req.get_param_value("authorization_request")
      );
      const auto decision = toLower(::oro::runtime::string::trim(
        req.get_param_value("decision")
      ));
      if (requestId.empty() || decision.empty()) {
        invalidRequest("Missing authorization_request or decision");
        return;
      }
      if (decision != "approve" && decision != "deny") {
        invalidRequest("Invalid decision");
        return;
      }

      OAuthAuthorizationRequest authorizationRequest;
      bool found = false;
      const auto now = std::chrono::steady_clock::now();
      {
        Lock lock(this->mutex);
        this->pruneExpiredOAuthArtifacts(now);
        const auto it = this->oauthAuthorizationRequests.find(requestId);
        if (it != this->oauthAuthorizationRequests.end()) {
          authorizationRequest = it->second;
          this->oauthAuthorizationRequests.erase(it);
          found = true;
        }
      }
      if (!found) {
        invalidRequest("Authorization request is invalid or has expired");
        return;
      }

      if (decision == "deny") {
        String location = appendQueryParameter(
          authorizationRequest.redirectUri,
          "error",
          "access_denied"
        );
        if (!authorizationRequest.state.empty()) {
          location = appendQueryParameter(location, "state", authorizationRequest.state);
        }
        location = appendQueryParameter(location, "iss", issuer);
        res.status = 302;
        res.set_header("Location", location.c_str());
        res.set_header("Cache-Control", "no-store");
        res.set_header("Pragma", "no-cache");
        res.set_content("Access denied", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      OAuthAuthorizationCode authorizationCode;
      authorizationCode.code = randomToken(64);
      authorizationCode.clientId = authorizationRequest.clientId;
      authorizationCode.redirectUri = authorizationRequest.redirectUri;
      authorizationCode.scope = authorizationRequest.scope;
      authorizationCode.state = authorizationRequest.state;
      authorizationCode.codeChallenge = authorizationRequest.codeChallenge;
      authorizationCode.codeChallengeMethod = authorizationRequest.codeChallengeMethod;
      authorizationCode.resource = authorizationRequest.resource;
      const uint32_t codeLifetime = oauthConfig.codeLifetimeSeconds > 0
        ? oauthConfig.codeLifetimeSeconds
        : 300;
      authorizationCode.expiresAt = now + std::chrono::seconds(codeLifetime);

      {
        Lock lock(this->mutex);
        this->pruneExpiredOAuthArtifacts(now);
        if (this->oauthAuthorizationCodes.size() >= kMaxOAuthArtifacts) {
          res.status = 503;
          res.set_content("OAuth authorization capacity reached", "text/plain");
          this->setCorsHeaders(res);
          return;
        }
        this->oauthAuthorizationCodes.insert_or_assign(
          authorizationCode.code,
          authorizationCode
        );
      }

      String location = appendQueryParameter(
        authorizationRequest.redirectUri,
        "code",
        authorizationCode.code
      );
      if (!authorizationRequest.state.empty()) {
        location = appendQueryParameter(location, "state", authorizationRequest.state);
      }
      location = appendQueryParameter(location, "iss", issuer);

      res.status = 302;
      res.set_header("Location", location.c_str());
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_content("Authorization complete", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    const String responseType = ::oro::runtime::string::trim(
      req.get_param_value("response_type")
    );
    const String clientId = ::oro::runtime::string::trim(
      req.get_param_value("client_id")
    );
    const String redirectUri = ::oro::runtime::string::trim(
      req.get_param_value("redirect_uri")
    );
    const String requestedScope = ::oro::runtime::string::trim(
      req.get_param_value("scope")
    );
    const String state = req.get_param_value("state");
    const String codeChallenge = ::oro::runtime::string::trim(
      req.get_param_value("code_challenge")
    );
    const String codeChallengeMethod = ::oro::runtime::string::trim(
      req.get_param_value("code_challenge_method")
    );
    const String resource = ::oro::runtime::string::trim(
      req.get_param_value("resource")
    );

    for (const auto* parameter : {
           "response_type",
           "client_id",
           "redirect_uri",
           "code_challenge",
           "code_challenge_method",
           "resource"
         }) {
      if (req.params.count(parameter) != 1) {
        invalidRequest(String(parameter) + " must occur exactly once");
        return;
      }
    }
    if (req.params.count("scope") > 1 || req.params.count("state") > 1) {
      invalidRequest("scope and state may occur at most once");
      return;
    }

    if (responseType != "code") {
      invalidRequest("Unsupported response_type");
      return;
    }

    if (clientId != oauthConfig.defaultClientId) {
      invalidRequest("Unknown client_id");
      return;
    }

    if (std::find(
          oauthConfig.redirectUris.begin(),
          oauthConfig.redirectUris.end(),
          redirectUri) == oauthConfig.redirectUris.end()) {
      invalidRequest("redirect_uri is not registered for this client");
      return;
    }

    const auto methodLower = toLower(codeChallengeMethod);
    if (methodLower != "s256" || !isValidPkceValue(codeChallenge)) {
      invalidRequest("A valid S256 code_challenge is required");
      return;
    }

    if (!resourcesMatch(resource, expectedResource)) {
      invalidRequest("resource must identify this MCP server");
      return;
    }

    if (state.size() > 2048 || requestedScope.size() > 4096) {
      invalidRequest("state or scope exceeds the supported length");
      return;
    }

    const String effectiveScope = requestedScope.empty()
      ? oauthConfig.defaultScope
      : requestedScope;
    if (!isScopeSubset(effectiveScope, oauthConfig.defaultScope)) {
      invalidRequest("Requested scope is not supported");
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    OAuthAuthorizationRequest authorizationRequest;
    authorizationRequest.id = randomToken(64);
    authorizationRequest.clientId = clientId;
    authorizationRequest.redirectUri = redirectUri;
    authorizationRequest.scope = effectiveScope;
    authorizationRequest.state = state;
    authorizationRequest.codeChallenge = codeChallenge;
    authorizationRequest.codeChallengeMethod = "S256";
    authorizationRequest.resource = expectedResource;
    const uint32_t codeLifetime = oauthConfig.codeLifetimeSeconds > 0 ? oauthConfig.codeLifetimeSeconds : 300;
    authorizationRequest.expiresAt = now + std::chrono::seconds(codeLifetime);

    {
      Lock lock(this->mutex);
      this->pruneExpiredOAuthArtifacts(now);
      if (this->oauthAuthorizationRequests.size() >= kMaxOAuthArtifacts) {
        res.status = 503;
        res.set_content("OAuth authorization capacity reached", "text/plain");
        this->setCorsHeaders(res);
        return;
      }
      this->oauthAuthorizationRequests.insert_or_assign(
        authorizationRequest.id,
        authorizationRequest
      );
    }

    auto html = this->renderAuthorizationPage(
      req,
      oauthConfig,
      clientId,
      redirectUri,
      effectiveScope,
      state,
      codeChallenge,
      "S256",
      expectedResource,
      authorizationRequest.id
    );
    res.status = 200;
    res.set_header("Content-Type", "text/html; charset=utf-8");
    res.set_header("Cache-Control", "no-store");
    res.set_header("Pragma", "no-cache");
    res.set_content(html.c_str(), "text/html; charset=utf-8");
    this->setCorsHeaders(res);
  }

  void HTTPServer::handleOAuthToken(const httplib::Request& req, httplib::Response& res) {
    debug("MCP OAuth token handler invoked for path %s method %s", req.path.c_str(), req.method.c_str());
    Config currentConfig;
    {
      Lock lock(this->mutex);
      currentConfig = this->config;
    }

    const auto& oauthConfig = currentConfig.oauth;
    if (!oauthConfig.enabled) {
      res.status = 404;
      res.set_content("Not Found", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (req.method != "POST") {
      res.status = 405;
      res.set_header("Allow", "POST");
      res.set_content("Method Not Allowed", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (toLower(req.get_header_value("Content-Type")).find(
          "application/x-www-form-urlencoded") != 0) {
      res.status = 415;
      res.set_content("Content-Type must be application/x-www-form-urlencoded", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    auto errorResponse = [this, &res](const char* code, const char* description, int status = 400) {
      nlohmann::json payload;
      payload["error"] = code;
      if (description && description[0] != '\0') {
        payload["error_description"] = description;
      }
      res.status = status;
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_header("Content-Type", "application/json");
      res.set_content(payload.dump(), "application/json");
      this->setCorsHeaders(res);
    };

    for (const auto* parameter : {
           "grant_type",
           "code",
           "redirect_uri",
           "client_id",
           "code_verifier",
           "resource"
         }) {
      if (req.params.count(parameter) != 1) {
        errorResponse("invalid_request", "Required parameters must each occur exactly once");
        return;
      }
    }

    const String grantType = req.get_param_value("grant_type");
    if (grantType != "authorization_code") {
      errorResponse("unsupported_grant_type", "Only the authorization_code grant type is supported");
      return;
    }

    const String code = req.get_param_value("code");
    const String redirectUri = ::oro::runtime::string::trim(req.get_param_value("redirect_uri"));
    const String clientId = ::oro::runtime::string::trim(req.get_param_value("client_id"));
    const String codeVerifier = req.get_param_value("code_verifier");
    const String resource = ::oro::runtime::string::trim(req.get_param_value("resource"));

    if (code.empty() ||
        redirectUri.empty() ||
        clientId.empty() ||
        !isValidPkceValue(codeVerifier) ||
        resource.empty()) {
      errorResponse("invalid_request", "Missing required parameters");
      return;
    }

    if (!isValidRedirectUri(redirectUri)) {
      errorResponse("invalid_request", "Invalid redirect_uri");
      return;
    }

    OAuthAuthorizationCode authorizationCode;
    bool found = false;
    {
      Lock lock(this->mutex);
      const auto now = std::chrono::steady_clock::now();
      this->pruneExpiredOAuthArtifacts(now);
      auto it = this->oauthAuthorizationCodes.find(code);
      if (it != this->oauthAuthorizationCodes.end()) {
        authorizationCode = it->second;
        this->oauthAuthorizationCodes.erase(it);
        found = true;
      }
    }

    if (!found) {
      errorResponse("invalid_grant", "Authorization code is invalid or has expired");
      return;
    }

    if (!authorizationCode.redirectUri.empty() && authorizationCode.redirectUri != redirectUri) {
      errorResponse("invalid_grant", "Redirect URI mismatch");
      return;
    }

    if (!authorizationCode.clientId.empty() && authorizationCode.clientId != clientId) {
      errorResponse("invalid_grant", "Invalid client");
      return;
    }

    if (!resourcesMatch(resource, authorizationCode.resource)) {
      errorResponse("invalid_target", "Resource does not match the authorization grant");
      return;
    }

    if (!verifyCodeChallenge(codeVerifier, authorizationCode.codeChallenge, authorizationCode.codeChallengeMethod)) {
      errorResponse("invalid_grant", "Code verifier mismatch");
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    OAuthAccessToken accessToken;
    accessToken.token = randomToken(96);
    accessToken.clientId = clientId;
    accessToken.scope = authorizationCode.scope;
    accessToken.resource = authorizationCode.resource;
    const uint32_t tokenLifetime = oauthConfig.tokenLifetimeSeconds > 0 ? oauthConfig.tokenLifetimeSeconds : 3600;
    accessToken.expiresAt = now + std::chrono::seconds(tokenLifetime);

    {
      Lock lock(this->mutex);
      this->pruneExpiredOAuthArtifacts(now);
      if (this->oauthAccessTokens.size() >= kMaxOAuthArtifacts) {
        errorResponse("temporarily_unavailable", "OAuth token capacity reached", 503);
        return;
      }
      this->oauthAccessTokens.insert_or_assign(accessToken.token, accessToken);
    }

    nlohmann::json payload;
    payload["access_token"] = accessToken.token.c_str();
    payload["token_type"] = "Bearer";
    payload["expires_in"] = tokenLifetime;
    if (!accessToken.scope.empty()) {
      payload["scope"] = accessToken.scope.c_str();
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Pragma", "no-cache");
    res.set_header("Content-Type", "application/json");
    res.set_content(payload.dump(), "application/json");
    this->setCorsHeaders(res);
  }

  void HTTPServer::handleOAuthMetadata(const httplib::Request& req, httplib::Response& res) {
    debug("MCP OAuth metadata handler invoked for path %s method %s", req.path.c_str(), req.method.c_str());
    (void)req;

    Config currentConfig;
    {
      Lock lock(this->mutex);
      currentConfig = this->config;
    }

    const auto& oauthConfig = currentConfig.oauth;
    if (!oauthConfig.enabled) {
      res.status = 404;
      res.set_content("Not Found", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    const String issuer = oauthIssuer(currentConfig);
    const String resource = oauthResource(currentConfig);

    auto composeEndpoint = [&](const String& path) -> String {
      String root = issuer;
      if (path.empty()) {
        return root;
      }
      const bool rootEndsWithSlash = !root.empty() && root.back() == '/';
      if (path.front() == '/') {
        if (rootEndsWithSlash) {
          return root.substr(0, root.size() - 1) + path;
        }
        return root + path;
      }
      return root + (rootEndsWithSlash ? "" : "/") + path;
    };

    auto scopesFromString = [](const String& value) -> std::vector<std::string> {
      std::vector<std::string> scopes;
      std::istringstream stream(value);
      std::string scope;
      while (stream >> scope) {
        scopes.push_back(scope);
      }
      return scopes;
    };

    nlohmann::json metadata;
    metadata["issuer"] = issuer.c_str();
    metadata["authorization_endpoint"] = composeEndpoint(oauthConfig.authorizePath.empty() ? "/oauth/authorize" : oauthConfig.authorizePath).c_str();
    metadata["token_endpoint"] = composeEndpoint(oauthConfig.tokenPath.empty() ? "/oauth/token" : oauthConfig.tokenPath).c_str();
    metadata["response_types_supported"] = nlohmann::json::array({"code"});
    metadata["grant_types_supported"] = nlohmann::json::array({"authorization_code"});
    metadata["code_challenge_methods_supported"] = nlohmann::json::array({"S256"});
    metadata["token_endpoint_auth_methods_supported"] = nlohmann::json::array({"none"});
    metadata["authorization_response_iss_parameter_supported"] = true;
    metadata["protected_resources"] = nlohmann::json::array({resource});

    const auto scopeList = scopesFromString(oauthConfig.defaultScope);
    if (!scopeList.empty()) {
      metadata["scopes_supported"] = scopeList;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Pragma", "no-cache");
    res.set_header("Content-Type", "application/json");
    res.set_content(metadata.dump(), "application/json");
    this->setCorsHeaders(res);
  }

  void HTTPServer::handleOAuthProtectedResourceMetadata(
    const httplib::Request& req,
    httplib::Response& res
  ) {
    (void)req;
    Config currentConfig;
    {
      Lock lock(this->mutex);
      currentConfig = this->config;
    }

    if (!currentConfig.oauth.enabled) {
      res.status = 404;
      res.set_content("Not Found", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    nlohmann::json metadata {
      {"resource", oauthResource(currentConfig)},
      {"authorization_servers", nlohmann::json::array({oauthIssuer(currentConfig)})},
      {"bearer_methods_supported", nlohmann::json::array({"header"})},
      {"resource_name", "Oro Runtime MCP server"}
    };

    std::vector<std::string> scopes;
    std::istringstream stream(currentConfig.oauth.defaultScope);
    std::string scope;
    while (stream >> scope) {
      scopes.push_back(scope);
    }
    if (!scopes.empty()) {
      metadata["scopes_supported"] = scopes;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Pragma", "no-cache");
    res.set_content(metadata.dump(), "application/json");
    this->setCorsHeaders(res);
  }

  void HTTPServer::handlePing(const String& sessionId) {
    if (this->delegate) {
      this->delegate->onPing(sessionId);
    }
  }

  void HTTPServer::closeSession(const String& sessionId) {
    if (this->delegate) {
      this->delegate->onSessionStopped(sessionId);
    }

    debug("MCP HTTP session stopped: %s", sessionId.c_str());

    Lock lock(this->mutex);
    this->sessions.erase(sessionId);
  }

  bool HTTPServer::authorize(const httplib::Request& req, httplib::Response& res) const {
    String staticToken;
    OAuthConfig oauthConfig;
    Config currentConfig;
    {
      Lock lock(this->mutex);
      staticToken = this->config.token;
      oauthConfig = this->config.oauth;
      currentConfig = this->config;
    }

    const bool oauthEnabled = oauthConfig.enabled;
    const bool authEnabled = !staticToken.empty() || oauthEnabled;

    const auto origin = ::oro::runtime::string::trim(req.get_header_value("Origin"));
    if (!origin.empty()) {
      // Reject non-loopback browser origins to prevent DNS rebinding attacks.
      bool allowed = false;
      if (origin != "null") {
        try {
          const ::oro::runtime::url::URL originUrl(origin, false);
          const auto host = toLower(originUrl.hostname);
          allowed = host == "localhost" || host == "127.0.0.1" || host == "::1";
        } catch (...) {
          allowed = false;
        }
      }

      if (!allowed) {
        debug("MCP HTTP request rejected (disallowed origin): %s", origin.c_str());
        res.status = 403;
        res.set_content("Forbidden", "text/plain");
        this->setCorsHeaders(res);
        return false;
      }
    }

    if (this->delegate) {
      auto decision = this->delegate->authorize(req, res);
      if (decision.has_value()) {
        return *decision;
      }
    }

    if (!authEnabled) {
      return true;
    }

    auto trim = [](String value) {
      auto begin = value.begin();
      while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
      }
      auto end = value.end();
      while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
      }
      return String(begin, end);
    };

    bool staticMatch = false;
    Vector<String> candidates;

    auto header = req.get_header_value("Authorization");
    if (!header.empty()) {
      static const std::string bearerPrefix = "Bearer ";
      if (header.size() >= bearerPrefix.size() && std::equal(bearerPrefix.begin(), bearerPrefix.end(), header.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
          })) {
        String provided = trim(header.substr(bearerPrefix.size()));
        if (!provided.empty()) {
          if (!staticToken.empty() && provided == staticToken) {
            staticMatch = true;
          } else {
            candidates.push_back(provided);
          }
        }
      }
    }

    if (staticMatch) {
      return true;
    }

    if (oauthEnabled) {
      for (const auto& candidate : candidates) {
        if (this->validateAccessToken(candidate)) {
          return true;
        }
      }
    }

    if (!staticToken.empty() && candidates.empty() && !oauthEnabled) {
      res.status = 401;
      res.set_header("WWW-Authenticate", "Bearer");
      res.set_content("Unauthorized", "text/plain");
      this->setCorsHeaders(res);
      debug("MCP HTTP request rejected (missing bearer token)");
      return false;
    }

    res.status = 401;
    String challenge = "Bearer";
    if (oauthEnabled) {
      challenge += " resource_metadata=\"" +
        protectedResourceMetadataUrl(currentConfig) + "\"";
      if (!oauthConfig.defaultScope.empty()) {
        challenge += ", scope=\"" + oauthConfig.defaultScope + "\"";
      }
      if (!candidates.empty()) {
        challenge += ", error=\"invalid_token\"";
      }
    }
    res.set_header("WWW-Authenticate", challenge.c_str());
    res.set_content("Unauthorized", "text/plain");
    this->setCorsHeaders(res);
    debug("MCP HTTP request rejected (unauthorized)");
    return false;
  }

  bool HTTPServer::validateAccessToken(const String& token) const {
    if (token.empty()) {
      return false;
    }

    const auto now = std::chrono::steady_clock::now();
    Lock lock(this->mutex);
    this->pruneExpiredOAuthArtifacts(now);
    const auto it = this->oauthAccessTokens.find(token);
    return it != this->oauthAccessTokens.end() &&
      resourcesMatch(it->second.resource, oauthResource(this->config));
  }

  void HTTPServer::pruneExpiredOAuthArtifacts(std::chrono::steady_clock::time_point now) const {
    for (auto it = this->oauthAuthorizationRequests.begin(); it != this->oauthAuthorizationRequests.end();) {
      if (it->second.expiresAt <= now) {
        auto eraseIt = it++;
        this->oauthAuthorizationRequests.erase(eraseIt);
      } else {
        ++it;
      }
    }

    for (auto it = this->oauthAuthorizationCodes.begin(); it != this->oauthAuthorizationCodes.end();) {
      if (it->second.expiresAt <= now) {
        auto eraseIt = it++;
        this->oauthAuthorizationCodes.erase(eraseIt);
      } else {
        ++it;
      }
    }

    for (auto it = this->oauthAccessTokens.begin(); it != this->oauthAccessTokens.end();) {
      if (it->second.expiresAt <= now) {
        auto eraseIt = it++;
        this->oauthAccessTokens.erase(eraseIt);
      } else {
        ++it;
      }
    }
  }

  String HTTPServer::renderAuthorizationPage(const httplib::Request& req,
                                             const OAuthConfig& oauthConfig,
                                             const String& clientId,
                                             const String& redirectUri,
                                             const String& scope,
                                             const String& state,
                                             const String& codeChallenge,
                                             const String& codeChallengeMethod,
                                             const String& resource,
                                             const String& authorizationRequestId) const {
    nlohmann::json context = nlohmann::json::object();
    context["clientId"] = clientId.c_str();
    context["redirectUri"] = redirectUri.c_str();
    context["scope"] = scope.c_str();
    context["state"] = state.c_str();
    context["codeChallenge"] = codeChallenge.c_str();
    context["codeChallengeMethod"] = codeChallengeMethod.c_str();
    context["resource"] = resource.c_str();
    context["authorizationRequest"] = authorizationRequestId.c_str();
    context["method"] = req.method;
    context["path"] = req.path;
    context["remoteAddress"] = req.remote_addr;
    context["defaults"] = {
      {"clientId", oauthConfig.defaultClientId},
      {"scope", oauthConfig.defaultScope}
    };

    nlohmann::json params = nlohmann::json::object();
    for (const auto& entry : req.params) {
      const auto existing = params.find(entry.first);
      if (existing == params.end()) {
        params[entry.first] = entry.second;
      } else if (existing->is_array()) {
        existing->push_back(entry.second);
      } else {
        nlohmann::json arr = nlohmann::json::array();
        arr.push_back(*existing);
        arr.push_back(entry.second);
        params[entry.first] = arr;
      }
    }
    context["parameters"] = params;

    String contextJson;
    for (const auto character : context.dump()) {
      if (character == '<') {
        contextJson += "\\u003c";
      } else if (character == '>') {
        contextJson += "\\u003e";
      } else if (character == '&') {
        contextJson += "\\u0026";
      } else {
        contextJson.push_back(character);
      }
    }

    auto appendContextScript = [&](String& html) {
      const String scriptTag = String("<script>window.__MCP_AUTHORIZATION_CONTEXT__ = ") + contextJson + ";</script>";
      const auto insertPos = html.rfind("</body>");
      if (insertPos != String::npos) {
        html.insert(insertPos, scriptTag);
      } else {
        html.append(scriptTag);
      }
    };

    auto replaceAll = [](String& target, const String& placeholder, const String& value) {
      size_t position = 0;
      while ((position = target.find(placeholder, position)) != String::npos) {
        target.replace(position, placeholder.size(), value);
        position += value.size();
      }
    };

    String templateHtml;
    if (!oauthConfig.screenHtml.empty()) {
      templateHtml = oauthConfig.screenHtml;
    } else if (!oauthConfig.screenFile.empty()) {
      this->readScreenTemplate(oauthConfig.screenFile, templateHtml);
    }

    if (!templateHtml.empty()) {
      replaceAll(templateHtml, "{{AUTH_CONTEXT_JSON}}", contextJson);
      replaceAll(templateHtml, "{{CLIENT_ID}}", htmlEscape(clientId));
      replaceAll(templateHtml, "{{REDIRECT_URI}}", htmlEscape(redirectUri));
      replaceAll(templateHtml, "{{SCOPE}}", htmlEscape(scope));
      replaceAll(templateHtml, "{{STATE}}", htmlEscape(state));
      replaceAll(templateHtml, "{{CODE_CHALLENGE}}", htmlEscape(codeChallenge));
      replaceAll(templateHtml, "{{CODE_CHALLENGE_METHOD}}", htmlEscape(codeChallengeMethod));
      replaceAll(templateHtml, "{{RESOURCE}}", htmlEscape(resource));
      replaceAll(templateHtml, "{{AUTHORIZATION_REQUEST}}", htmlEscape(authorizationRequestId));
      appendContextScript(templateHtml);
      return templateHtml;
    }

    std::ostringstream html;
    html << "<!DOCTYPE html><html lang=\"en\"><head>"
         << "<meta charset=\"utf-8\" />"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"
         << "<title>Authorize Access</title>"
         << "<style>"
         << "body{font-family:Inter,system-ui,-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:0;background:#f6f8fb;color:#1b1f23;}"
         << ".container{max-width:520px;margin:60px auto;padding:32px;background:#fff;border-radius:16px;box-shadow:0 20px 45px rgba(15,23,42,0.12);}"
         << "h1{margin:0 0 16px;font-size:1.6rem;font-weight:600;}"
         << "p{margin:12px 0 20px;color:#4b5563;line-height:1.6;}"
         << ".details{border:1px solid #e5e7eb;border-radius:12px;padding:16px;margin-bottom:24px;background:#f9fafb;}"
         << ".details dt{font-weight:600;color:#374151;margin-bottom:4px;}"
         << ".details dd{margin:0 0 12px;color:#111827;overflow-wrap:anywhere;}"
         << ".actions{display:flex;gap:12px;}"
         << "button{flex:1;padding:12px 18px;border-radius:999px;border:none;font-size:1rem;font-weight:600;cursor:pointer;transition:transform 0.15s ease,box-shadow 0.15s ease;}"
         << "button.primary{background:#2563eb;color:#fff;box-shadow:0 10px 24px rgba(37,99,235,0.35);}"
         << "button.secondary{background:#e5e7eb;color:#111827;}"
         << "button:hover{transform:translateY(-1px);}"
         << "</style></head><body>"
         << "<div class=\"container\">"
         << "<h1>Authorize Client Access</h1>"
         << "<p>The client below is requesting permission to access your Oro MCP server.</p>"
         << "<dl class=\"details\">"
         << "<dt>Client ID</dt><dd>" << htmlEscape(clientId.empty() ? oauthConfig.defaultClientId : clientId) << "</dd>"
         << "<dt>Redirect URI</dt><dd>" << htmlEscape(redirectUri) << "</dd>";

    if (!scope.empty()) {
      html << "<dt>Scope</dt><dd>" << htmlEscape(scope) << "</dd>";
    }

    html << "<dt>Resource</dt><dd>" << htmlEscape(resource) << "</dd>";

    if (!state.empty()) {
      html << "<dt>State</dt><dd>" << htmlEscape(state) << "</dd>";
    }

    html << "<dt>PKCE Method</dt><dd>" << htmlEscape(codeChallengeMethod) << "</dd>"
         << "</dl>"
         << "<form method=\"post\" action=\"" << htmlEscape(req.path) << "\">"
         << "<input type=\"hidden\" name=\"authorization_request\" value=\"" << htmlEscape(authorizationRequestId) << "\" />"
         << "<div class=\"actions\">"
         << "<button type=\"submit\" name=\"decision\" value=\"approve\" class=\"primary\">Authorize</button>"
         << "<button type=\"submit\" name=\"decision\" value=\"deny\" class=\"secondary\">Deny</button>"
         << "</div></form></div></body></html>";

    String htmlString = html.str();
    appendContextScript(htmlString);
    return htmlString;
  }

  bool HTTPServer::readScreenTemplate(const String& path, String& out) const {
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
  }

  std::optional<String> HTTPServer::validateSessionId(const httplib::Request& req) const {
    String sessionId = normalizeSessionIdValue(req.get_header_value("Mcp-Session-Id"));
    if (sessionId.empty()) {
      sessionId = normalizeSessionIdValue(req.get_param_value("session_id"));
    }

    if (sessionId.empty()) {
      return std::nullopt;
    }

    Lock lock(this->mutex);
    const auto it = this->sessions.find(sessionId);
    if (it == this->sessions.end() || !it->second) {
      return std::nullopt;
    }
    return sessionId;
  }

  std::optional<String> HTTPServer::createSessionForRequest(const String& requestedId) {
    auto session = std::make_shared<Session>();
    session->id = isValidSessionIdToken(requestedId) ? requestedId : randomSessionId();
    size_t maxQueuedEvents = 0;
    size_t maxQueuedBytes = 0;
    {
      Lock lock(this->mutex);
      if (this->sessions.size() >= this->config.maxSessions) {
        return std::nullopt;
      }
      maxQueuedEvents = this->config.maxQueuedEvents;
      maxQueuedBytes = this->config.maxQueuedBytes;
    }

    session->dispatcher = std::make_shared<EventDispatcher>(maxQueuedEvents, maxQueuedBytes);
    session->closed = false;
    session->hasStream = false;
    session->lastActivity = std::chrono::steady_clock::now();

    {
      Lock lock(this->mutex);
      if (this->sessions.size() >= this->config.maxSessions) {
        return std::nullopt;
      }
      this->sessions.insert_or_assign(session->id, session);
    }

    if (this->delegate) {
      this->delegate->onSessionStarted(session->id);
    }

    debug("MCP HTTP session created via POST: %s", session->id.c_str());
    return session->id;
  }

void HTTPServer::attachSessionHeader(httplib::Response& res, const String& sessionId) const {
  res.headers.erase("Mcp-Session-Id");
  res.set_header("Mcp-Session-Id", sessionId.c_str());
}

void HTTPServer::setCorsHeaders(httplib::Response& res) const {
  res.headers.erase("Access-Control-Allow-Origin");
  res.headers.erase("Access-Control-Allow-Headers");
  res.headers.erase("Access-Control-Allow-Methods");
  res.headers.erase("Access-Control-Expose-Headers");

  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, MCP-Protocol-Version, Mcp-Method, Mcp-Name, Mcp-Session-Id");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id");
}
}
