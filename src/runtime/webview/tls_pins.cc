#include "tls_pins.hh"

#include "../bytes.hh"
#include "../string.hh"

#include <cctype>
#include <string>

namespace oro::runtime::webview {
  using oro::runtime::bytes::base64::encode;
  using oro::runtime::bytes::base64::decode;
  using oro::runtime::string::split;
  using oro::runtime::string::toLowerCase;
  using oro::runtime::string::trim;

  namespace {
    inline bool isWhitespace (char c) {
      return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    Vector<String> splitWhitespace (const String& value) {
      Vector<String> tokens;
      String token;

      for (const auto ch : value) {
        if (isWhitespace(ch)) {
          if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
          }
          continue;
        }
        token.push_back(ch);
      }

      if (!token.empty()) {
        tokens.push_back(token);
      }

      return tokens;
    }

    String normaliseHost (const String& rawHost) {
      auto host = trim(rawHost);
      if (host.empty()) {
        return "";
      }

      host = toLowerCase(host);

      const auto scheme = host.find("://");
      if (scheme != String::npos) {
        host = host.substr(scheme + 3);
      } else if (host.rfind("//", 0) == 0) {
        host = host.substr(2);
      }

      const auto path = host.find_first_of("/?#");
      if (path != String::npos) {
        host = host.substr(0, path);
      }

      const auto at = host.rfind('@');
      if (at != String::npos) {
        host = host.substr(at + 1);
      }

      host = trim(host);
      if (host.empty()) {
        return "";
      }

      while (!host.empty() && host.back() == '.') {
        host.pop_back();
      }

      if (host.empty()) {
        return "";
      }

      if (!host.empty() && host.front() == '[') {
        const auto end = host.find(']');
        if (end == String::npos || end <= 1) {
          return "";
        }

        const auto inner = host.substr(1, end - 1);
        if (inner.empty()) {
          return "";
        }

        const auto suffix = end + 1 < host.size()
          ? host.substr(end + 1)
          : "";

        if (suffix.empty()) {
          return inner;
        }

        if (suffix.front() != ':' || suffix.size() == 1) {
          return "";
        }

        for (size_t i = 1; i < suffix.size(); ++i) {
          if (!std::isdigit(static_cast<unsigned char>(suffix[i]))) {
            return "";
          }
        }

        return inner;
      }

      const auto colon = host.rfind(':');
      if (colon != String::npos) {
        const auto prefix = host.substr(0, colon);
        const auto suffix = colon + 1 < host.size()
          ? host.substr(colon + 1)
          : "";

        if (prefix.find(':') == String::npos) {
          if (prefix.empty() || suffix.empty()) {
            return "";
          }

          bool digitsOnly = true;
          for (const auto ch : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
              digitsOnly = false;
              break;
            }
          }

          if (!digitsOnly) {
            return "";
          }

          host = prefix;
          while (!host.empty() && host.back() == '.') {
            host.pop_back();
          }

          if (host.empty()) {
            return "";
          }
        }
      }

      return host;
    }

