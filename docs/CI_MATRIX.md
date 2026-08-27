# Oro Runtime CI/CD Matrix

This document describes the community‑run CI/CD validation for the Oro Runtime
in this repository. The goals are:

- Give contributors a predictable, documented test matrix.
- Ensure changes are validated on at least one desktop target.
- Make it easy to extend coverage (extra OSes, architectures, or self‑hosted runners).

## Current GitHub Actions workflow

The primary workflow lives at `.github/workflows/ci.yml` with the name **CI**.

Triggers:

- `push` to `master`, `dev`, `jwerle/**`, `feature/**`, or `fix/**` branches.
- `pull_request` targeting this repository.
- Manual `workflow_dispatch` from the GitHub Actions UI.
- Reusable `workflow_call` from the signed-tag release chain.

Jobs overview:

- **Lint** validates all workflow files with `actionlint`, runs tooling unit tests, and executes the
  authoritative repository lint contract through `npm run test:lint:ci` on Linux.
- **Test platform** builds a fresh staged runtime and exercises the applicable desktop, runtime-core,
  iOS Simulator, or Android Emulator tests on every supported host axis.

The release packaging workflow lives at
`.github/workflows/release-artifacts.yml` with the name **Release Artifacts**.
It is distinct from the test workflow above and is used to publish downstream
runtime distributions on release tags or manual dispatches. Manual dispatches
can build either the full matrix or one selected `artifact_id` for pre-release
smoke validation on hosted runners. A release-tag run calls the same CI workflow
and waits for every lint and platform-test leg before any release archive build.

## Test matrix

| Matrix leg | Hosted runner | Native host | Mobile coverage | Node.js |
| --- | --- | --- | --- | --- |
| Linux x64 | `ubuntu-24.04` | x86_64 | none | 22 and 24 |
| Linux arm64 | `ubuntu-24.04-arm` | arm64 | none | 24 |
| Android | `ubuntu-24.04` | x86_64 | Android x86_64 and arm64-v8a | 24 |
| macOS + iOS x64 | `macos-15-intel` | x86_64 | iOS device and Simulator libraries; Simulator tests | 24 |
| macOS + iOS arm64 | `macos-14` | arm64 | iOS device and Simulator libraries; Simulator tests | 24 |
| Windows x64 | `windows-2022` | x86_64 | none | 24 |

Every desktop leg runs `npm test`, `npm run test:child-process`, `npm run test:mcp`, and
`npm run test:runtime-core`. The Android leg runs `npm run test:android-emulator`; each Apple leg
boots an available iPhone Simulator and runs `npm run test:ios-simulator`.

Android is intentionally cross-built on the Ubuntu x64 runner. The Android NDK distributes Linux
host tools for x86_64, while Oro emits both supported Android target ABIs from that host. The
Ubuntu arm64 leg is therefore native Linux arm64 coverage, not a second Android host build.

## Runtime build settings

The CI workflow stages the runtime beneath runner-temporary `ORO_HOME` and `PREFIX` directories.
Unix jobs use `./bin/install.sh --yes-deps`; Windows uses `bin/install.ps1 -yesdeps -verbose`.
All legs set `DEBUG=1` and `VERBOSE=1` for diagnosable CI builds.

Target inclusion is declared independently on every matrix leg:

- Desktop-only Linux and Windows set `NO_ANDROID=1` and `NO_IOS=1`.
- Android leaves `NO_ANDROID` empty, sets `NO_IOS=1`, and enables the explicit non-interactive
  Android CI controls.
- macOS and iOS set `NO_ANDROID=1` and leave `NO_IOS` empty.
- `NO_ANDROID` and `NO_IOS` are independent presence switches, not a combined mobile toggle.
  Non-empty values including `0` and `false` disable their respective targets; an enabled target's
  variable must be unset or empty. See [Source-build environment](BUILD_ENVIRONMENT.md).
- Desktop tests run with:
  - `ORO_TEST_HEADLESS=1` to prefer headless/browserless execution where supported.
  - `ORO_TEST_SKIP_DESKTOP_EXTENSION=1` and
    `ORO_TEST_SKIP_TEST_EXTENSIONS=1` to avoid building heavy native test
    extensions in constrained CI environments while still exercising the
    core/runtime.

After every build, CI checks the staged target directories against the declared family and rejects
mobile libraries in desktop-only output. The shared test launcher resolves `build/x86_64-desktop`
or `build/arm64-desktop` from the actual Node host architecture instead of assuming x64.

