# WebView TLS Pins

The `webview_tls_pins` configuration key lets you pin server certificates for HTTPS requests made by the embedded WebViews. When no pins are configured for a host, the platform’s default certificate validation behaviour is preserved.

## Configuration

Pins are declared in `oro.toml` as a newline‑separated list of entries:

```toml
[webview]
tls_pins = """
example.com sha256/BASE64_DIGEST
api.example.test sha256/OTHER_BASE64_DIGEST
"""
```

Each non‑empty, non‑comment line has the form:

- `<host> <pin> [<pin>...]`
- `<host>` may be a hostname, `hostname:port`, `[ipv6]:port`, or a URL; any scheme/path/query/fragment is ignored. When a numeric `:port` suffix is present, the pins apply only to that port; otherwise pins apply to any port for the host unless a more specific `<host>:<port>` entry exists.
- `<pin>` is either `sha256/<base64>` or just `<base64>` (the digest is always interpreted as SHA‑256).
- `<base64>` may be standard Base64 or Base64URL (using `-` and `_`), with or without padding.
- Hosts are matched case‑insensitively.
- Lines may include comments starting with `#` or `;` (trailing content is ignored).

The digest is the SHA‑256 of the server certificate (not the entire chain), encoded as standard Base64.

## Computing a Pin

The pin value is the Base64-encoded SHA-256 digest of the leaf certificate’s DER bytes.

From a remote server (uses OpenSSL):

```sh
HOST=example.com
PORT=443

openssl s_client -connect "${HOST}:${PORT}" -servername "${HOST}" </dev/null 2>/dev/null \
  | openssl x509 -outform der \
  | openssl dgst -sha256 -binary \
  | openssl base64 -A
```

From a local PEM file:

```sh
openssl x509 -in server.pem -outform der \
  | openssl dgst -sha256 -binary \
  | openssl base64 -A
```

Then configure:

```
example.com sha256/<PASTE_OUTPUT_HERE>
```

## Runtime Updates

In addition to static configuration, you can update pins at runtime via IPC:

```js
import ipc from 'oro:ipc'

await ipc.request('application.setWebviewTlsPins', {
  value: `
example.com sha256/BASE64_DIGEST
api.example.test sha256/OTHER_BASE64_DIGEST
`,
  // mode: 'append' (default) to extend existing pins,
  // or 'replace' to overwrite them entirely.
  mode: 'append',
})
```

Invalid entries are rejected with `err.code = WEBVIEW_TLS_PINS_INVALID`.

Or from JavaScript via the TLS module:

```js
import * as tls from 'oro:tls'

await tls.setWebViewTlsPins(
  [
    'example.com sha256/BASE64_DIGEST',
    'api.example.test sha256/OTHER_BASE64_DIGEST',
  ],
  { mode: 'append' }
)

// Clear pins:
await tls.clearWebViewTlsPins()
```

Per-host helpers are also available:

```js
import * as tls from 'oro:tls'

await tls.setWebViewTlsPinsForHost('example.com', ['sha256/BASE64_DIGEST'])
await tls.addWebViewTlsPinsForHost('example.com', ['sha256/NEXT_BASE64_DIGEST'])
await tls.removeWebViewTlsPinsForHost('example.com')

const { configured, pins } = await tls.getWebViewTlsPinsForHost('example.com')
console.log({ configured, pins })
```

This call:

- Updates the process‑wide runtime config (`webview_tls_pins`), so Android’s WebView sees the new pins via `getUserConfigValue`.
- Updates the default window config used for future windows.
- Updates `bridge.userConfig["webview_tls_pins"]` for all active windows so Apple/Linux WebViews immediately pick up the new pins.

## Behaviour by Platform

- **macOS / iOS (WKWebView)**  
  - Pins are enforced during the TLS handshake via `WKNavigationDelegate`’s authentication challenge.  
  - When pins exist for a host, only certificates whose SHA‑256 digest matches one of the configured pins are accepted; other certificates are rejected even if they are trusted by the system.

- **Linux (WebKitGTK)**  
  - Pins are evaluated when WebKit reports TLS errors via `load-failed-with-tls-errors`.  
  - For endpoints that include an explicit port (`<host>:<port>`), the runtime also prewarms the WebKit allow‑list by connecting to each endpoint, verifying the certificate digest against the configured pins, and then calling `webkit_web_context_allow_tls_certificate_for_host`. This enables subresource requests (e.g., `fetch()`/XHR) that may not emit `load-failed-with-tls-errors`.  
  - If the failing certificate’s digest matches a configured pin for the host, the runtime whitelists that certificate for the host and retries the navigation.  
  - If no pin matches, the original TLS error is preserved.  
  - Note: `webkit_web_context_allow_tls_certificate_for_host` is sticky for the lifetime of the WebKitWebContext; clearing pins does not revoke previously allowed certificates without restarting the WebView.
  - Note: this currently only relaxes errors for pinned certificates; it does not reject otherwise‑valid certificates that are not pinned.

- **Android (android.webkit.WebView)**  
  - Pins are evaluated in `onReceivedSslError`, which is only invoked when WebView encounters TLS errors.  
  - If pins exist for the host and the certificate digest matches, the error is overridden and the navigation proceeds; otherwise the navigation is cancelled.  
  - As with Linux, this cannot currently reject non‑pinned but otherwise valid certificates.

- **Windows (WebView2)**  
  - Pins are evaluated when WebView2 reports server certificate failures via `ServerCertificateErrorDetected`.
  - If the failing certificate’s digest matches a configured pin for the host, the runtime overrides the error and allows navigation; otherwise the navigation is cancelled.
  - Note: WebView2’s “always allow” decision may persist for the lifetime of the profile/user data folder; clearing pins does not necessarily revoke previously allowed certificates without clearing that profile state.
  - As with Linux/Android, this is currently an error‑path hook and cannot reject otherwise‑valid certificates that are not pinned.

When enabling pins, start with a single host in a controlled environment, verify behaviour on your target platforms, and only then roll out more broadly.