    String normaliseEndpoint (const String& rawHost) {
      auto host = trim(rawHost);
      if (host.empty()) {
        return "";
      }

      host = toLowerCase(host);

      const auto scheme = host.find("://");
      if (scheme != String::npos) {
        host = host.substr(scheme + 3);
      } else if (host.rfind("//", 0) == 0) {
        host = host.substr(2);
      }

      const auto path = host.find_first_of("/?#");
      if (path != String::npos) {
        host = host.substr(0, path);
      }

      const auto at = host.rfind('@');
      if (at != String::npos) {
        host = host.substr(at + 1);
      }

      host = trim(host);
      if (host.empty()) {
        return "";
      }

      while (!host.empty() && host.back() == '.') {
        host.pop_back();
      }

      if (host.empty()) {
        return "";
      }

      if (!host.empty() && host.front() == '[') {
        const auto end = host.find(']');
        if (end == String::npos || end <= 1) {
          return "";
        }

        const auto inner = host.substr(1, end - 1);
        if (inner.empty()) {
          return "";
        }

        const auto suffix = end + 1 < host.size()
          ? host.substr(end + 1)
          : "";

        if (suffix.empty()) {
          return inner;
        }

        if (suffix.front() != ':' || suffix.size() == 1) {
          return "";
        }

        for (size_t i = 1; i < suffix.size(); ++i) {
          if (!std::isdigit(static_cast<unsigned char>(suffix[i]))) {
            return "";
          }
        }

        return "[" + inner + "]" + suffix;
      }

      const auto colon = host.rfind(':');
      if (colon != String::npos) {
        const auto prefix = host.substr(0, colon);
        const auto suffix = colon + 1 < host.size()
          ? host.substr(colon + 1)
          : "";

        if (prefix.find(':') == String::npos) {
          if (prefix.empty() || suffix.empty()) {
            return "";
          }

          bool digitsOnly = true;
          for (const auto ch : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
              digitsOnly = false;
              break;
            }
          }

          if (!digitsOnly) {
            return "";
          }

          auto base = prefix;
          while (!base.empty() && base.back() == '.') {
            base.pop_back();
          }

          if (base.empty()) {
            return "";
          }

          return base + ":" + suffix;
        }
      }