Contributors should still follow `CONTRIBUTING.md` for local builds. The hosted labels above are
standard GitHub-hosted x64, arm64, Intel macOS, Apple Silicon, and Windows runners; no self-hosted
runner registration is required. GitHub's
[hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
is the source of truth for their current architectures and capacity.

Linux support floor:

- Ubuntu versions lower than `20.04` are out of scope.
- GitHub-hosted validation and release packaging currently run on `ubuntu-24.04`.

## Release artifact matrix

The release workflow publishes separate downstream archives so one target family
does not block unrelated release assets:

- `linux-x64-desktop`
- `linux-x64-android-sdk`
- `linux-arm64-desktop`
- `macos-x64-desktop`
- `macos-x64-ios-sdk`
- `macos-arm64-desktop`
- `macos-arm64-ios-sdk`
- `windows-x64-desktop`

Each archive is built from a staged `$ORO_HOME` distribution. Desktop archives
contain the host CLI, headers, libraries, docs, and man pages for that host.
The mobile SDK archives contain the same host toolchain plus the mobile runtime
libraries for the advertised target family:

- `linux-x64-android-sdk` must include both `arm64-v8a` and `x86_64` Android
  targets built from the Linux x64 host.
- `macos-*-ios-sdk` must include the arm64 iOS device target and the x86_64
  Simulator target. The Apple Silicon artifact must also include the arm64
  Simulator target.

The release workflow pins the Android JDK to the exact version expected by
`bin/android-functions.sh`, so the hosted runner does not drift to a patch level
that the Android bootstrap rejects.

For a signed `v<version>` tag, release automation also builds the five platform npm tarballs,
the Node adapter, and the runtime meta-package. Every native runner installs its exact three-package
set and exercises the packaged CLI before upload. Publication then verifies the complete tarball set
and embedded package names and versions, uses npm trusted publishing, and publishes the GitHub
release only after all exact archive checksums and SPDX documents validate and npm succeeds. See
[Oro Runtime release automation](release/ORO_RELEASE_AUTOMATION.md).

## How to trigger CI for your changes

- Open a pull request against this repository.
  - The **CI** workflow runs automatically and reports status on the PR.
- Push commits to an existing PR branch.
  - CI re‑runs on the updated commit set.
- For long‑lived topic branches (e.g., `jwerle/run-48-…`), push directly to the
  branch; CI runs on `push` in addition to any open PRs.
- To re‑run checks without pushing a new commit:
  - Use the **Re-run jobs** button in the Actions tab, or
  - Trigger `workflow_dispatch` manually from the **CI** workflow page.
- To smoke-test release packaging before publishing a release:
  - Trigger `workflow_dispatch` on **Release Artifacts**.
  - Leave `artifact_id=all` to build the full hosted release matrix, or choose a
    single artifact target such as `linux-x64-desktop` or `windows-x64-desktop`
    when you only need to validate one packaging leg.

## Reading results and debugging failures

- Each job publishes logs directly in the GitHub Actions UI.
- Lint failures:
  - Look in the **Lint** job. It runs `actionlint`, Standard, TypeScript declaration
    generation, source-build environment documentation checks, oxlint, Prettier
    checks for managed files, and cpplint.
  - `lint:build-env` rejects public documentation or help that mentions only one
    of `NO_ANDROID` and `NO_IOS`, and validates their canonical presence-flag,
    desktop-only, first-time-setup, and application-target distinctions.
  - Standard output still lists the concrete file paths and rule violations that
    must be fixed when JavaScript style fails.
- Platform test failures:
  - The applicable **Test platform** matrix leg emits:
    - Build output from `./bin/install.sh` (including missing dependency hints).
    - Test runner output from `npm test` and `npm run test:runtime-core`.
  - Common issues:
    - Missing system packages (see hints printed by `bin/install.sh`).
    - An inherited non-empty `NO_ANDROID` or `NO_IOS` unexpectedly excluding only its corresponding
      target. Capture both variables separately when reproducing a source-build failure.
    - Tests that assume non‑headless environments; consider using
      `ORO_TEST_HEADLESS=0` locally when reproducing.

## Extending the matrix

The matrix is the blocking support contract. To propose additional coverage:

- Open a GitHub issue in this repository describing:
  - Desired OS/arch (e.g., `macos-latest`, `windows-latest`, self‑hosted label).
  - Any additional Node.js versions or test targets to include.
  - Whether failures on the new axis should be blocking or optional.
- Optionally send a pull request that:
  - Extends `matrix.os` or `matrix.node` in `.github/workflows/ci.yml`.
  - Adjusts environment variables or build steps to keep runtimes stable on the new platform.
  - Updates `test/unit/bootstrap-tooling.test.js` and this document with the new support axis.

## Feedback and community input

Feedback on the CI/CD matrix is welcome:

- File issues for flaky jobs, missing coverage, or confusing failure modes.
- Suggest improvements to this document or the workflow in pull requests.
- Use GitHub Discussions (where enabled for the org) to coordinate broader
  changes to the test strategy before sending large matrix expansions.
