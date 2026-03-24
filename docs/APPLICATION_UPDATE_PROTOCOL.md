# Oro Application Update Protocol (OUP)

This document describes a portable, transport-agnostic system for delivering
signed application updates over HTTP and custom UDP/TCP transports.

The design is inspired by existing update frameworks (for example, systems like
The Update Framework and package manager repositories) but is tailored for the
Oro runtime, libsodium, and environments where application developers can host
their own infrastructure.

## Goals

- Protect users against tampered or replayed updates, even over untrusted
  networks.
- Make the format transport-agnostic so the same metadata works over HTTP,
  TCP, or UDP.
- Keep the server side simple enough for static hosting or small services.
- Allow applications to remain in control: the runtime can fetch & verify;
  the app decides when and how to apply an update.
- Provide a clear, evolvable wire format that can be implemented in other
  languages or runtimes.

Non-goals:

- Full OS / installer updates.
- Mobile app store updates (those environments are expected to use store
  mechanisms).
- Complex multi-role key hierarchies; the initial design keeps keys simple
  but is compatible with future extensions.

## High-level architecture

The update system separates **metadata**, **artifacts**, and **transport**:

- **Artifacts** are the update payloads (archives, binaries, bundles, etc).
- **Manifests** describe artifacts (version, platform, hashes, URLs) and are
  signed with an Ed25519 key.
- **Transports** (HTTP, TCP, UDP) move manifests and artifacts between
  publisher and client. Transport is assumed untrusted; integrity and
  authenticity are enforced at the metadata level with libsodium.

Each application ships with a built‑in **update public key**. Only holders of
the corresponding **update private key** can publish updates for that
application.

## Cryptography

The system uses libsodium:

- **Signatures**: `crypto_sign_ed25519_*` for manifest signatures.
- **Hashes**: `crypto_generichash` (or SHA‑256 via WebCrypto) for artifact
  digests.

Recommendations:

- Use one or more long‑lived Ed25519 **root/update keys** per application.
- Store private keys offline or in a CI secret store.
- Distribute the public key with the app (for example, hard‑coded or loaded
  from a trusted bundle).

Public keys and signatures are represented as:

- Public key (`pk`): 32‑byte Ed25519 public key, encoded as lowercase hex or
  base64url.
- Signature (`sig`): 64‑byte Ed25519 signature, encoded as lowercase hex or
  base64url.

The client treats encodings as interchangeable and normalises them back to
raw bytes for verification.

## Manifest format

The manifest is an opaque byte string from the perspective of signature
verification: publishers sign the raw bytes of the manifest file, and
clients verify the exact same bytes. A canonical JSON representation is
recommended for portability.

### Top-level shape

```json
{
  "schemaVersion": 1,
  "appId": "com.example.myapp",
  "generatedAt": "2025-01-01T12:34:56Z",
  "channels": ["stable", "beta"],
  "updates": [
    {
      "id": "stable-1.2.3",
      "version": "1.2.3",
      "channel": "stable",
      "minRuntimeVersion": "0.6.0",
      "critical": false,
      "notesUrl": "https://example.com/myapp/1.2.3-notes.html",
      "targets": [
        {
          "platform": "darwin",
          "arch": "x64",
          "osVersionRange": ">=11",
          "artifactUrl": "https://updates.example.com/myapp/darwin-x64-1.2.3.tar.zst",
          "length": 12345678,
          "hashAlgorithm": "sha256",
          "hash": "b0f3…", // hex
          "signatureAlgorithm": "ed25519",
          "artifactSignature": "cafe…" // optional per-artifact signature
        }
      ]
    }
  ]
}
```

Key points:

- `schemaVersion` gates breaking changes in the manifest layout.
- `appId` uniquely identifies the application (reverse DNS is recommended).
- `updates` may contain many versions and channels; the client filters to the
  relevant channel, platform, and version.
- The manifest is **not** trusted until its signature has been verified.

An authoritative JSON Schema for this manifest lives at
`schemas/update-manifest.schema.json` in the repository. Publishers and
downstream tooling can use it to validate `manifest.json` files during CI or
manual editing.

### Manifest signature sidecar

The manifest is distributed together with a signature file:

- Manifest bytes: `manifest.json`
- Signature bytes: `manifest.sig`

The signature file contains an encoded signature and metadata:

```json
{
  "schemaVersion": 1,
  "algorithm": "ed25519",
  "keyId": "pk-1",
  "signature": "cafe…" // base64url or hex
}
```

