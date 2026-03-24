# TLS Testing Guide

This guide shows how to run the experimental TLS tests (examples use Linux tooling) with the providers currently implemented in this repository and how to decrypt captures in Wireshark using the key-log file. Linux examples use OpenSSL or mbedTLS; Windows uses Schannel. macOS, iOS, and Android do not currently have a built-in TLS provider in this repository, so those targets compile without TLS support and `oro:tls` returns `NOT_IMPLEMENTED`.

Prerequisites

- Build toolchain: gcc/g++ (C++20), make, pkg-config
- Libraries (Ubuntu/Debian names):
  - Core GUI/runtime: libwebkit2gtk-4.1-dev, libgtk-3-dev, libdbus-1-dev, libsoup-3.0-dev
  - TLS provider (choose one):
    - OpenSSL: libssl-dev
    - mbedTLS: libmbedtls-dev
  - Optional: xvfb (for CI headless runs), wireshark (for capture/decrypt)

Install example (Ubuntu):

```
sudo apt-get update
sudo apt-get install -y \
  build-essential pkg-config \
  libwebkit2gtk-4.1-dev libgtk-3-dev libdbus-1-dev libsoup-3.0-dev \
  libssl-dev # or: libmbedtls-dev
```

Environment

- Enable TLS and pick a provider:

```
export ORO_ENABLE_TLS=1
export ORO_TLS_PROVIDER=openssl   # or: mbedtls, schannel
```

- When using OpenSSL on desktop targets and `pkg-config` is unavailable, provide build flags explicitly:

```
export ORO_TLS_CFLAGS="-I/path/to/include"
export ORO_TLS_LDFLAGS="-L/path/to/lib -lssl -lcrypto"   # adjust for your TLS provider
```

- Optional: write TLS key log lines for Wireshark decryption:

```
export ORO_TLS_KEYLOG=/tmp/sslkeys.txt
## or, per invocation:
##   oroc run --tls-keylog=/tmp/sslkeys.txt --test=…
##   oroc build --tls-keylog=/tmp/sslkeys.txt …
```

- When using encrypted private keys in tests, pass the matching `keyPassphrase` (or `passphrase`) field alongside the PEM.

Run the tests

1. Echo + ALPN + mTLS

```
oroc run --test=test/src/tls/echo.js
```

2. Negative scenarios

```
oroc run --test=test/src/tls/negative.js
```

Expected results

- echo.js
  - Self-signed echo succeeds when `rejectUnauthorized:false` and negotiates `alpnProtocol: 'http/1.1'`.
  - CA-trusted echo succeeds with `rejectUnauthorized:true` and negotiates `alpnProtocol: 'http/1.1'`.
  - mTLS echo succeeds when server `requestCert:true` and client supplies `cert`/`key`.
- negative.js
  - Missing CA path fails; error may include `code` such as `CERT_UNTRUSTED` or `CERTIFICATE_VERIFY_FAILED`.
  - Hostname mismatch fails (`HOSTNAME_MISMATCH` or `CERTIFICATE_VERIFY_FAILED`).
  - Server requires client cert, client omits it → handshake fails.

Wireshark decryption (OpenSSL provider)

1. Ensure `ORO_TLS_KEYLOG` is set to a writable path (see Environment above).
2. In Wireshark: Preferences → Protocols → TLS → set “(Pre)-Master-Secret log filename” to the keylog path.
3. Capture filters (ports used by tests):

```
tcp port 30443 or tcp port 30444 or tcp port 30445 or tcp port 30446 or tcp port 30447 or tcp port 30448
```

4. Display filter examples: `tls` or `tcp.stream eq 0`.

Troubleshooting

- If you see linker errors for WebKitGTK or GTK, ensure `libwebkit2gtk-4.1-dev` and `libgtk-3-dev` are installed.
- In CI/headless environments, you may need `xvfb-run` to provide a display server.
- To skip building the desktop extension in constrained Linux CI, set `ORO_TEST_SKIP_DESKTOP_EXTENSION=1`.
