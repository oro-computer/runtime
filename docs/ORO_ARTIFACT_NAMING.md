# Oro Runtime Artifact Naming

This document defines the canonical naming for Oro Runtime build artifacts across
platforms and packaging ecosystems.

## Native libraries and archives

Primary Oro Runtime libraries use the `oro-runtime` stem:

- **Static libraries (desktop/mobile toolchains)**
  - Linux/macOS: `liboro-runtime.a`
  - Windows (MSVC): `oro-runtime.lib`
- **Dynamic libraries / shared objects (where produced)**
  - Linux: `liboro-runtime.so`
  - macOS: `liboro-runtime.dylib`
  - Windows: `oro-runtime.dll`

## pkg-config files

Oro Runtime ships a pkg-config file using the Oro name:

- Primary file: `oro-runtime.pc`
  - `Name: oro-runtime`
  - `Version: <runtime version>`

## CLI and configuration artifacts

- CLI binary: `oroc`
- Project config: `oro.toml`
- Per-developer overrides/secrets: `.ororc`
- Environment variables: `ORO_*`

## NPM packages and scopes

Oro Runtime publishes packages under the `@oro-computer` scope:

- `@oro-computer/runtime`
- `@oro-computer/runtime-{darwin,linux,win32}-{arm64,x64}`
- `@oro-computer/runtime-node`

These package names are the npm distribution policy. They are not the same thing
as GitHub release archive names, and they are not a promise that every package
axis is emitted by every GitHub Actions release run.

## GitHub release archives

The `Release Artifacts` workflow publishes built downstream runtime
distributions as archives named:

- `oro-runtime-<version>-linux-x64-desktop.tar.gz`
- `oro-runtime-<version>-linux-arm64-desktop.tar.gz`
- `oro-runtime-<version>-linux-x64-android-sdk.tar.gz`
- `oro-runtime-<version>-macos-x64-desktop.tar.gz`
- `oro-runtime-<version>-macos-x64-ios-sdk.tar.gz`
- `oro-runtime-<version>-macos-arm64-desktop.tar.gz`
- `oro-runtime-<version>-macos-arm64-ios-sdk.tar.gz`
- `oro-runtime-<version>-windows-x64-desktop.zip`

The `*-desktop` archives are host runtime distributions for that OS/arch. The
`*-android-sdk` and `*-ios-sdk` archives are host distributions that include the
host `oroc` CLI plus the corresponding Android or iOS runtime libraries needed
to build downstream apps for those targets.

The source-build controls used to produce these classes are intentionally
separate. A non-empty `NO_ANDROID` disables only Android bootstrap/artifacts; a
non-empty `NO_IOS` disables only iOS/iOS Simulator work on macOS. Desktop-only
jobs set both explicitly, while each mobile SDK job enables its own family by
leaving that family's variable unset. `0` and `false` are non-empty and still
disable the named family. See [Source-build environment](BUILD_ENVIRONMENT.md).

The release archive matrix is intentionally narrower than the npm naming policy:

- GitHub-hosted release runners currently cover Linux x64, Linux arm64, macOS
  x64, macOS arm64, and Windows x64.
- Android support is built from Linux x64 hosts.
- iOS support is built from macOS x64 and macOS arm64 hosts.
- Windows arm64 release archives are not emitted by the current hosted workflow.

## Updating this policy

If artifact naming changes, update this document alongside the implementation
and ensure `test/unit/bootstrap-tooling.test.js` continues to enforce Oro-only
release tooling.