The JSON Schema for the signature file is available at
`schemas/update-manifest-signature.schema.json` in this repository.

Clients:

1. Download `manifest.json` as raw bytes.
2. Download and parse `manifest.sig`.
3. Decode the `signature` field to raw bytes.
4. Verify the signature over the raw `manifest.json` bytes using the
   configured public key.
5. Only then parse `manifest.json` as JSON and process its contents.

The same manifest and signature format is used for all transports.

## HTTP transport binding

HTTP(S) is the primary transport for manifests and artifacts. A minimal
setup can be hosted on static file storage or any simple HTTP server.

### Layout

Publishers choose a base URL and directory structure. One suggested layout is:

```text
https://updates.example.com/myapp/
  manifest.json
  manifest.sig
  artifacts/
    darwin-x64-1.2.3.tar.zst
    win32-x64-1.2.3.zip
```

The client is configured with:

- `manifestUrl` (for example, `https://updates.example.com/myapp/manifest.json`)
- `publicKey` (Ed25519 public key)
- Optionally a `channel`, `currentVersion`, and target platform/arch.

### HTTP request/response flow

1. Client sends `GET manifestUrl`.
2. Client sends `GET manifestUrl + ".sig"` (unless overridden).
3. Client verifies the manifest signature.
4. Client selects the best update for its channel, version, and platform.
5. Client downloads `artifactUrl` with `GET`, optionally using `Range`
   requests.
6. Client verifies the artifact digest and optional per‑artifact signature.

Transport security:

- HTTPS is strongly recommended.
- Even when HTTPS is unavailable or misconfigured, the client must reject
  updates whose manifest signatures or artifact hashes do not validate.

## UDP/TCP transport binding

Some deployments may prefer a push‑oriented or low‑latency transport. The
update protocol defines a simple binary framing suitable for TCP or UDP.

All messages start with:

```text
struct Header {
  uint8  version;     // protocol version, e.g., 1
  uint8  msgType;     // 0x01 = CHECK, 0x02 = RESPONSE, 0x03 = MANIFEST_CHUNK, 0x04 = ERROR
  uint16 reserved;    // must be zero for now
  uint32 length;      // length of the remaining payload in bytes (big-endian)
}
```

Payloads are encoded as JSON or CBOR; implementations may choose either as
long as both sides agree. For portability, JSON with UTF‑8 is recommended
initially.

### CHECK message

Client → server:

```json
{
  "schemaVersion": 1,
  "appId": "com.example.myapp",
  "currentVersion": "1.1.0",
  "channel": "stable",
  "platform": "darwin",
  "arch": "x64",
  "runtimeVersion": "0.6.0"
}
```

### RESPONSE message

Server → client:

```json
{
  "schemaVersion": 1,
  "hasUpdate": true,
  "selectedUpdateId": "stable-1.2.3",
  "manifestInline": false,
  "manifestUrl": "https://updates.example.com/myapp/manifest.json"
}
```

If `manifestInline` is `true`, the server may send the manifest bytes over
subsequent `MANIFEST_CHUNK` messages:

```text
struct ManifestChunkPayload {
  uint32 offset;      // byte offset of this chunk
  uint32 totalLength; // total manifest length in bytes
  uint8  data[];      // chunk bytes
}
```

The client reassembles the manifest bytes, then verifies the signature as in
the HTTP case. Artifacts are still typically downloaded over HTTP, but a
TCP or UDP stream could also carry artifact chunks, subject to deployment
constraints.

## Client-side API (overview)

The Oro runtime exposes a high‑level `oro:application/update` module
that:

- Fetches manifest and signature over HTTP.
- Verifies manifest signatures using libsodium.
- Selects an appropriate update for the app’s platform/channel/version.
- Downloads and verifies artifact hashes.
- Returns verified artifacts to the application for installation.

The API is transport‑agnostic: advanced users can swap the HTTP transport
with a custom TCP/UDP implementation that conforms to the same manifest and
signature rules.

### Mapping to the `oro:application/update` module

The `api/application/update.js` module provides a small set of primitives that map
directly onto this protocol:

- `fetchManifest(options)`  
  - Inputs: `manifestUrl`, optional `signatureUrl`, `publicKey`, optional
    `fetch`, `signal`, and extra HTTP headers.  
  - Behavior: downloads `manifest.json` and `manifest.sig`, verifies the
    Ed25519 signature using libsodium, then parses the manifest JSON.  
  - Returns: `{ manifest, raw, signature }`, where `raw` is the original
    manifest bytes and `signature` includes the decoded signature bytes and
    metadata.

