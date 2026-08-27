# Changelog

All notable user-visible changes to Oro Runtime are documented here.

## [Unreleased]

## [0.1.0] - 2026-08-27

This is the first public preview of Oro Runtime. The `0.x` series follows
Semantic Versioning: incompatible public API changes may occur in minor
releases and will be documented here.

### Runtime and API

- Ship the cross-platform `oroc` CLI and the public `oro:*` JavaScript API
  surface for Linux, macOS, Windows, Android, and iOS development.
- Publish generated TypeScript declarations, JavaScript API reference, CLI and
  configuration reference, manpages, and installed runtime documentation.
- Provide application, filesystem, networking, storage, worker, webview,
  device, native extension, AI, and Iroh integration surfaces.

### Model Context Protocol

- Implement MCP `2026-07-28` discovery, per-request metadata and mirrored
  headers, structured results, result metadata, resource contents, and
  `subscriptions/listen`.
- Validate `x-mcp-header` declarations at registration, including static
  property reachability, ASCII token names, primitive types, safe integers,
  and case-insensitive uniqueness.
- Retain explicit MCP `2025-11-25` and `2025-06-18` initialize/session compatibility paths.
- Advertise current tool and resource titles, schemas, icons, annotations,
  sizes, and `_meta` fields.
- Validate tool schemas at registration and tool inputs and outputs at execution
  with standards-compliant JSON Schema 2020-12 handling, explicit alternate
  dialect support, bounded input size/depth, and stable error results.
- Support arbitrary JSON values in modern `structuredContent`, preserve their
  serialized text representation, and adapt non-object results for legacy clients.
- Scope file reads to the workspace and require a generated bearer token in
  CLI HTTP mode by default.
- Harden embedded HTTP OAuth with protected-resource discovery, pre-registered
  redirects, S256 PKCE, issuer and resource binding, one-time approval state,
  secure random tokens, and bounded request/session/event memory.
- Bound CLI MCP file, output, timeout, directory, and write inputs, reject
  misspelled tool arguments, and execute CLI arguments without a shell.
- Preserve exact `oro:child_process` argument boundaries and output bytes across
  platforms, while keeping `exec` shell semantics distinct from direct `spawn`
  and `execFile` execution.

### Release and supply chain

- Synchronize `0.1.0` across runtime, Rust, and npm package metadata with
  dedicated version-set and version-check commands.
- Publish the `@oro-computer` platform packages, Node adapter, and top-level npm
  meta-package through a signed-tag workflow using npm OIDC trusted publishing
  and automatic provenance, without a long-lived registry token.
- Bind archive and npm builds to the commit named by the verified signed tag,
  smoke-install exact package tarballs on every native runner, verify the
  complete tarball family and embedded manifests, safely resume partial
  publications by integrity, and publish platform packages before dependent
  packages.
- Keep the local npm packaging helper pack-only, remove direct package publish
  shortcuts, and confine release publication to the protected OIDC job.
- Provide a guarded, rerunnable local bootstrap for reserving previously
  unpublished npm package names before attaching OIDC trusted publishers.
- Generate checksums, SPDX SBOMs, GitHub artifact attestations, and release
  assets for supported target archives, including Linux arm64; require every
  exact filename and validate its checksum and SPDX document before npm starts
  and again before publishing the GitHub release.
- Normalize signed `v<version>` tags to unprefixed SemVer in archive names so
  signed-tag and manual package runs produce identical artifact identities.
- Pin Git dependencies to immutable revisions and verify downloaded SQLite,
  WebView2, Android, JDK, and Gradle archives before use.
- Ship Apache-2.0 licensing, NOTICE, third-party notices, and collected native
  dependency license files.

### Contributor experience

- Validate JavaScript, generated declarations, dependency integrity, release
  metadata, oxlint, Prettier, C++, and GitHub Actions workflows in CI.
- Support Node.js 22 and 24 with pnpm 11 and pinned GitHub Actions.
- Build and test Linux x64 and arm64, Android x86_64 and arm64-v8a, macOS and
  iOS on Intel and Apple Silicon, and Windows x64 on native hosted runners.
- Resolve test CLI paths from the actual host architecture so arm64 validation
  does not depend on x86_64 build directories.
- Add release, contribution, security, support, governance, issue, and pull
  request guidance for public collaboration.
- Document current MCP client flows, secure defaults, release recovery, and
  source-build prerequisites.
- Document `NO_ANDROID` and `NO_IOS` as independent, presence-based source-build
  controls across installer help, contributor, CI, release, support, example,
  generated CLI, and installed runtime references.
- Enforce that paired documentation contract during lint, distinguish
  `--no-android-fte` prompt suppression from target exclusion, and prevent
  reused npm staging trees from leaking excluded Android or Apple-mobile
  directories into platform packages.
- Make archive and npm platform matrices declare both mobile-family intents,
  fail packaging when actual target directories disagree, and opt the Linux
  x64 npm package into its advertised non-interactive Android toolchain build.
- Require both advertised Android ABIs and the complete host-appropriate iOS
  device/Simulator target set before CI or release packaging can pass.
