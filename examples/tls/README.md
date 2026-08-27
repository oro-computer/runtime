# TLS Echo Examples

These demos exercise the experimental `oro:tls` module with a minimal echo server and client. A mutual TLS (mTLS) variant is also included to highlight certificate exchange in both directions.

## Requirements

- Desktop build with the mbedTLS backend enabled. Set `ORO_ENABLE_MBEDTLS=1` when invoking `oroc`, or compile the runtime with `-DORO_RUNTIME_ENABLE_MBEDTLS=1` and have mbedTLS available via `pkg-config`.
- Locally trusted certificates for both the server and the client. Self-signed certs are sufficient for local testing.

## Generate local certificates

The PEM keys checked into this example are public, test-only fixtures. Never reuse them for a
deployed service, application, or local trust root. Generate a fresh private CA and leaf keys for
every real environment.

Use the helper script to create a certificate authority (CA) and issue matching server/client certificates:

```
bash examples/tls/gencerts.sh
```

To generate only a server certificate manually:

```
openssl req -x509 -newkey rsa:2048 -sha256 -days 365 -nodes \
  -keyout server-key.pem -out server-cert.pem \
  -subj "/CN=localhost"
```

## Basic echo server and client

Start the TLS echo server:

```
ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/server.mjs
```

Connect with the client in another shell:

```
ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/client.mjs
```

The client emits `secureConnect` once the handshake completes, writes `hello over tls`, and prints the echoed payload before closing. It sets `rejectUnauthorized: false` so the self-signed server certificate is accepted. For production scenarios always bundle a trusted CA chain and leave `rejectUnauthorized` enabled.

When using encrypted private keys, supply `keyPassphrase` (or `passphrase`) alongside `key` in both server and client options. Handshake metadata surfaces in the `secureConnectionReady` (server) and `secureConnect` (client) events, including negotiated ALPN details.

## Mutual TLS workflow

After generating certificates with `gencerts.sh`, launch the mTLS echo server:

```
ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/server-mtls.mjs
```

Then run the mTLS client:

```
ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/client-mtls.mjs
```

The server requires clients signed by the local CA and keeps `rejectUnauthorized` enabled. The client validates the server against the same CA bundle; keep this enabled so untrusted servers fail the connection early.

## Synthetic server testing (no native backend)

You can validate TLS server event wiring without a native TLS backend by dispatching synthetic events. For example, to assert that `secureConnectionReady` is delivered for your server instance:

```
import { createServer } from 'oro:tls'

const server = createServer()
const serverId = server.id

let ready = false
server.on('secureConnectionReady', ({ clientId }) => {
  console.log('synthetic secure ready for clientId', clientId)
  ready = true
})

// Synthesize a native event indicating a completed handshake for a client
const clientId = '1234'
globalThis.dispatchEvent(new CustomEvent('data', {
  detail: { source: 'tls.server.secureConnection', params: { data: { serverId, clientId } } }
}))

// JS will emit 'secureConnectionReady' even without a native TLS backend
```

The `connection` event depends on the native accept flow (`tls.server.accept`) and cannot be fully simulated. Use the snippet above to test listener behaviour that only depends on secure handshake completion (logging, metrics, orchestration, etc.).