- `selectUpdate(manifest, options)`  
  - Inputs: a verified `UpdateManifest` and selection hints (`channel`,
    `currentVersion`, `platform`, `arch`, `runtimeVersion`).  
  - Behavior: filters updates by channel and targets by platform/arch,
    enforces `minRuntimeVersion` and `currentVersion`, then chooses the
    highest compatible version (preferring `critical` when versions tie).  
  - Returns: `{ manifest, update, target }` or `null` if no suitable update
    exists.

- `downloadUpdate(target, options)`  
  - Inputs: a `UpdateTarget` from the manifest and optional `fetch`/`signal`.  
  - Behavior: downloads the artifact at `artifactUrl`, enforces `length` (if
    provided), computes a digest using WebCrypto (`SHA-256` by default), and
    compares it to the manifest’s `hash` field.  
  - Returns: a `Uint8Array` with the verified artifact bytes.

- `verifyArtifact(payload, target)`  
  - Inputs: artifact bytes and the corresponding `UpdateTarget` from the
    manifest.  
  - Behavior: enforces `length` (if present) and verifies the declared hash.  
  - Use this when artifacts are obtained via a non-HTTP transport (for
    example, custom TCP/UDP delivery or a local cache).

- `checkForUpdates(options)`  
  - Convenience wrapper that calls `fetchManifest`, `selectUpdate`, and (if
    requested via `download: true`) `downloadUpdate`.  
  - Returns a discriminated result:
    - `{ updateAvailable: false, manifest, signature }`, or  
    - `{ updateAvailable: true, manifest, signature, update, target, artifact? }`.

- `openArtifactArchive(artifact)`  
  - Inputs: a verified artifact payload (for example, the `artifact` field
    from `checkForUpdates` when `download: true`).  
  - Behavior: wraps the bytes in a native tar reader and returns a
    `TarArchive` backed by the runtime’s tar service. This allows callers to
    inspect and extract entries using the same semantics as `oro:tar`
    (see `TAR_API.md` for details).  
  - Intended for tar-based update bundles; callers are free to ignore it for
    non-tar artifacts.

Client code is expected to embed or otherwise obtain the Ed25519 public key
and pass it in as `publicKey`. The runtime does not manage update keys.

When calling `fetchManifest` or `checkForUpdates`, callers MAY also provide
an `expectedAppId` option; if present, the helpers will reject manifests
whose `appId` does not match the expected value. This is recommended for
applications that support multiple products or environments.

### Example integration

Simple HTTP-based check-and-download flow:

```js
import { checkForUpdates } from 'oro:application/update'

const result = await checkForUpdates({
  manifestUrl: 'https://updates.example.com/myapp/manifest.json',
  publicKey: '<ed25519-public-key-hex-or-base64>',
  channel: 'stable',
  currentVersion: '1.2.3',
  download: true,
})

if (!result.updateAvailable) {
  console.log('No updates available')
} else {
  const { update, target, artifact } = result
  console.log('Selected update', update.version, 'for', target.platform, target.arch)
  // TODO: apply the update bytes in a way that makes sense for your app.
}
```

### CLI tooling

The Oro CLI provides first-party helpers for managing update keys, manifests,
and bundles:

- `oroc update keygen`  
  - Generates an Ed25519 keypair suitable for signing manifests.  
  - Outputs JSON with `keyId`, `publicKey`, and `privateKey` (hex).

- `oroc update init`  
  - Scaffolds a minimal `manifest.json` file in the current directory.  
  - Sets `schemaVersion = 1`, `appId` from your oro.toml's `[meta] bundle_identifier` (falling back to a placeholder),
    `generatedAt` (UTC), `channels` from `update_channel` (or `["stable"]` when not set), and an `updates` array
    containing a single entry for the current version/channel with an empty `targets` array.

- `oroc update sign`  
  - Signs a manifest file and writes a detached `manifest.sig` JSON sidecar.  
  - Inputs: optional `--manifest=<path>` (defaults to `manifest.json` or a
    custom name), and either `--keys=<file>` (JSON) or
    `--private-key=<hex>`.  
  - Supports `--key-id` and `--out` to control metadata and output path.
  - By default, the signature filename is derived from the manifest name by stripping the extension and appending `.sig`
    (for example, `manifest.json` → `manifest.sig`).
  - Advanced: set `ORO_UPDATE_MANIFEST_FILENAME` or pass
    `--manifest-name=<name>` to change the default manifest filename (and
    derived signature path).

