# Signed update quick start

This example shows how to generate a signing key, produce a signed update
manifest, and verify it using the same primitives that the Oro runtime uses
internally.

> [!WARNING]
> The checked-in `keys.json` and `manifest.json.sig` files are public,
> deterministic demonstration fixtures. The private key in `keys.json` is
> intentionally disclosed so the example can be reproduced; it cannot protect
> a real release. Never reuse this key or trust its signature outside this
> example. Generate and securely store a new key for every real application.

All paths below are relative to the repository root.

## 1. Generate an Ed25519 keypair

Generate a keypair using your preferred tooling (for example, a small Node
script that uses libsodium) and write a JSON file:

```jsonc
{
  "keyId": "pk-1",
  "publicKey": "<32-byte-ed25519-pk-hex>",
  "privateKey": "<64-byte-ed25519-sk-hex>",
}
```

The `publicKey` value is what you embed in your application and pass to the
`oro:application/update` module. The `privateKey` should be kept secret (for example, in
your release CI environment or a secure signing host).

## 2. Create an artifact and compute its hash

Build or package your application update into an artifact (for example,
`hello-1.0.0.tar.zst`). Compute its SHA‑256 hash:

```bash
node -e "const fs=require('node:fs'); const crypto=require('node:crypto');
  const data=fs.readFileSync('hello-1.0.0.tar.zst');
  console.log(crypto.createHash('sha256').update(data).digest('hex'))"
```

Copy the printed hex digest; it will be referenced in the manifest.

## 3. Create `manifest.json`

Create `examples/updates/manifest.json` with a minimal manifest describing
your update:

```jsonc
{
  "schemaVersion": 1,
  "appId": "com.example.hello",
  "generatedAt": "2025-01-01T00:00:00Z",
  "channels": ["stable"],
  "updates": [
    {
      "id": "stable-1.0.0",
      "version": "1.0.0",
      "channel": "stable",
      "minRuntimeVersion": "0.6.0",
      "critical": false,
      "targets": [
        {
          "platform": "darwin",
          "arch": "x64",
          "artifactUrl": "https://example.com/downloads/hello-1.0.0-darwin-x64.tar.zst",
          "length": 123456, // optional, but recommended
          "hashAlgorithm": "sha256",
          "hash": "<sha256-hex-of-your-artifact>",
        },
      ],
    },
  ],
}
```

Adjust `appId`, `platform`, `arch`, `artifactUrl`, and `length` for your
application and artifacts. You can add more `updates` entries and
platform-specific `targets` as needed.

## 4. Sign the manifest

Use your signing tool (for example, libsodium bindings) to sign the raw
manifest bytes with your private key and write `manifest.json.sig` next to
the manifest:

```jsonc
{
  "schemaVersion": 1,
  "algorithm": "ed25519",
  "keyId": "pk-1",
  "signature": "<64-byte-signature-hex>",
}
```

These two files (`manifest.json` and `manifest.json.sig`) are what you host
on your update server (for example, under `https://updates.example.com/app/`).

## 5. Verify the manifest and signature

Before publishing, you should verify the manifest + signature pair using
the public key. Any tool that can perform Ed25519 detached signature
verification over the raw `manifest.json` bytes (for example, libsodium)
will work.

## 6. Use the manifest from your application

In your application code, you can use the `oro:application/update` module to
check and download updates:

```js
import { checkForUpdates } from 'oro:application/update'

// Public key copied from examples/updates/keys.json
const publicKeyHex = '<32-byte-ed25519-pk-hex>'

const result = await checkForUpdates({
  manifestUrl: 'https://updates.example.com/app/manifest.json',
  publicKey: publicKeyHex,
  expectedAppId: 'com.example.hello',
  channel: 'stable',
  currentVersion: '1.0.0',
  download: true,
})

if (!result.updateAvailable) {
  console.log('No updates available')
} else {
  const { update, target, artifact } = result
  console.log(
    'Update',
    update.version,
    'selected for',
    target.platform,
    target.arch
  )
  // TODO: apply the "artifact" bytes (e.g., write to disk and run installer).
}
```

This mirrors the protocol described in `docs/APPLICATION_UPDATE_PROTOCOL.md`
and uses the same verification logic as the runtime. You are free to host
the manifest, signature, and artifacts on any HTTP server or CDN, or to
deliver them over custom transports as long as the manifest bytes and
signatures remain unchanged.
