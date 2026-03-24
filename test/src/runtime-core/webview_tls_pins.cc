#include "tests.hh"
#include "src/runtime/bytes.hh"
#include "src/runtime/webview/tls_pins.hh"

namespace oro::Tests {
  void webview_tls_pins (Harness& t) {
    using oro::runtime::Vector;
    using oro::runtime::String;
    using oro::runtime::bytes::base64::encode;
    using oro::runtime::webview::parseTlsPinConfig;
    using oro::runtime::webview::hasPinsForHost;
    using oro::runtime::webview::isCertificateAllowedForHost;

    t.test("webview tls pins: parse normalizes host and accumulates pins", [](auto t) {
      Vector<uint8_t> digest(32);
      for (size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<uint8_t>(i);
      }
      const auto digestB64 = encode(digest);

      const String raw =
        "\n"
        "# comment-only\n"
        "Example.COM\tSHA256/" + digestB64 + " extra\t# trailing\n"
        "example.com " + digestB64 + "\n"
        "badline\n";

      const auto pins = parseTlsPinConfig(raw);

      t.equals(pins.size(), static_cast<size_t>(1), "pins map has one host");
      t.assert(hasPinsForHost(pins, "example.com"), "pins apply to example.com");
      t.assert(hasPinsForHost(pins, "EXAMPLE.COM"), "pins match host case-insensitively");
      t.assert(hasPinsForHost(pins, " example.com "), "pins match trimmed host");

      const auto it = pins.find("example.com");
      t.assert(it != pins.end(), "example.com entry exists");
      t.equals(it->second.size(), static_cast<size_t>(2), "example.com has two pins");
      t.equals(it->second[0].algorithm, "sha256", "pin[0] algorithm");
      t.equals(it->second[0].value, digestB64, "pin[0] value");
      t.equals(it->second[1].algorithm, "sha256", "pin[1] algorithm");
      t.equals(it->second[1].value, digestB64, "pin[1] value");
    });

    t.test("webview tls pins: base64url pins are accepted and normalized", [](auto t) {
      Vector<uint8_t> digest(32, 0xFF);
      digest[0] = 0xFB;

      const auto digestB64 = encode(digest);
      String digestB64Url = digestB64;
      for (auto& ch : digestB64Url) {
        if (ch == '+') {
          ch = '-';
        } else if (ch == '/') {
          ch = '_';
        }
      }
      while (!digestB64Url.empty() && digestB64Url.back() == '=') {
        digestB64Url.pop_back();
      }

      const String raw = "example.com sha256/" + digestB64Url + "\n";
      const auto pins = parseTlsPinConfig(raw);

      const auto it = pins.find("example.com");
      t.assert(it != pins.end(), "example.com entry exists");
      t.equals(it->second.size(), static_cast<size_t>(1), "example.com has one pin");
      t.equals(it->second[0].value, digestB64, "pin value normalized to standard base64");
      t.assert(isCertificateAllowedForHost(pins, "example.com", digest), "normalized pin allows digest");
    });

    t.test("webview tls pins: isCertificateAllowedForHost honours per-host pins", [](auto t) {
      Vector<uint8_t> digest(32);
      Vector<uint8_t> otherDigest(32);
      for (size_t i = 0; i < 32; ++i) {
        digest[i] = static_cast<uint8_t>(i + 10);
        otherDigest[i] = static_cast<uint8_t>(i + 42);
      }
      const auto digestB64 = encode(digest);

      const String raw = "example.com sha256/" + digestB64 + "\n";
      const auto pins = parseTlsPinConfig(raw);

      t.assert(isCertificateAllowedForHost(pins, "example.com", digest), "pinned host allows matching digest");
      t.assert(!isCertificateAllowedForHost(pins, "example.com", otherDigest), "pinned host rejects mismatched digest");

      // Unpinned hosts preserve platform default behaviour.
      t.assert(isCertificateAllowedForHost(pins, "other.example", Vector<uint8_t>{}), "unpinned host allows empty digest");
      t.assert(isCertificateAllowedForHost(pins, "other.example", otherDigest), "unpinned host allows any digest");

      // Pinned hosts require a certificate digest to match.
      t.assert(!isCertificateAllowedForHost(pins, "example.com", Vector<uint8_t>{}), "pinned host rejects empty digest");
    });

    t.test("webview tls pins: port-specific pins override host pins", [](auto t) {
      Vector<uint8_t> hostDigest(32);
      Vector<uint8_t> portDigest(32);
      for (size_t i = 0; i < 32; ++i) {
        hostDigest[i] = static_cast<uint8_t>(i + 1);
        portDigest[i] = static_cast<uint8_t>(i + 101);
      }
      const auto hostB64 = encode(hostDigest);
      const auto portB64 = encode(portDigest);

      const String raw =
        "example.com sha256/" + hostB64 + "\n"
        "example.com:443 sha256/" + portB64 + "\n";

      const auto pins = parseTlsPinConfig(raw);

      t.equals(pins.size(), static_cast<size_t>(2), "pins map includes host and host:port entries");
      t.assert(hasPinsForHost(pins, "example.com:444"), "host pins apply to other ports by fallback");

      t.assert(isCertificateAllowedForHost(pins, "example.com:443", portDigest), "host:port pin allows matching digest");
      t.assert(!isCertificateAllowedForHost(pins, "example.com:443", hostDigest), "host:port pin overrides host pin");

      t.assert(isCertificateAllowedForHost(pins, "example.com:444", hostDigest), "host pin applies when no host:port entry exists");
      t.assert(!isCertificateAllowedForHost(pins, "example.com:444", portDigest), "mismatched digest is rejected for pinned host");
    });
  }
}
