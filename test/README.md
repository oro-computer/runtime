## Test Runner

The test app now ships with a single orchestrator:

```bash
node ./scripts/run.js [target] [options]
```

Key targets:

- `desktop` (default) — builds and runs the full TAP suite.
- `runtime-core` — executes the native runtime-core harness.
- `android`, `android-emulator`, `ios-simulator`, `desktop-autoindex`, `desktop-spa`, `cli`, `cli-ip`, `node` — delegated to their existing scripts.

Run `node ./scripts/run.js --list-targets` to see the complete list.

Before running a target, distinguish runtime source bootstrap from the test
target itself. A non-empty `NO_ANDROID` disables only Android bootstrap and
artifacts; a non-empty `NO_IOS` disables only iOS/iOS Simulator work on macOS.
They are independent presence flags, so `0`/`false` still disable the named
target. Use both only for a desktop-only runtime rebuild, and do not set the
variable for a mobile target you intend to test. See
[Source-build environment](../docs/BUILD_ENVIRONMENT.md).

The Android emulator target installs the emulator and one Google APIs system
image matching the host architecture if its versioned AVD is not already
available. A normal runtime build or relink does not download emulator packages.
With `ORO_ANDROID_CI` set, the test APK is installed with runtime permissions
granted so unattended runs do not wait on permission dialogs.

The iOS Simulator target installs fixtures before launching tests on the booted
simulator, or on `ORO_IOS_SIMULATOR_UDID` when set. CI supplies that UUID both to
the harness and to the build through an `ORO_RC` override of
`[settings.ios] simulator_uuid`, so the build and launch use the same device.

### Developer Ergonomics

- `--quick` skips native test extensions, reuses the staged workdir/fixtures, and disables the strict lifecycle pass for faster iteration.
- `--entry ./filename.js` runs a specific desktop entry (defaults to `./index.js`).
- `--reuse-workdir`, `--refresh-workdir`, `--keep-workdir` control the isolated copy of the test app when extensions are disabled.
- `--skip-test-extensions` avoids building sqlite/runtime-core helpers.
- `--skip-desktop-extension` skips the heavyweight desktop extension build (helpful on CI or Linux workstations).
- Additional `--oroc-arg <value>` flags are forwarded directly to `oroc`.

### Filtering Tests

The built-in `oro:test` runner now honours the following environment variables (or matching CLI flags):

- `--match <regex>` / `ORO_TEST_GREP` — only run tests whose name matches the pattern.
- `--skip <regex>` / `ORO_TEST_SKIP` — skip tests whose name matches the pattern.
- `--only <pattern>` / `ORO_TEST_ONLY` — focus the run on selected tests.

Each pattern accepts comma-separated entries or `/regex/flags` syntax. Filter matches are reported in the TAP output.

### Dependency Management

`node ./scripts/run.js ...` installs `test/` dependencies on-demand unless `--skip-install` is provided. Use `--install` (or `--force-install`) to force a fresh install.
