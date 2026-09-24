# Changelog

Notable changes to Oro Runtime are documented here. During the `0.x` series,
incompatible public API changes require a minor release; compatible fixes use
a patch release.

## [Unreleased]

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
