# Changelog

Notable changes to Oro Runtime are documented here. During the `0.x` series,
incompatible public API changes require a minor release; compatible fixes use
a patch release.

## [Unreleased]

## [0.2.0] - 2026-10-27

### Added

- Expose platform extension requests and backend support discovery through
  `oro:platform`.
- Add authentication-bound encryption and signing keys, explicit password-provider
  selection, and document-owned cancellation with bounded native requests.
- Add Android finite and continuous foreground execution, deferred native work,
  and bundled declarative read/checkpoint/notification workflows with bounded
  retry. Applications retain server authorization, enrollment and outcome
  reconciliation responsibilities; these APIs do not run arbitrary background
  JavaScript.
- Add allowlisted historical protected imports, retained Android components,
  provenance-checked selection between AES and native-record histories, and an
  explicit bridge build before generated-adapter adoption. Originals and
  uncertain outcomes are preserved.
- Add native encrypted records, compare-and-swap updates and durable operation
  coordination. The shared C++ v2 format is separate from Android Kotlin v1;
  migration is explicit.
- Add UnifiedPush development tools through `oroc push server` and
  `oroc push distributor`, including Oro development distributors and support
  for existing distributors such as ntfy. Add bounded push journals, recovery
  inspection and native notification/response correlation.
- Add native launcher destinations, persisted document-provider grants,
  notification/channel inspection, Back handling and bounded Android system
  gesture exclusion.
- Add Linux native execution, scheduling, UnifiedPush D-Bus integration, desktop
  actions, explicit Secret Service password selection and protected-key providers
  for TPM2 and hardware PKCS#11.
- Add project-aware language-server tooling rooted at `oro.toml`, backend process
  lifecycle and cancellation handling, editor qualification and a VS Code client.
- Document that deprecation requires an API from a previous published release;
  unreleased API experiments are renamed or removed directly.

### Fixed

- Correct Android full-view rotation and managed-window attachment, apply OS text
  scaling without duplicate application hooks, and strengthen Activity, focus,
  keyboard, inset and renderer-recovery handling.
- Retain work submitted to paused native loops until resume, prevent Linux GLib
  pumping from bypassing the pause, preserve UDP handles/endpoints across repeated
  pause/resume, and restart TCP readers that were active before pause. Forward
  Linux lifecycle changes to the compiled WebKit extension's filesystem runtime.
- Preserve request ownership through cancellation and document/process changes,
  settle native worker callbacks on the correct thread, and stream IPC responses
  before producers finish.
- Share fail-closed TLS pin and authority checks with native unattended HTTPS
  reads; reject missing keys, malformed state and stale generations without
  silently replacing retained data.
- Support read-only SDK inputs with writable application staging and per-user
  CLI configuration. Bind compiled CLI/templates and Android API/source inputs
  to distribution metadata, including correctly quoted source fingerprints.
- Harden Windows Unicode window handling, CRT selection and native archive
  consumer linkage.

### Build and release

- Publish Linux source-development SDKs as immutable, verified generations with
  atomic activation, active-command leases, rollback retention and automatic
  collection of unused generations and interrupted staging. This local linking
  path remains separate from npm registry publication.
- Verify exact npm tarballs, read-only installations, first-run configuration,
  consumer builds and editor backends without using workspace API symlinks.
- Require native backend, Android Kotlin/JVM and language-server regression
  workflows through the reusable release source gate. Release tags always run
  validation even if their commit also belongs to an open pull request.
- Install checksum-pinned JVM test fixtures in CI so Android storage, migration,
  lifecycle, trust and cancellation checks execute instead of silently skipping.
- Keep native test source paths intact in copied desktop projects, explicitly
  expose the strict lifecycle flag, and run the pause/resume checks under the
  intended root-level lifecycle configuration.
- Stop npm staging on copy failures and avoid copying read-only Android library
  directories twice.

### Platform scope

The new high-level workflow and historical migration executors are Android-only.
Their Linux, macOS, iOS/iPadOS and Windows backends remain unimplemented and
report unavailable support or `ENOSYS`. Lower-level shared storage or native
Linux services do not imply those executors are available. Other new backend
limits and required runtime probes are documented in
[Native platform parity](docs/NATIVE_PLATFORM_PARITY.md) and each JavaScript
module's JSDoc. Public fixture and host coverage does not establish production
signing, private historical data, OEM behavior or complete device qualification.

## [0.1.8] - 2026-10-20

### Fixed

- Prevent Android asset-read crashes by reading packaged assets directly,
  handling missing or incomplete files safely, and rejecting null WebView
  response buffers.
- Correct UDP hostname resolution and address-family selection for bind,
  connect, and send, and use the IPv6 wildcard address for `udp6` bindings.
