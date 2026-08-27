# TLS Quickstart (Experimental)

Status: Experimental. Runtime TLS is implemented via a single built-in provider per build.

- Linux desktop defaults to the vendored **mbedTLS** backend (built by `bin/install.sh`).
- Windows desktop supports the built-in **Schannel** backend.
- The **OpenSSL** backend is an optional build-time alternative for desktop builds when you provide the toolchain/linker flags it needs.
- macOS, iOS, and Android do not currently have a built-in TLS provider in this repository.
- Those targets still build successfully, but `oro:tls` and related TLS entry points return `NOT_IMPLEMENTED` when no provider is compiled in.
- **GnuTLS**, **SecureTransport**, and platform **Android** TLS providers are not implemented in this repository yet.

When building the runtime itself, `NO_ANDROID` and `NO_IOS` remain distinct
target exclusions even though TLS provider availability differs by platform.
Any non-empty `NO_ANDROID` disables only Android bootstrap/artifacts; any
non-empty `NO_IOS` disables only iOS/iOS Simulator work on macOS. Neither
disables a desktop TLS build, and `0`/`false` still disable the named target.
See [Source-build environment](BUILD_ENVIRONMENT.md).

## Enabling TLS

1. Build-time provider selection:
   - Linux desktop defaults to **mbedTLS**.
   - Windows desktop defaults to **Schannel** when TLS is enabled.
   - To build with **OpenSSL** instead, set `ORO_TLS_BUILD_PROVIDER=openssl` (or `ORO_TLS_ENABLE_OPENSSL=1`) when building the runtime.
   - `ORO_TLS_BUILD_PROVIDER=gnutls` is currently rejected because there is no backend implementation under `src/runtime/tls`.
   - `ORO_TLS_BUILD_PROVIDER=securetransport` and `ORO_TLS_BUILD_PROVIDER=android` do not enable a provider in this repository; builds continue without TLS support on those targets, and runtime TLS entry points return `NOT_IMPLEMENTED`.
   - On macOS, iOS, and Android, `ORO_ENABLE_TLS=1` is treated as a request to keep the API surface available while building without a built-in provider.

2. Runtime selection:
   - `ORO_TLS_PROVIDER` can select a provider at runtime when multiple providers are available (e.g., via dynamic provider modules).
   - With the current single-provider build model, `ORO_TLS_PROVIDER` must match the compiled provider (or `tls.connect` will return `NOT_IMPLEMENTED`).

```
export ORO_TLS_PROVIDER=mbedtls        # or: openssl, schannel
```

You can confirm the active provider at runtime from JavaScript:

```js
import * as tls from 'oro:tls'

const { provider } = await tls.getTlsProvider()
console.log('TLS provider:', provider)
```

3. Manual OpenSSL flags (when `pkg-config` is unavailable):

```
export ORO_TLS_CFLAGS="-I/path/to/include"
export ORO_TLS_LDFLAGS="-L/path/to/lib -lssl -lcrypto"
```

## Client API (oro:tls)

```
import { connect } from 'oro:tls'

const socket = connect({
  host: 'example.com',
  port: 443,
  servername: 'example.com',          // SNI/hostname (defaults to host)
  rejectUnauthorized: true,           // default true
  alpnProtocols: ['h2', 'http/1.1'],  // optional
  minVersion: 'TLSv1.2',              // optional
  maxVersion: 'TLSv1.3'               // optional
})

socket.on('secureConnect', () => {
  socket.write('GET / HTTP/1.1\r\nHost: example.com\r\n\r\n')
})

socket.on('data', (buf) => {
  console.log(Buffer.from(buf).toString('utf8'))
})

socket.on('error', (err) => console.error('TLS error:', err))
```

## Trust and Verification

- `rejectUnauthorized` defaults to `true`. When enabled:
  - If `ca` PEM is provided, it is used as the trust store.
  - Otherwise, the runtime attempts to load the system CA bundle on Linux; if unavailable, verification fails.
  - Hostname (SNI) is set to `servername || host` and must match the certificate.
- On verification failure, the error will include a `code` with a canonical value such as:
  - `HOSTNAME_MISMATCH`
  - `CERT_UNTRUSTED`
  - `CERT_UNTRUSTED_ROOT`
  - `CERT_EXPIRED`
  - `CERT_NOT_TIME_VALID`
  - `CERT_REVOKED`
  - `CRL_EXPIRED`
  - `CRL_UNTRUSTED`
  - `CERT_NOT_PERMITTED`
  - `CERT_BAD_SIGNATURE_ALG`
  - `CERT_BAD_SIGNATURE`
  - `CERT_BAD_PUBLIC_KEY`
  - `CERT_BAD_KEY`
  - or `CERTIFICATE_VERIFY_FAILED`

## TLS Pinning (Runtime TLS)

The runtime TLS service supports strict leaf-certificate pinning via the `tls_pins` configuration key and the `oro:tls` JavaScript API.

Pins are evaluated **after** the TLS handshake completes:

- If pins are configured for a host, the connection succeeds only when the peer leaf certificate’s SHA‑256 digest matches one of the configured pins.
- Pinning is enforced even when `rejectUnauthorized: false`.
- When `rejectUnauthorized: true` (default), both certificate verification **and** pin matching must succeed.