- `oroc update verify`  
  - Verifies a manifest + signature pair against a public key.  
  - Inputs: optional `--manifest=<path>` (defaults to `manifest.json` or a
    custom name), optional `--signature=<path>` (defaults to the derived signature path, e.g. `manifest.json` → `manifest.sig`),
    and either
    `--keys=<file>` (JSON) or `--public-key=<hex>`.
  - Advanced: set `ORO_UPDATE_MANIFEST_FILENAME` or pass
    `--manifest-name=<name>` to change the default manifest filename; the
    default signature path is derived by stripping the manifest's extension and appending `.sig`
    (for example, `manifest.json` → `manifest.sig`).

- `oroc update validate`  
  - Validates a manifest JSON file against the expected schema shape.  
  - Inputs: optional `--manifest=<path>` (defaults to `manifest.json` or a
    custom name), and optional `--manifest-name=<name>` (used when `--manifest` is not provided).  
  - Behavior: parses the manifest and enforces structural rules aligned with
    `schemas/update-manifest.schema.json` (required fields, types, relationships).  
  - Advanced: with `--strict`, additional consistency rules are applied (for example,
    ensuring each update’s `channel` is present in the top‑level `channels` array and that `artifactUrl` values do not
    contain whitespace). Intended for fast local checks and CI.

- `oroc update bundle`  
  - Builds a tar archive containing the contents of a directory, suitable as
    an update artifact.  
  - Inputs: optional `--input=<dir>` (defaults to the project directory, i.e.,
    the app source) and optional `--output=<bundle.tar>` (defaults to
    `<build_name>-<version>.tar` derived from `oro.toml`).  
  - Additional options:
    - `--manifest` / `--manifest-name` (or `ORO_UPDATE_MANIFEST_FILENAME`): when provided, the CLI parses the manifest,
      validates it against the schema shape, and updates it with a new target describing the bundle (including `length`,
      `hashAlgorithm`, and `hash`).  
    - `--channel`, `--update-id`, `--platform`, `--arch`, `--artifact-url`: control which update entry and target
      are created or amended. Reasonable defaults are derived from `oro.toml` (`update_channel`, `meta_version`) and the
      bundle filename when these flags are omitted (for source-only bundles, `platform` defaults to `"source"` and
      `arch` defaults to `"any"`).  
    - `--hash-algorithm=<sha256|sha1>`: hash algorithm for the tar payload. Defaults to `sha256` when libsodium is
      available at build time (via `crypto_generichash`) and to `sha1` otherwise. The resulting digest is written into
      the manifest’s `hash` field for the new target.
  - Preserves directory layout and file metadata (size, basic mode bits,
    and mtime) using the runtime’s native tar implementation.

- `oroc update extract`  
  - Extracts a tar archive (for example, one produced by `update-bundle`)
    into a destination directory.  
  - Inputs: `--bundle=<bundle.tar>`, `--dest=<dir>`.  
  - Rejects absolute paths and `..` segments inside the archive to avoid
    directory traversal; ignores special entries such as symlinks.

- `oroc update server`  
  - Runs an update server implementing the HTTP and binary TCP/UDP bindings of this protocol.  
  - Default mode is HTTP and is intended to be run behind a reverse proxy or
    load balancer in production, but TCP and UDP modes are also suitable for production
    deployments when a binary CHECK/RESPONSE transport is desired.  
  - Inputs: `--root=<dir>` (directory containing one or more manifest trees),
    optional `--host=<host>` and `--port=<port>` (default `0.0.0.0:8080`), and optional
    `--manifest-name=<name>` (defaults to `manifest.json` or `ORO_UPDATE_MANIFEST_FILENAME`).  
  - HTTP exposes:
    - `GET /health` — readiness metadata for the server.  
    - `POST /check` — accepts a `CHECK` JSON payload with `appId` and responds with a
      `RESPONSE` JSON whose `manifestUrl` points at `/<appId>/<manifest-name>` when such a manifest
      exists under `--root`; otherwise `hasUpdate: false`.  
    - `GET /<path>` — serves static files rooted under `--root`, including
      `/<appId>/<manifest-name>` and the corresponding signature file (for example
      `/<appId>/manifest.sig` when `manifest-name` is `manifest.json`).  
  - TCP/UDP expose the same CHECK/RESPONSE semantics over the OUP binary framing described above:
    - The payload inside the binary frame is the same JSON `CHECK`/`RESPONSE` body as HTTP,
      including `schemaVersion`, `appId`, `hasUpdate`, and `manifestUrl`.