- Resolve Android secure-storage classes through the application class loader
  and handle JNI lookup failures without leaving pending exceptions.
- Use the application icon for Android notifications when generated mipmap
  resources are absent.

### Android local network access

- Add `local-network` permission query and request support for apps targeting
  Android 17 (API 37), including manifest generation and the
  `[permissions] allow_local_network` configuration gate. Handle denied or
  dismissed prompts and share a single prompt across concurrent requests.
- Document platform gating and the need to rebuild and reinstall existing apps
  to include the permission declaration and native request flow. See
  [Android local network access](docs/ANDROID_LOCAL_NETWORK.md).

### Build and development

- Recover incomplete libusb bootstrap files, validate CMake fallback inputs,
  refresh stale CMake toolchain caches, and isolate Android libusb build caches
  by NDK version and API level.
- Build llama before whisper on Android and iOS so required GGML headers and
  libraries are available.
- Generate Android Gradle wrappers in an isolated project without evaluating
  application plugins or forcing offline mode, and generate Groovy build
  scripts directly from CLI templates.
- Run standard Android build tools without requiring `--allow-exec` and expose
  native compiler and linker failures.
- Add regression coverage for dependency bootstrap, Android build generation,
  secure-storage class loading, local-network permissions, UDP hostnames, and
  resource reads; update API declarations and CLI documentation.

## [0.1.7] - 2026-10-14

### Fixed