      return host;
    }

    String normalisePinBase64Value (const String& rawValue) {
      String value = trim(rawValue);
      if (value.empty()) {
        return "";
      }

      // Normalise Base64URL -> Base64.
      for (auto& ch : value) {
        if (ch == '-') {
          ch = '+';
        } else if (ch == '_') {
          ch = '/';
        }
      }

      // Ensure proper padding for decoding.
      const auto remainder = value.size() % 4;
      if (remainder != 0) {
        const auto pad = 4 - remainder;
        value.append(pad, '=');
      }

      const auto decoded = decode(value);
      if (!value.empty() && decoded.empty()) {
        return "";
      }

      if (decoded.size() != 32) {
        return "";
      }

      Vector<uint8_t> digest(decoded.begin(), decoded.end());
      return encode(digest);
    }

    TlsPin parsePinToken (const String& token) {
      TlsPin pin;
      auto value = trim(token);

      if (toLowerCase(value).rfind("sha256/", 0) == 0) {
        pin.algorithm = "sha256";
        pin.value = value.substr(String("sha256/").size());
      } else {
        pin.algorithm = "sha256";
        pin.value = value;
      }

      return pin;
    }
  }

  String normaliseTlsPinHost (const String& host) {
    return normaliseHost(host);
  }

  String normaliseTlsPinEndpoint (const String& host) {
    return normaliseEndpoint(host);
  }

  TlsPinMap parseTlsPinConfig (const String& raw, bool includeEmptyHosts) {
    TlsPinMap result;

    if (raw.empty()) {
      return result;
    }

    const auto lines = split(raw, '\n');

    for (auto line : lines) {
      line = trim(line);

      if (line.empty()) {
        continue;
      }

      // Strip trailing comments.
      const auto comment = line.find_first_of("#;");
      if (comment != String::npos) {
        line = trim(line.substr(0, comment));
      }

      if (line.empty()) {
        continue;
      }

      const auto tokens = splitWhitespace(line);
      if (tokens.empty()) {
        continue;
      }

      // Guard against accidentally providing pin tokens where a host is expected.
      {
        const auto firstToken = trim(tokens[0]);
        const auto lowerFirst = toLowerCase(firstToken);
        if (lowerFirst.rfind("sha256/", 0) == 0) {
          continue;
        }

        const auto maybePin = parsePinToken(firstToken);
        if (!normalisePinBase64Value(maybePin.value).empty()) {
          continue;
        }
      }

      auto host = normaliseEndpoint(tokens[0]);
      if (host.empty()) {
        continue;
      }

      if (includeEmptyHosts) {
        (void) result[host];
      }

      if (tokens.size() < 2) {
        continue;
      }

      for (size_t i = 1; i < tokens.size(); ++i) {
        auto pin = parsePinToken(tokens[i]);
        if (pin.algorithm != "sha256") {
          continue;
        }

        pin.value = normalisePinBase64Value(pin.value);
        if (pin.value.empty()) {
          continue;
        }

        result[host].push_back(pin);
      }
    }

    return result;
  }

  Vector<TlsPinConfigIssue> diagnoseTlsPinConfig (const String& raw) {
    Vector<TlsPinConfigIssue> issues;

    if (trim(raw).empty()) {
      return issues;
    }

    const auto lines = split(raw, '\n');
    size_t lineNumber = 0;

    for (auto line : lines) {
      ++lineNumber;
      line = trim(line);

      if (line.empty()) {
        continue;
      }

      const auto comment = line.find_first_of("#;");
      if (comment != String::npos) {
        line = trim(line.substr(0, comment));
      }

      if (line.empty()) {
        continue;
      }

      const auto tokens = splitWhitespace(line);
      if (tokens.empty()) {
        continue;
      }

      const auto firstToken = trim(tokens[0]);
      const auto lowerFirst = toLowerCase(firstToken);
      if (lowerFirst.rfind("sha256/", 0) == 0) {
        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Pins entries must begin with a host, not a pin token"
        });
        continue;
      }

      const auto maybePin = parsePinToken(firstToken);
      if (!normalisePinBase64Value(maybePin.value).empty()) {
        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Pins entries must begin with a host, not a pin token"
        });
        continue;
      }

      const auto host = normaliseEndpoint(firstToken);
      if (host.empty()) {
        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Invalid host in pins entry"
        });
        continue;
      }

      if (tokens.size() < 2) {
        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Pins entry for '" + host + "' has no pin tokens (will reject all certificates)"
        });
        continue;
      }

      Vector<String> badPins;
      size_t validPins = 0;

      for (size_t i = 1; i < tokens.size(); ++i) {
        auto pin = parsePinToken(tokens[i]);
        pin.value = normalisePinBase64Value(pin.value);
        if (pin.value.empty()) {
          badPins.push_back(tokens[i]);
          continue;
        }
        ++validPins;
      }

      if (validPins == 0) {
        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Pins entry for '" + host + "' has no valid sha256 pin tokens (will reject all certificates)"
        });
        continue;
      }

      if (!badPins.empty()) {
        String detail;
        const size_t maxTokens = 3;
        for (size_t i = 0; i < badPins.size() && i < maxTokens; ++i) {
          if (!detail.empty()) {
            detail += ", ";
          }
          detail += "'" + badPins[i] + "'";
        }

        if (badPins.size() > maxTokens) {
          detail += ", and " + std::to_string(badPins.size() - maxTokens) + " more";
        }

        issues.push_back(TlsPinConfigIssue {
          lineNumber,
          "Pins entry for '" + host + "' contains invalid sha256 pin token(s): " + detail
        });
      }
    }

    return issues;
  }

  bool isCertificateAllowedForHost (
    const TlsPinMap& pins,
    const String& host,
    const Vector<uint8_t>& certSha256
  ) {
    if (host.empty()) {
      return true;
    }

    const auto endpointKey = normaliseEndpoint(host);
    if (endpointKey.empty()) {
      return true;
    }

    auto it = pins.find(endpointKey);
    if (it == pins.end()) {
      const auto hostKey = normaliseHost(host);
      if (hostKey.empty()) {
        return true;
      }

      it = pins.find(hostKey);
    }

    // No pins configured for this host (or host:port).
    if (it == pins.end()) {
      return true;
    }

    if (certSha256.empty()) {
      return false;
    }

    const auto digestB64 = encode(certSha256);

    for (const auto& pin : it->second) {
      if (pin.algorithm != "sha256") {
        continue;
      }

      if (pin.value == digestB64) {
        return true;
      }
    }

    return false;
  }

  bool hasPinsForHost (const TlsPinMap& pins, const String& host) {
    if (host.empty()) {
      return false;
    }

    const auto endpointKey = normaliseEndpoint(host);
    if (endpointKey.empty()) {
      return false;
    }
    auto it = pins.find(endpointKey);

    if (it == pins.end()) {
      const auto hostKey = normaliseHost(host);
      if (hostKey.empty()) {
        return false;
      }
      it = pins.find(hostKey);
    }

    return it != pins.end();
  }
}