- `oroc update info`  
  - Acts as a small client for the update protocol and for static manifest hosting.  
  - HTTP static mode: with `--manifest-url=<url>`, fetches a JSON manifest from an HTTP(S) origin, pretty‑prints it,
    and reports whether a companion signature file is reachable (by default derived as `manifest.sig`). When `--keys` or
    `--public-key` is provided and libsodium is available, it also verifies the manifest signature before printing.  
  - HTTP server mode: with `--host`/`--port` and no `--manifest-url`, sends a `CHECK` JSON body to an HTTP update server’s
    `/check` endpoint and pretty‑prints the `RESPONSE` JSON. With `--follow-manifest`, if the RESPONSE includes a
    `manifestUrl` pointing at a HTTP(S) resource, the CLI follows that URL, validates the referenced manifest, and when
    `--keys`/`--public-key` are provided it also verifies the manifest signature before printing it.  
  - TCP/UDP modes: with `--transport=tcp`/`--tcp` or `--transport=udp`/`--udp`, sends a binary‑framed `CHECK` message and
    pretty‑prints the JSON `RESPONSE` payload returned by the server. A `--timeout-ms` flag can be used to bound how long the
    client waits for a TCP or UDP response. When `--follow-manifest` is provided and the RESPONSE contains a `manifestUrl`
    pointing at a HTTP(S) resource, the CLI follows that URL and applies the same manifest validation / verification flow
    as in the HTTP server mode.  
  - Common hints such as `--app-id`, `--channel`, `--current-version`, `--platform`, `--arch`, and `--runtime-version`
    are included in the `CHECK` payload when provided; their defaults are taken from `oro.toml` when available.  
  - The flags `--http`, `--tcp`, and `--udp` are shorthands for `--transport=http`, `--transport=tcp`, and
    `--transport=udp` respectively.  
  - When `--follow-manifest` is used in server modes and `--app-id` is set, any fetched manifest must have a matching
    `appId` value or the command fails; this prevents misconfiguration where a server points to a manifest for a different app.

#### Example end-to-end flows

Basic local flow using the default `manifest.json`:

```bash
# 1) Scaffold a manifest for the current project.
oroc update init

# 2) Generate a signing keypair (writes key.json).
oroc update keygen > key.json

# 3) Build a source-only bundle and record it in the manifest.
oroc update bundle --manifest manifest.json

# 4) Sign the manifest using the generated keypair.
oroc update sign --keys key.json --manifest manifest.json

# 5) Verify the manifest + signature using the same keypair.
oroc update verify --keys key.json --manifest manifest.json
```

Serving many apps/manifests and querying them:

```bash
# 1) Prepare a directory of per-app trees.
mkdir -p ./updates/com.example.app
cp manifest.json ./updates/com.example.app/
cp manifest.sig ./updates/com.example.app/

# 2) Run an HTTP update server on 0.0.0.0:8080.
oroc update server --root ./updates

# 3) From another terminal, query for a given app and follow the manifestUrl.
oroc update info --http --host 127.0.0.1 --port 8080 \
  --app-id com.example.app \
  --follow-manifest
```

Static object store hosting (no custom server, just HTTP):

```bash
# 1) Upload manifest.json and manifest.sig to object storage/CDN.
#    Example URLs:
#      https://cdn.example.com/app/manifest.json
#      https://cdn.example.com/app/manifest.sig

# 2) Inspect the manifest only (no verification).
oroc update info \
  --manifest-url https://cdn.example.com/app/manifest.json

# 3) Inspect and verify the manifest using a public key file.
oroc update info \
  --manifest-url https://cdn.example.com/app/manifest.json \
  --keys app-pubkey.json
```

### Configuration defaults

Applications can provide update defaults in `oro.toml` (or `.ororc`) which
the native update service uses when corresponding options are not provided
programmatically:

- `update_channel` (flattened key)  
  - Default update channel when a `channel` option is not provided to
    `checkForUpdates`.  
  - Common values: `"stable"`, `"beta"`, `"nightly"`.  
  - When absent, `"stable"` is used.

