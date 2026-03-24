#include "../string.hh"
#include "../http.hh"

using oro::runtime::string::split;

namespace oro::runtime::http {
  Request::Request (
    const String& input,
    const String& scheme,
    const String& method
  ) : scheme(scheme),
      method(method) {
    const auto crlf = input.find("\r\n");
    const auto headerEnd = input.find("\r\n\r\n");
    this->url.scheme = scheme;
    this->url.hostname = "127.0.0.1";
    if (crlf != String::npos) {
      auto stream = std::istringstream(input.substr(0, crlf));
      String pathname;
      String version;
      stream
        >> this->method
        >> pathname
        >> version;

      const auto versionParts = split(version, '/');
      const auto pathParts = split(pathname, '?');

      if (versionParts.size() == 2) {
        this->version = versionParts[1];
      }

      if (pathParts.size() == 2) {
        this->url.pathname = pathParts[0];
        this->url.search = "?" + pathParts[1];
        this->url.searchParams.set(pathParts[1]);
      } else {
        this->url.pathname = pathname;
      }

      if (headerEnd != String::npos && headerEnd > crlf + 2) {
        this->headers = input.substr(crlf + 2, headerEnd - (crlf + 2));
      } else if (crlf + 2 < input.size()) {
        // If the header terminator isn't present, treat the remainder as headers.
        this->headers = input.substr(crlf + 2);
      }

      if (headerEnd != String::npos) {
        const auto bodyStart = headerEnd + 4;
        if (bodyStart < input.size()) {
          this->body = input.substr(bodyStart);
        }
      }

      const auto host = this->headers.get("host");
      if (!host.empty()) {
        const auto raw = host.value.str();
        if (!raw.empty() && raw.front() == '[') {
          // IPv6 host header: "[::1]:8888" or "[::1]"
          const auto end = raw.find(']');
          if (end != String::npos) {
            this->url.hostname = raw.substr(0, end + 1);
            if (end + 2 < raw.size() && raw[end + 1] == ':') {
              this->url.port = raw.substr(end + 2);
            }
          } else {
            this->url.hostname = raw;
          }
        } else {
          const auto parts = split(raw, ':');
          if (parts.size() == 2) {
            this->url.hostname = parts[0];
            this->url.port = parts[1];
          } else {
            this->url.hostname = raw;
          }
        }
      }
    }

    this->url = this->url.str();
  }

  Request::Request (
    const unsigned char* input,
    size_t size,
    const String& scheme,
    const String& method
  ) : scheme(scheme),
      method(method) {
    const auto string = size >= 0
      ? String(reinterpret_cast<const char*>(input), size)
      : String(reinterpret_cast<const char*>(input));

    const auto crlf = string.find("\r\n");
    const auto headerEnd = string.find("\r\n\r\n");

    this->url.scheme = scheme;
    if (crlf != String::npos) {
      auto stream = std::istringstream(string.substr(0, crlf));
      String pathname;
      String version;
      stream
        >> this->method
        >> pathname
        >> version;

      const auto versionParts = split(version, '/');
      const auto pathParts = split(pathname, '?');

      if (versionParts.size() == 2) {
        this->version = versionParts[1];
      }

      if (pathParts.size() == 2) {
        this->url.pathname = pathParts[0];
        this->url.search = "?" + pathParts[1];
        this->url.searchParams.set(pathParts[1]);
      } else {
        this->url.pathname = pathname;
      }

      if (headerEnd != String::npos && headerEnd > crlf + 2) {
        this->headers = string.substr(crlf + 2, headerEnd - (crlf + 2));
      } else if (crlf + 2 < string.size()) {
        this->headers = string.substr(crlf + 2);
      }
      this->body.set(
        input + (headerEnd != String::npos ? headerEnd + 4 : string.size()),
        0,
        headerEnd != String::npos && size >= headerEnd + 4
          ? size - headerEnd - 4
          : 0
      );

      const auto host = this->headers.get("host");
      if (!host.empty()) {
        const auto raw = host.value.str();
        if (!raw.empty() && raw.front() == '[') {
          const auto end = raw.find(']');
          if (end != String::npos) {
            this->url.hostname = raw.substr(0, end + 1);
            if (end + 2 < raw.size() && raw[end + 1] == ':') {
              this->url.port = raw.substr(end + 2);
            }
          } else {
            this->url.hostname = raw;
          }
        } else {
          const auto parts = split(raw, ':');
          if (parts.size() == 2) {
            this->url.hostname = parts[0];
            this->url.port = parts[1];
          } else {
            this->url.hostname = raw;
          }
        }
      }
    }

    this->url = this->url.str();
  }

  bool Request::valid () const {
    return (
      this->method.size() > 0 &&
      this->version.size() > 0 &&
      this->url.size() > 0
    );
  }

  String Request::str () const {
    return this->method + " " + this->url.pathname + this->url.search + " HTTP/" + this->version + "\r\n" + this->headers.str();
  }
}