### Configuration (`oro.toml`)

Pins are declared as a newline‑separated list of entries:

```toml
[tls]
pins = """
example.com sha256/BASE64_DIGEST
api.example.test sha256/OTHER_BASE64_DIGEST
"""
```

Alternatively, in TOML you can use a string array:

```toml
[tls]
pins = [
  "example.com sha256/BASE64_DIGEST",
  "api.example.test sha256/OTHER_BASE64_DIGEST",
]
```

Each non-empty, non-comment line has the form:

- `<host> <pin> [<pin>...]`
- `<host>` may be a hostname, `hostname:port`, `[ipv6]:port`, or a URL; any scheme/path/query/fragment is ignored. When a numeric `:port` suffix is present, the pins apply only to that port; otherwise pins apply to any port for the host unless a more specific `<host>:<port>` entry exists.
- `<pin>` is either `sha256/<base64>` or just `<base64>` (always interpreted as SHA‑256).
- `<base64>` may be standard Base64 or Base64URL (using `-` and `_`), with or without padding.
- Hosts are matched case-insensitively.
- Lines may include comments starting with `#` or `;` (trailing content is ignored).

### Computing a pin

From a remote server (uses OpenSSL):

```sh
HOST=example.com
PORT=443

openssl s_client -connect "${HOST}:${PORT}" -servername "${HOST}" </dev/null 2>/dev/null \
  | openssl x509 -outform der \
  | openssl dgst -sha256 -binary \
  | openssl base64 -A
```

Or from JavaScript (DER or PEM):

```js
import * as tls from 'oro:tls'

// await tls.createTlsPinFromCertificateDer(derBytes)
// await tls.createTlsPinFromCertificatePem(pemString)
```

### Runtime API

```js
import * as tls from 'oro:tls'

await tls.setTlsPins(
  [
    'example.com sha256/BASE64_DIGEST',
    'api.example.test sha256/OTHER_BASE64_DIGEST',
  ],
  { mode: 'append' } // or 'replace'
)

const { value } = await tls.getTlsPins()
console.log(value)
```

`setTlsPins()` validates entries and throws when the host or pin tokens are invalid.
If you use IPC directly (`tls.setPins`), invalid entries are rejected with `err.code = TLS_PINS_INVALID`.

#### Per-host helpers

If you want to manage pins for a single host without rewriting the entire config string:

```js
import * as tls from 'oro:tls'

await tls.setTlsPinsForHost('example.com', ['sha256/BASE64_DIGEST'])
await tls.addTlsPinsForHost('example.com', ['sha256/NEXT_BASE64_DIGEST'])
await tls.removeTlsPinsForHost('example.com', ['sha256/OLD_BASE64_DIGEST'])

// Remove the host entry entirely:
await tls.removeTlsPinsForHost('example.com')

const { configured, pins } = await tls.getTlsPinsForHost('example.com')
console.log({ configured, pins })
```

### Per-connection pins

`connect()` also accepts per-connection pin overrides:

```js
import { connect } from 'oro:tls'

connect({
  host: 'example.com',
  port: 443,
  // pins can be a list of pin tokens (auto-associated with servername||host),
  // and the connection port (i.e., `${servername||host}:${port}`),
  // or full '<host> <pin>' lines.
  pins: ['sha256/BASE64_DIGEST'],
  // 'append' (default) merges with global tls_pins; 'replace' uses only pins above.
  // If `pins` is omitted/blank, global tls_pins still apply.
  pinsMode: 'replace',
  // rejectUnauthorized: false, // optional: rely on pins without CA validation
})
```

Invalid `pins` input throws a `TypeError` before any network I/O occurs.
With `pinsMode: 'replace'`, `pins` must include at least one pin that applies to `servername || host`.

On pin failure, the error includes:

- `code`: `PIN_MISCONFIGURED` | `PIN_UNAVAILABLE` | `PIN_MISMATCH`
- `provider`: `mbedtls` | `openssl` | `schannel` | `auto`
- `peerPin`: `sha256/<base64>` (when available)
- `expectedPins`: string[]

On success, the `secureConnect` event payload also includes `peerPin` (when available) and `provider`, which is useful for logging and pin rotation.

## Mutual TLS

Provide `cert` and `key` in PEM format to enable client authentication.
If the private key is encrypted, pass `keyPassphrase` (or `passphrase`) alongside `key`;
this is supported for both `connect()` and `createServer()`.

## ALPN, Versions, and Ciphers

- `alpnProtocols`: e.g., `['h2', 'http/1.1']` (order matters)
- `minVersion`, `maxVersion`: `'TLSv1.2' | 'TLSv1.3'` (if supported by the provider)
- `ciphers`: numeric cipher IDs as hex strings (e.g., `['0x1301']`)

## Notes

- Handshake and record I/O are non-blocking and integrated with libuv; both client and server APIs are available via `oro:tls`.
- Builds that do not compile a TLS provider will surface `NOT_IMPLEMENTED` errors from the IPC routes.
- For WebView TLS certificate pinning (WebKit/WebView/WebView2), see `docs/WEBVIEW_TLS_PINS.md`.
