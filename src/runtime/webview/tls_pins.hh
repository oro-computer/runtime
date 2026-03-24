#ifndef ORO_RUNTIME_WEBVIEW_TLS_PINS_H
#define ORO_RUNTIME_WEBVIEW_TLS_PINS_H

#include "../platform.hh"

namespace oro::runtime::webview {
  struct TlsPin {
    String algorithm;
    String value;
  };

  using TlsPinList = Vector<TlsPin>;
  using TlsPinMap = Map<String, TlsPinList>;

  struct TlsPinConfigIssue {
    size_t line = 0; // 1-based line number
    String message;
  };

  /**
   * Normalize a host value for TLS pin lookups.
   *
   * - Trims whitespace and lowercases.
   * - Accepts full URLs and strips scheme/path/query/fragment.
   * - Removes a trailing '.' (fully-qualified domain name form).
   * - Strips an appended numeric port (e.g., "example.com:443").
   * - For bracketed IPv6 literals ("[::1]:443"), returns the address without
   *   brackets and ignores any port suffix.
   */
  String normaliseTlsPinHost (const String& host);

  /**
   * Normalize a host value for TLS pin configuration entries.
   *
   * This is similar to `normaliseTlsPinHost()`, but preserves an appended
   * numeric port suffix when present:
   *
   * - "example.com:443" -> "example.com:443"
   * - "[::1]:443" -> "[::1]:443"
   *
   * When no port suffix exists, this returns the same value as
   * `normaliseTlsPinHost()`.
   */
  String normaliseTlsPinEndpoint (const String& host);

  /**
   * Parse a TLS pin configuration string into a map of host -> pins.
   *
   * The configuration string is expected to be a newline-delimited list of
   * entries. Each non-empty, non-comment line should have the form:
   *
   *   <host> <pin> [<pin>...]
   *
   * where <pin> is either "sha256/<base64>" or just "<base64>". Hosts are
   * normalised with `normaliseTlsPinEndpoint()` and stored in canonical form.
   * A `<host>:<port>` entry applies only to that port. A `<host>` entry applies
   * to any port unless a more specific `<host>:<port>` entry exists.
   *
   * The <base64> portion may be provided as standard Base64 or Base64URL
   * (using '-' and '_' in place of '+' and '/'), with or without padding. Pins
   * are normalised to standard Base64 and validated to represent a SHA-256
   * digest (32 bytes).
   *
   * When `includeEmptyHosts` is true, a host entry is created even if no valid
   * pins are found for that line. This enables fail-closed behaviour for
   * security-sensitive callers (a configured host with no valid pins will
   * reject all certificates).
   */
  TlsPinMap parseTlsPinConfig (const String& raw, bool includeEmptyHosts = false);

  /**
   * Validate a TLS pin configuration string and return any issues found.
   *
   * This does not modify behaviour. It is intended to improve observability by
   * surfacing invalid hosts or pin tokens that would otherwise be ignored
   * (or cause fail-closed pinning with empty pin lists).
   */
  Vector<TlsPinConfigIssue> diagnoseTlsPinConfig (const String& raw);

  /**
   * Check whether a certificate identified by its SHA-256 digest (binary) is
   * allowed for the given host (or host:port), according to the provided pin map.
   *
   * When no pins are configured for `host`, this returns true. When pins are
   * present, this returns true only if at least one pin for the most specific
   * matching entry matches the certificate digest:
   *
   * - If `host` includes a port and `<host>:<port>` exists, only that entry is
   *   evaluated.
   * - Otherwise, `<host>` is evaluated.
   */
  bool isCertificateAllowedForHost (
    const TlsPinMap& pins,
    const String& host,
    const Vector<uint8_t>& certSha256
  );

  /**
   * Returns true when pins apply to `host` (or host:port).
   *
   * This is true when either an exact `<host>:<port>` entry exists, or a
   * fallback `<host>` entry exists (even if its list is empty due to invalid or
   * missing pins).
   *
   * This is useful for callers that need to preserve the platform default TLS
   * behaviour when no pins apply to a given host.
   */
  bool hasPinsForHost (const TlsPinMap& pins, const String& host);
}

#endif
