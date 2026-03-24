#include "http_server.hh"

#include "../debug.hh"
#include "../string.hh"
#include "../url.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace oro::runtime::mcp {
  namespace {
    String randomSessionId() {
      return String("session-") + std::to_string(crypto::rand64());
    }

    bool isValidSessionIdToken(const String& value) {
      if (value.empty()) return false;
      if (value.size() > 128) return false;
      for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_') continue;
        return false;
      }
      return true;
    }

    String randomToken(size_t length) {
      static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
      static constexpr size_t alphabetSize = sizeof(alphabet) - 1;
      String token;
      token.reserve(length);
      while (token.size() < length) {
        uint64_t value = crypto::rand64();
        for (int i = 0; i < 11 && token.size() < length; ++i) {
          const auto index = static_cast<size_t>(value % alphabetSize);
          token.push_back(alphabet[index]);
          value /= alphabetSize;
        }
      }
    return token;
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
        if (c == '\r' || c == '\n') {
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
  }

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

  void HTTPServer::EventDispatcher::send(const String& payload) {
    {
      Lock lock(this->mutex);
      if (this->closed) {
        return;
      }
      this->queue.push(payload);
    }
    this->cond.notify_one();
  }

  void HTTPServer::EventDispatcher::close() {
    {
      Lock lock(this->mutex);
      this->closed = true;
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

    String endpointNormalized = normalizePath(cfg.endpoint);
    if (endpointNormalized == "/" && ::oro::runtime::string::trim(cfg.endpoint).empty()) {
      endpointNormalized = "/mcp";
    }

    this->server = std::make_unique<httplib::Server>();
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
    session->dispatcher->send(payload);
    return true;
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

    if (req.method != "POST") {
      res.status = 405;
      res.set_header("Allow", "POST");
      res.set_content("Method Not Allowed", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (this->delegate == nullptr) {
      res.status = 503;
      res.set_content("MCP server not ready", "text/plain");
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

      if (!parsed.is_object()) {
        res.status = 400;
        res.set_content("Invalid JSON-RPC payload", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      const bool hasMethod = parsed.contains("method") && parsed["method"].is_string();
      const String method = hasMethod ? parsed["method"].get<std::string>() : "";
      const bool isInitialize = hasMethod && method == "initialize";

      const bool hasId = parsed.contains("id");
      const bool idIsNull = !hasId || parsed["id"].is_null();
      const bool isNotification = hasMethod && idIsNull;
      const bool isRequest = hasMethod && !idIsNull;

      const bool isResponse = !hasMethod &&
        parsed.contains("jsonrpc") &&
        parsed.contains("id") &&
        (parsed.contains("result") || parsed.contains("error"));

      const auto headerSessionId = normalizeSessionIdValue(req.get_header_value("Mcp-Session-Id"));
      const auto querySessionId = normalizeSessionIdValue(req.get_param_value("session_id"));
      const auto clientSessionId = !headerSessionId.empty() ? headerSessionId : querySessionId;
      const bool clientProvidedSessionId = !clientSessionId.empty();

      std::optional<String> sessionId;
      if (clientProvidedSessionId) {
        sessionId = this->validateSessionId(req);
        if (!sessionId.has_value()) {
          sessionId = this->createSessionForRequest(clientSessionId);
        }
      } else if (!isInitialize) {
        res.status = 400;
        res.set_content("Missing Mcp-Session-Id", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      if (!sessionId.has_value()) {
        sessionId = this->createSessionForRequest();
        if (!sessionId.has_value()) {
          res.status = 500;
          res.set_content("Failed to create session", "text/plain");
          this->setCorsHeaders(res);
          return;
        }
      }

      {
        Lock lock(this->mutex);
        auto it = this->sessions.find(*sessionId);
        if (it != this->sessions.end() && it->second) {
          it->second->lastActivity = std::chrono::steady_clock::now();
        }
      }

      debug("MCP HTTP message received for session %s", sessionId->c_str());

      if (isResponse) {
        // This server does not currently issue JSON-RPC requests to clients.
        // Accept and ignore any responses from clients to remain compliant with Streamable HTTP.
        this->attachSessionHeader(res, *sessionId);
        res.status = 202;
        this->setCorsHeaders(res);
        return;
      }

      auto immediate = this->delegate->onJsonRpcRequest(*sessionId, req.body);
      this->attachSessionHeader(res, *sessionId);

      if (isNotification) {
        res.status = 202;
        this->setCorsHeaders(res);
        return;
      }

      if (!isRequest || !immediate.has_value()) {
        res.status = 500;
        res.set_content("Invalid MCP request handling", "text/plain");
        this->setCorsHeaders(res);
        return;
      }

      res.status = 200;
      res.set_content(*immediate, "application/json");
      this->setCorsHeaders(res);
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
            session->dispatcher = std::make_shared<EventDispatcher>();
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
	      auto createdId = this->createSessionForRequest(requestedSessionId);
	      if (!createdId.has_value()) {
	        res.status = 500;
	        res.set_content("Failed to create session", "text/plain");
	        this->setCorsHeaders(res);
	        return;
	      }

	      Lock lock(this->mutex);
	      auto it = this->sessions.find(*createdId);
	      if (it != this->sessions.end() && it->second) {
	        session = it->second;
	        if (!session->dispatcher || session->closed.load()) {
	          session->dispatcher = std::make_shared<EventDispatcher>();
	        }
	        dispatcher = session->dispatcher;
	        session->closed = false;
	        session->hasStream = true;
	        session->lastActivity = std::chrono::steady_clock::now();
	      }
	    }

	    if (!session) {
	      res.status = 500;
	      res.set_content("MCP session unavailable", "text/plain");
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

      bool active = false;
      {
        Lock lock(this->mutex);
        if (session->dispatcher == dispatcher) {
          session->closed = true;
          session->hasStream = false;
        }

        auto it = this->sessions.find(session->id);
        if (it != this->sessions.end() && it->second) {
          it->second->lastActivity = std::chrono::steady_clock::now();
          active = it->second->hasStream.load();
        }
      }

      if (this->delegate && !active) {
        this->delegate->onSessionStopped(session->id);
      }
    });
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

    const String responseType = ::oro::runtime::string::trim(req.get_param_value("response_type"));
    String clientId = ::oro::runtime::string::trim(req.get_param_value("client_id"));
    String redirectUri = ::oro::runtime::string::trim(req.get_param_value("redirect_uri"));
    String scope = ::oro::runtime::string::trim(req.get_param_value("scope"));
    const String state = req.get_param_value("state");
    String codeChallenge = ::oro::runtime::string::trim(req.get_param_value("code_challenge"));
    String codeChallengeMethod = ::oro::runtime::string::trim(req.get_param_value("code_challenge_method"));

    if (codeChallengeMethod.empty()) {
      codeChallengeMethod = "plain";
    }

    auto invalidRequest = [this, &res](const String& message) {
      res.status = 400;
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_content(message.c_str(), "text/plain");
      this->setCorsHeaders(res);
    };

    if (responseType != "code") {
      invalidRequest("Unsupported response_type");
      return;
    }

    if (redirectUri.empty()) {
      invalidRequest("Missing redirect_uri");
      return;
    }

    if (!isValidRedirectUri(redirectUri)) {
      invalidRequest("Invalid redirect_uri");
      return;
    }

    const auto methodLower = toLower(codeChallengeMethod);
    if (codeChallenge.empty() || (methodLower != "plain" && methodLower != "s256")) {
      invalidRequest("Invalid code_challenge or code_challenge_method");
      return;
    }

    const String effectiveScope = scope.empty() ? oauthConfig.defaultScope : scope;

    if (req.method == "GET") {
      auto html = this->renderAuthorizationPage(
        req,
        oauthConfig,
        clientId.empty() ? oauthConfig.defaultClientId : clientId,
        redirectUri,
        effectiveScope,
        state,
        codeChallenge,
        codeChallengeMethod
      );
      res.status = 200;
      res.set_header("Content-Type", "text/html; charset=utf-8");
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_content(html.c_str(), "text/html; charset=utf-8");
      this->setCorsHeaders(res);
      return;
    }

    const auto decision = toLower(::oro::runtime::string::trim(req.get_param_value("decision")));
    if (decision.empty()) {
      invalidRequest("Missing decision");
      return;
    }

    if (decision == "deny") {
      String location = appendQueryParameter(redirectUri, "error", "access_denied");
      if (!state.empty()) {
        location = appendQueryParameter(location, "state", state);
      }
      res.status = 302;
      res.set_header("Location", location.c_str());
      res.set_header("Cache-Control", "no-store");
      res.set_header("Pragma", "no-cache");
      res.set_content("Access denied", "text/plain");
      this->setCorsHeaders(res);
      return;
    }

    if (decision != "approve") {
      invalidRequest("Invalid decision");
      return;
    }

    if (clientId.empty()) {
      invalidRequest("Missing client_id");
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    OAuthAuthorizationCode authorizationCode;
    authorizationCode.code = randomToken(64);
    authorizationCode.clientId = clientId;
    authorizationCode.redirectUri = redirectUri;
    authorizationCode.scope = effectiveScope;
    authorizationCode.state = state;
    authorizationCode.codeChallenge = codeChallenge;
    authorizationCode.codeChallengeMethod = codeChallengeMethod;
    const uint32_t codeLifetime = oauthConfig.codeLifetimeSeconds > 0 ? oauthConfig.codeLifetimeSeconds : 300;
    authorizationCode.expiresAt = now + std::chrono::seconds(codeLifetime);

    {
      Lock lock(this->mutex);
      this->pruneExpiredOAuthArtifacts(now);
      this->oauthAuthorizationCodes.insert_or_assign(authorizationCode.code, authorizationCode);
    }

    String location = appendQueryParameter(redirectUri, "code", authorizationCode.code);
    if (!state.empty()) {
      location = appendQueryParameter(location, "state", state);
    }

    res.status = 302;
    res.set_header("Location", location.c_str());
    res.set_header("Cache-Control", "no-store");
    res.set_header("Pragma", "no-cache");
    res.set_content("Authorization complete", "text/plain");
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

    const String grantType = req.get_param_value("grant_type");
    if (grantType != "authorization_code") {
      errorResponse("unsupported_grant_type", "Only the authorization_code grant type is supported");
      return;
    }

    const String code = req.get_param_value("code");
    const String redirectUri = ::oro::runtime::string::trim(req.get_param_value("redirect_uri"));
    const String clientId = ::oro::runtime::string::trim(req.get_param_value("client_id"));
    const String codeVerifier = req.get_param_value("code_verifier");

    if (code.empty() || redirectUri.empty() || clientId.empty() || codeVerifier.empty()) {
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

    if (!verifyCodeChallenge(codeVerifier, authorizationCode.codeChallenge, authorizationCode.codeChallengeMethod)) {
      errorResponse("invalid_grant", "Code verifier mismatch");
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    OAuthAccessToken accessToken;
    accessToken.token = randomToken(96);
    accessToken.clientId = clientId;
    accessToken.scope = authorizationCode.scope;
    const uint32_t tokenLifetime = oauthConfig.tokenLifetimeSeconds > 0 ? oauthConfig.tokenLifetimeSeconds : 3600;
    accessToken.expiresAt = now + std::chrono::seconds(tokenLifetime);

    {
      Lock lock(this->mutex);
      this->pruneExpiredOAuthArtifacts(now);
      this->oauthAccessTokens.insert_or_assign(accessToken.token, accessToken);
    }

    nlohmann::json payload;
    payload["access_token"] = accessToken.token.c_str();
    payload["token_type"] = "bearer";
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

    String host = currentConfig.host.empty() ? String("127.0.0.1") : currentConfig.host;
    bool isIpv6 = host.find(':') != String::npos;
    std::ostringstream base;
    base << "http://";
    if (isIpv6) {
      base << '[' << host << ']';
    } else {
      base << host;
    }
    if (currentConfig.port > 0 && currentConfig.port != 80) {
      base << ':' << currentConfig.port;
    }
    String baseUrl = base.str();

    auto composeEndpoint = [&](const String& path) -> String {
      String root = !oauthConfig.issuer.empty() ? oauthConfig.issuer : baseUrl;
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
    metadata["issuer"] = (!oauthConfig.issuer.empty() ? oauthConfig.issuer : baseUrl).c_str();
    metadata["authorization_endpoint"] = composeEndpoint(oauthConfig.authorizePath.empty() ? "/oauth/authorize" : oauthConfig.authorizePath).c_str();
    metadata["token_endpoint"] = composeEndpoint(oauthConfig.tokenPath.empty() ? "/oauth/token" : oauthConfig.tokenPath).c_str();
    metadata["response_types_supported"] = nlohmann::json::array({"code"});
    metadata["grant_types_supported"] = nlohmann::json::array({"authorization_code"});
    metadata["code_challenge_methods_supported"] = nlohmann::json::array({"S256", "plain"});
    metadata["token_endpoint_auth_methods_supported"] = nlohmann::json::array({"none"});

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
    if (this->delegate) {
      auto decision = this->delegate->authorize(req, res);
      if (decision.has_value()) {
        return *decision;
      }
    }

    String staticToken;
    OAuthConfig oauthConfig;
    {
      Lock lock(this->mutex);
      staticToken = this->config.token;
      oauthConfig = this->config.oauth;
    }

    const bool oauthEnabled = oauthConfig.enabled;
    const bool authEnabled = !staticToken.empty() || oauthEnabled;

    const auto origin = ::oro::runtime::string::trim(req.get_header_value("Origin"));
    if (!origin.empty() && !authEnabled) {
      // With no auth configured, reject non-loopback browser origins to avoid DNS rebinding attacks.
      bool allowed = false;
      if (origin == "null") {
        allowed = true;
      } else {
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

    const auto queryToken = trim(req.get_param_value("token"));
    if (!queryToken.empty()) {
      if (!staticToken.empty() && queryToken == staticToken) {
        staticMatch = true;
      } else {
        candidates.push_back(queryToken);
      }
    }

    const auto accessTokenParam = trim(req.get_param_value("access_token"));
    if (!accessTokenParam.empty()) {
      if (!staticToken.empty() && accessTokenParam == staticToken) {
        staticMatch = true;
      } else {
        candidates.push_back(accessTokenParam);
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
  res.set_header("WWW-Authenticate", "Bearer");
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
    return this->oauthAccessTokens.find(token) != this->oauthAccessTokens.end();
  }

  void HTTPServer::pruneExpiredOAuthArtifacts(std::chrono::steady_clock::time_point now) const {
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
                                             const String& codeChallengeMethod) const {
    nlohmann::json context = nlohmann::json::object();
    context["clientId"] = clientId.c_str();
    context["redirectUri"] = redirectUri.c_str();
    context["scope"] = scope.c_str();
    context["state"] = state.c_str();
    context["codeChallenge"] = codeChallenge.c_str();
    context["codeChallengeMethod"] = codeChallengeMethod.c_str();
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

    String contextJson = context.dump();
    const String closingTag = "</script>";
    size_t pos = 0;
    while ((pos = contextJson.find(closingTag, pos)) != String::npos) {
      contextJson.insert(pos + 1, "\\");
      pos += closingTag.size() + 1;
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
         << "<p>The client below is requesting permission to access your Socket MCP server.</p>"
         << "<dl class=\"details\">"
         << "<dt>Client ID</dt><dd>" << htmlEscape(clientId.empty() ? oauthConfig.defaultClientId : clientId) << "</dd>"
         << "<dt>Redirect URI</dt><dd>" << htmlEscape(redirectUri) << "</dd>";

    if (!scope.empty()) {
      html << "<dt>Scope</dt><dd>" << htmlEscape(scope) << "</dd>";
    }

    if (!state.empty()) {
      html << "<dt>State</dt><dd>" << htmlEscape(state) << "</dd>";
    }

    html << "<dt>PKCE Method</dt><dd>" << htmlEscape(codeChallengeMethod) << "</dd>"
         << "</dl>"
         << "<form method=\"post\" action=\"" << htmlEscape(req.path) << "\">"
         << "<input type=\"hidden\" name=\"response_type\" value=\"code\" />"
         << "<input type=\"hidden\" name=\"client_id\" value=\"" << htmlEscape(clientId.empty() ? oauthConfig.defaultClientId : clientId) << "\" />"
         << "<input type=\"hidden\" name=\"redirect_uri\" value=\"" << htmlEscape(redirectUri) << "\" />"
         << "<input type=\"hidden\" name=\"scope\" value=\"" << htmlEscape(scope) << "\" />"
         << "<input type=\"hidden\" name=\"state\" value=\"" << htmlEscape(state) << "\" />"
         << "<input type=\"hidden\" name=\"code_challenge\" value=\"" << htmlEscape(codeChallenge) << "\" />"
         << "<input type=\"hidden\" name=\"code_challenge_method\" value=\"" << htmlEscape(codeChallengeMethod) << "\" />"
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
    session->dispatcher = std::make_shared<EventDispatcher>();
    session->closed = false;
    session->hasStream = false;
    session->lastActivity = std::chrono::steady_clock::now();

    {
      Lock lock(this->mutex);
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
  res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, Mcp-Session-Id");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id");
}
}