- `update_max_manifest_bytes`  
  - Maximum allowed manifest size in bytes when `maxManifestBytes` is not
    passed to `checkForUpdates`.  
  - Manifests larger than this are rejected with `ERR_MANIFEST_TOO_LARGE`.

- `update_max_artifact_bytes`  
  - Maximum allowed artifact size in bytes when `maxArtifactBytes` is not
    passed to `checkForUpdates`/`downloadUpdate`.  
  - Artifacts larger than this are rejected with `ERR_ARTIFACT_TOO_LARGE`.

All per-call options (`channel`, `maxManifestBytes`, `maxArtifactBytes`,
etc.) continue to take precedence over configuration defaults when
explicitly provided.

A UDP/TCP deployment can mirror the same flow but substitute a custom
transport for the manifest/artifact fetch:

1. Use `oro:dgram` or `oro:tcp` to send the `CHECK` message and receive
   `RESPONSE` / `MANIFEST_CHUNK` messages.  
2. Reassemble the manifest bytes as described in the UDP/TCP section.  
3. Call `fetchManifest`-like logic (or `verifyManifestBytes`/`parseManifest`
   if you are reusing the implementation) with the reassembled bytes and the
   configured public key.  
4. Use `selectUpdate`, `downloadUpdate`, and/or `verifyArtifact` exactly as
   in the HTTP case.

## Server-side considerations

Publishers are free to implement infrastructure however they like, subject
to a few constraints:

- Manifests and signatures must be served as opaque byte streams (no
  on‑the‑fly rewriting).
- Artifact bytes must be stable and match the hashes declared in the
  manifest.
- Version/channel rules should stay monotonic for a given app to avoid
  downgrade attacks (for example, never republish an older binary with a
  newer version).
- Keys should be rotated rarely and with care; when rotation is necessary,
  ship a new app build that trusts the new key before publishing manifests
  signed solely by that key.

### Key management recommendations

- Keep update signing keys separate from other application secrets.  
- When rotating keys:
  - Ship a new app build that trusts both the old and new public keys.  
  - Start signing manifests with the new key while still allowing the old
    one for a defined window.  
  - Once a majority of clients are upgraded, stop accepting the old key.
- Record which key was used in the `keyId` field of `manifest.sig` so logs
  and tooling can easily audit past updates.

## Extensibility

The protocol is designed to evolve without breaking existing clients:

- `schemaVersion` allows new required fields to be introduced.
- New hash or signature algorithms can be added as additional values.
- Additional metadata (for example, delta update descriptors or mirrors) can
  be added without affecting signature verification, as long as publishers
  sign the full manifest bytes.

Future work can add:

- Multi‑role key hierarchies (root vs. release keys).
- Delta update formats and patch application helpers.
- Push notifications over the Oro network stack to notify clients of
  available updates without polling.

### Current implementation status and limitations

The implementation in this repository provides:

- Full HTTP binding support (manifest + signature fetch over HTTP(S)), now
  implemented natively in the Oro runtime (C++ service) and exposed via IPC.  
- Manifest verification using Ed25519 via libsodium in the native runtime
  (with a JS fallback for environments where the native service is
  unavailable).  
- Artifact integrity checks using SHA‑2 digests and optional length checks,
  performed natively when the update service is enabled.  
- An application-facing API in `oro:application/update` that delegates to
  the native update service when available and falls back to a
  transport-agnostic JS implementation that can be reused with custom
  transports.

Notable limitations and areas reserved for future work:

- Per-artifact signatures: the manifest format supports
  `signatureAlgorithm`/`artifactSignature`, but the current JS helpers only
  enforce hashes and lengths. Deployments that require per-artifact
  signatures must add verification on top or extend `verifyArtifact`.  
- `schemaVersion` handling: manifests and signature files carry
  `schemaVersion`. The current helpers enforce `schemaVersion === 1` for
  manifests and reject unknown values as unsupported. Future versions may
  allow callers to opt into accepting additional schema versions.  
- `appId` enforcement: callers can provide an `expectedAppId` option when
  fetching manifests. If present, the helpers enforce an exact match between
  `manifest.appId` and this value. Deployments that reuse signing keys
  across multiple products SHOULD set `expectedAppId` to avoid configuration
  mistakes.  
- UDP/TCP transport helpers: the binary framing and messages are specified,
  but no concrete `oro:application/update` UDP/TCP helpers ship yet. Deployments that
  require them should implement framing on top of `oro:dgram` / `oro:tcp`
  and then reuse the manifest/artifact verification APIs described above.
