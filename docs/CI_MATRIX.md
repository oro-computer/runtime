# Oro Runtime CI/CD Matrix

This document describes the community‑run CI/CD validation for the Oro Runtime
in this repository. The goals are:

- Give contributors a predictable, documented test matrix.
- Ensure changes are validated on at least one desktop target.
- Make it easy to extend coverage (extra OSes, architectures, or self‑hosted runners).

## Current GitHub Actions workflow

The primary workflow lives at `.github/workflows/ci.yml` with the name **CI**.

Triggers:

- `push` to `dev`, `jwerle/**`, `feature/**`, or `fix/**` branches.
- `pull_request` targeting this repository.
- Manual `workflow_dispatch` from the GitHub Actions UI.

Jobs overview:

- **Lint** — validates the GitHub Actions workflow files with `actionlint`, then runs the authoritative repo-wide lint contract via `npm run test:lint:ci` on Linux.
- **Desktop + runtime-core tests** — builds the Oro Runtime CLI and runs the
  desktop and runtime-core test suites on Linux across a Node.js matrix.

The release packaging workflow lives at
`.github/workflows/release-artifacts.yml` with the name **Release Artifacts**.
It is distinct from the test workflow above and is used to publish downstream
runtime distributions on release tags or manual dispatches. Manual dispatches
can build either the full matrix or one selected `artifact_id` for pre-release
smoke validation on hosted runners.

## Test matrix

The current matrix is intentionally conservative and can be expanded as the
community exercises the pipeline:

- Operating systems:
  - `ubuntu-24.04`
- Node.js versions:
  - `18.x` (LTS)
  - `20.x` (LTS)
- Test targets:
  - Lint: `npm run test:lint:ci`
  - Desktop tests: `npm test` (desktop target via `test/scripts/run.js`)
  - Runtime core tests: `npm run test:runtime-core`

## Runtime build settings

The CI workflow builds the `oroc` CLI before running tests:

- `./bin/install.sh` runs on `ubuntu-24.04` with:
  - `NO_ANDROID=1` and `NO_IOS=1` to avoid mobile toolchain setup in CI.
  - `DEBUG=1` to produce debug builds useful for diagnosing failures.
  - `VERBOSE=1` to log build configuration and any dependency advice.
- Desktop tests run with:
  - `ORO_TEST_HEADLESS=1` to prefer headless/browserless execution where supported.
  - `ORO_TEST_SKIP_DESKTOP_EXTENSION=1` and
    `ORO_TEST_SKIP_TEST_EXTENSIONS=1` to avoid building heavy native test
    extensions in constrained CI environments while still exercising the
    core/runtime.

Contributors should still follow the guidance in `CONTRIBUTING.md` for local
builds (for example, running `./bin/install.sh` before `npm test`), but the CI
matrix is designed to work out of the box on GitHub‑hosted Linux runners.

Linux support floor:

- Ubuntu versions lower than `20.04` are out of scope.
- GitHub-hosted validation and release packaging currently run on `ubuntu-24.04`.

## Release artifact matrix

The release workflow publishes separate downstream archives so one target family
does not block unrelated release assets:

- `linux-x64-desktop`
- `linux-x64-android-sdk`
- `macos-x64-desktop`
- `macos-x64-ios-sdk`
- `macos-arm64-desktop`
- `macos-arm64-ios-sdk`
- `windows-x64-desktop`

Each archive is built from a staged `$ORO_HOME` distribution. Desktop archives
contain the host CLI, headers, libraries, docs, and man pages for that host.
The mobile SDK archives contain the same host toolchain plus the mobile runtime
libraries for the advertised target family:

- `linux-x64-android-sdk` includes Android ABIs built from the Linux x64 host.
- `macos-*-ios-sdk` includes the macOS host toolchain plus iOS device/simulator
  libraries built on the corresponding macOS runner.

The release workflow pins the Android JDK to the exact version expected by
`bin/android-functions.sh`, so the hosted runner does not drift to a patch level
that the Android bootstrap rejects.

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
    generation, oxlint, Prettier checks for managed files, and cpplint.
  - Standard output still lists the concrete file paths and rule violations that
    must be fixed when JavaScript style fails.
- Desktop/runtime-core test failures:
  - The **Desktop + runtime-core tests** job emits:
    - Build output from `./bin/install.sh` (including missing dependency hints).
    - Test runner output from `npm test` and `npm run test:runtime-core`.
  - Common issues:
    - Missing system packages (see hints printed by `bin/install.sh`).
    - Tests that assume non‑headless environments; consider using
      `ORO_TEST_HEADLESS=0` locally when reproducing.

## Extending the matrix

The matrix is intentionally minimal to keep CI turnaround reasonable. To propose
additional coverage:

- Open a GitHub issue in this repository describing:
  - Desired OS/arch (e.g., `macos-latest`, `windows-latest`, self‑hosted label).
  - Any additional Node.js versions or test targets to include.
  - Whether failures on the new axis should be blocking or optional.
- Optionally send a pull request that:
  - Extends `matrix.os` or `matrix.node` in `.github/workflows/ci.yml`.
  - Adjusts environment variables or build steps to keep runtimes stable on the
    new platform.

## Feedback and community input

Feedback on the CI/CD matrix is welcome:

- File issues for flaky jobs, missing coverage, or confusing failure modes.
- Suggest improvements to this document or the workflow in pull requests.
- Use GitHub Discussions (where enabled for the org) to coordinate broader
  changes to the test strategy before sending large matrix expansions.