- Retain static TLS provider registration during application linking and register
  built-in providers once before lookup. This fixes native `oro:tls` connections
  on Linux requiring an application-level registration shim
  ([#1](https://github.com/oro-computer/runtime/issues/1)).

### Development and CI

- Add a Linux SDK application regression test covering certificate-verified TLS
  connections and encrypted loopback traffic without a registration shim.

## [0.1.6] - 2026-09-28

### Fixed

- Link the packaged Iroh static library when building Windows desktop apps,
  and report a missing library before invoking the linker.

### Development and CI

- Run npm packaging, local/global installation, and production consumer builds
  on all five native hosts during the full branch release preflight. Verify all
  seven package manifests before tagging or publishing, reusing the built
  runtime archives. Registry publication still requires a verified signed tag.

## [0.1.5] - 2026-09-28

### Installation and packaging

- Normalize npm staging paths to forward slashes so Windows package builds
  accept valid staging directories. Preserve directory containment and symlink
  checks, and stop packaging if path resolution fails.
- Accept CRLF line endings when comparing the release version file with CLI
  output. Continue to reject version mismatches and propagate CLI failures.

## [0.1.4] - 2026-09-24

### Development and CI

- Cache individual Apple device and Simulator runtime objects through explicit
  ccache invocation, preserving compiler arguments and paths containing spaces.
- Build runtime target families sequentially when they outnumber host CPUs,
  while allowing parallel object compilation within each target.
- Enable Rust compiler caching for Unix release jobs and increase the macOS
  release compiler-cache limit to 3 GB.
- Save distinct release cache snapshots for each workflow run and attempt so
  repeated preflights can retain newly compiled objects. Stop compiler writers
  before saving partial caches after failures or timeouts.
- Allow up to 210 minutes for cold Apple SDK builds within a 240-minute job
  budget, leaving time for cache preservation and artifact validation. Retain
  production optimization and every advertised SDK target.

## [0.1.3] - 2026-09-23

### Development and CI

- Show Git diagnostics and exit codes when native dependency clones fail,
  making source-download failures visible in CI logs.
- Retry failed dependency clones up to three times with short delays and
  disable interactive Git credential prompts. Stop if a failed checkout leaves
  a partial directory, and continue to require the pinned commit before using
  downloaded sources.

## [0.1.2] - 2026-09-23

### Development and CI

- Honor `run_cross_platform: false` when release workflows call reusable CI,
  avoiding duplicate platform builds during release validation while retaining
  the full platform matrix for push and pull request CI.
- Give the connected UDP send test a ten-second budget for socket setup and
  packet delivery so iOS Simulator IPC setup does not exhaust a one-second
  deadline before sending. Report the stage on timeout, handle socket errors,
  and close both sockets after success or failure.

## [0.1.1] - 2026-09-23

### Installation and packaging

- Build desktop libusb with position-independent code on Linux and macOS so it
  can link into native shared extensions. Use separate staging output to avoid
  reusing older objects built without PIC.
- Handle paths containing spaces and shell punctuation in setup checksum
  commands, and propagate checksum failures.

### Release validation

- Accept Windows CRLF line endings when checking and updating the runtime
  version in `Cargo.lock`, preserving its line endings during updates.
- Resolve macOS temporary-directory symlinks before checking npm smoke-build
  output paths, while continuing to reject output outside the consumer project.
- Include verbose compiler diagnostics in npm package smoke builds to make
  native compilation and linker failures visible in CI logs.

## [0.1.0] - 2026-09-22

The first public preview of Oro Runtime brings native desktop and mobile
application development to HTML, CSS, and JavaScript.

### Runtime and API

- Build desktop and mobile applications with the `oroc` CLI and `oro:*`
  JavaScript APIs for Linux, macOS, Windows, Android, and iOS.
- Provide APIs for windows, files, networking, storage, workers, device access,
  native extensions, local AI inference, and peer-to-peer connections through Iroh.
- Include generated TypeScript declarations, API and CLI references,
  configuration documentation, and installed manpages.
- Preserve argument boundaries and output bytes in `oro:child_process` across
  platforms, with distinct shell-based `exec` and direct `spawn`/`execFile` behavior.

### Installation and packaging

- Package the runtime for Linux x64 and arm64, macOS x64 and arm64, and Windows
  x64 under the `@oro-computer` npm scope, with a shared CLI package and Node adapter.
- Include Android libraries in the Linux x64 package and iOS device and Simulator
  libraries in the macOS packages. Validate the advertised target libraries
  before packaging.
- Correct Windows package verification commands and quote desktop compiler paths
  so installations in directories containing spaces can build applications.
- Resolve runtime resources from the installed package, normalize installation
  prefixes, and default application builds and runs to the host platform.
- Limit first-time dependency setup to setup, build, and run commands.
- Verify exact npm tarballs on all five supported hosts through local and global
  installations in paths containing spaces. Check command shims, versions,
  installation prefixes, CommonJS and ESM adapters, and production desktop builds
  with their packaged JavaScript resources.

### Model Context Protocol

- Support MCP `2026-07-28` discovery, metadata, structured results, resource
  contents, and subscriptions, with initialize/session compatibility for
  `2025-11-25` and `2025-06-18` clients.
- Validate tool schemas, inputs, and outputs with JSON Schema 2020-12 support,
  bounded input size and depth, and stable errors. Adapt non-object structured
  results for MCP 2025 clients.
- Validate `x-mcp-header` declarations and expose tool and resource schemas,
  titles, icons, annotations, and metadata.
- Restrict CLI file reads to the workspace, require bearer authentication for
  CLI HTTP mode by default, and execute CLI arguments without a shell.
- Protect embedded HTTP OAuth with registered redirects, S256 PKCE, issuer and
  resource binding, one-time approval state, and bounded session storage.

### Release and supply chain

- Synchronize runtime, Rust, and npm versions with release metadata tools.
- Automate releases from verified, signed tags using the exact tagged commit,
  npm OIDC trusted publishing, and the `npm-publish` GitHub environment.
- Build eight desktop and mobile SDK distributions with SHA-256 checksums and
  SPDX SBOMs. Generate GitHub artifact attestations for tagged builds and npm
  provenance when publishing from the public repository.
- Require the complete verified archive and npm package sets before publication.
  Publish platform packages before dependent packages, and create the GitHub
  release after all seven npm packages succeed.
- Resume partial npm publication only when existing package integrity matches
  the original tarball. Keep local packaging helpers pack-only.
- Pin Git dependencies to immutable revisions and verify downloaded SQLite,
  WebView2, Android, JDK, and Gradle archives before use.
- Ship Apache-2.0 licensing, NOTICE, third-party notices, and collected native
  dependency license files.

### Development and CI

- Validate JavaScript, generated declarations, dependency integrity, release
  metadata, oxlint, Prettier, C++, and GitHub Actions workflows in CI.
- Support Node.js 22 and 24 with pnpm 11 and pinned GitHub Actions.
- Build and test Linux x64 and arm64, Android x86_64 and arm64-v8a, macOS and
  iOS on Intel and Apple Silicon, and Windows x64 on native hosted runners.
- Cache dependencies and compiler outputs, gate native jobs behind lint, and
  support manual builds of individual release artifacts.
- Resolve test CLI paths from the host architecture for arm64 validation.
- Add release, contribution, security, support, governance, issue, and pull
  request guidance for public collaboration.
- Document current MCP client flows, secure defaults, release recovery, and
  source-build prerequisites.
- Document and validate `NO_ANDROID` and `NO_IOS` as independent source-build
  controls: any non-empty value disables only the named target family. Reject
  packaged target directories that conflict with those controls.

### Requirements and validation limits

- npm installation requires Node.js 22 or newer. Application builds also require
  the native toolchain and SDKs for the target platform.
- npm package checks compile desktop applications; application launch, mobile
  execution, and dependency setup on fresh machines require separate validation.
