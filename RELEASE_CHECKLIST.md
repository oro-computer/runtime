# Oro Runtime release checklist

This checklist is the release gate for `v0.1.0` and later releases. Complete it from a clean,
reviewed commit. A release tag, npm publish, or GitHub release must not be created from an
uncommitted working tree.

## Release metadata

- [ ] Choose a SemVer version. During the `0.x` series, breaking changes require a minor release;
      compatible fixes use a patch release.
- [ ] Run `node bin/set-release-version.js <version>` and review every changed manifest.
- [ ] Move user-visible changes from `[Unreleased]` to a dated version in `CHANGELOG.md`.
- [ ] Run `node bin/check-release-version.js <version>`. This verifies `VERSION.txt`, `clib.json`,
      the npm package family, `rust/oro-iroh/Cargo.toml`, dependency ranges, and the changelog.
- [ ] Confirm package names, descriptions, repository URLs, licenses, included files, and platform
      constraints with the pack-only `bin/publish-npm-modules.sh` helper or the non-publishing manual
      npm workflow. Confirm the helper contains no registry publication path.

## Quality and compatibility

- [ ] Run `npm run lint` with Node.js 22 or 24, pnpm 11, Python 3, and `cpplint` installed.
- [ ] Run `npm run gen`, then confirm `git diff --exit-code` so generated declarations and docs are
      current.
- [ ] Run desktop integration tests and `npm run test:runtime-core` against a rebuilt `oroc`.
- [ ] Smoke test `oroc help`, `oroc --version`, project creation, build, and run on every supported
      desktop package.
- [ ] Exercise MCP `2026-07-28` discovery, tool calls, resource reads, validation errors, and
      `subscriptions/listen`. Cover JSON Schema 2020-12 input/output validation and object, array,
      primitive, and `null` structured results. Verify standard and `x-mcp-header` mirroring,
      encoding, static reachability, and body/header mismatch rejection. Exercise `2025-11-25`
      and `2025-06-18` initialize/session compatibility clients.
- [ ] Validate changed mobile surfaces on the applicable simulator or emulator.
- [ ] Review public API, CLI, config, and behavioral changes for migration notes.

## Security and provenance

- [ ] Review dependency updates and `THIRD_PARTY_NOTICES.md`; confirm every distributed dependency
      has an applicable license.
- [ ] Confirm dependency sources use HTTPS and immutable revisions or checksums.
- [ ] Confirm production builds do not set `DEBUG`, `VERBOSE`, or `ORO_ENABLE_SANITIZERS`.
- [ ] Review `NO_ANDROID` and `NO_IOS` independently for every artifact job. A non-empty
      `NO_ANDROID` must suppress only Android work; a non-empty `NO_IOS` must suppress only iOS and
      iOS Simulator work on macOS. Jobs excluding both mobile families must explicitly set both.
- [ ] Inspect dry-run platform tarballs and confirm reused staging inputs did not include an
      Android or Apple-mobile directory excluded by its corresponding `NO_ANDROID` or `NO_IOS`
      control. Do not treat `--no-android-fte` as an Android artifact exclusion.
- [ ] Confirm Android outputs contain both `arm64-v8a` and `x86_64`; confirm Apple-mobile outputs
      contain arm64 device and x86_64 Simulator targets, plus arm64 Simulator on Apple Silicon.
- [ ] Confirm the release workflow generates and validates a SHA-256 checksum and SPDX document for
      every exact expected archive name.
- [ ] Confirm GitHub artifact attestations and npm provenance are enabled.
- [ ] Review `SECURITY.md`, known limitations, and unresolved security reports.

## Build and publish

- [ ] For the first release only, complete every account-level activation step in
      `docs/release/ORO_RELEASE_AUTOMATION.md`: make the repository public, create the exact
      `npm-publish` GitHub environment, reserve all seven npm package names without consuming the
      release version, and configure each package's trusted publisher for
      `release-artifacts.yml` with the `npm-publish` environment.
- [ ] Confirm the `npm-publish` environment accepts only `v*.*.*` tags and a GitHub ruleset restricts
      creation and deletion of matching release tags to release maintainers.
- [ ] Run `npm run release:bootstrap-npm` to confirm all seven package names exist. If this is the
      first publication, authenticate the intended npm maintainer locally and explicitly run
      `npm run release:bootstrap-npm -- --publish --yes` before configuring the trusted publishers.
- [ ] Confirm no `NPM_TOKEN` or `NODE_AUTH_TOKEN` is configured for the release workflow. Revoke
      any temporary write token used to reserve the initial package names.
- [ ] If the package tarballs require human inspection before publication, configure a required
      reviewer on `npm-publish`. Download and inspect all seven tarballs while the signed-tag run is
      waiting for environment approval.
- [ ] Merge the reviewed release commit and wait for required CI checks on that exact commit.
- [ ] Create an annotated, signed `v<version>` tag on that commit and push the tag.
- [ ] Let `Release Artifacts` build all supported targets and package tarballs. If the
      `npm-publish` environment requires a reviewer, approve its deployment after inspecting the
      completed build legs. With no reviewer rule, the chain continues automatically.
- [ ] Confirm the release run's reusable CI gate passed lint, generated-output validation, and every
      Linux, Android, Apple, iOS Simulator, and Windows matrix leg before packaging began.
- [ ] Confirm the complete exact archive/checksum/SPDX set passed verification before the protected
      npm publication job started.
- [ ] Confirm every native runner installed its exact platform, Node-adapter, and meta-package
      tarballs and passed packaged CLI smoke tests; then confirm the publication job verified all
      seven manifests, published platform packages before dependent packages, and attached npm
      provenance.
- [ ] Download and independently verify the archives, checksums, SBOMs, attestations, and
      smoke-test logs.
- [ ] Verify a fresh global install on Linux, macOS, and Windows and confirm `oroc --version` reports
      the released version.
- [ ] Confirm the workflow published the GitHub release only after npm and archive verification
      succeeded. There is no separate draft-release publication step.

## After release

- [ ] Confirm the GitHub release, npm packages, checksums, SBOMs, and changelog are publicly
      reachable.
- [ ] Add a new empty `[Unreleased]` section if the release process changed it.
- [ ] Announce material compatibility or security notes.
- [ ] If a package is defective, publish a fixed patch. Do not overwrite tags or published npm
      versions; deprecate the affected npm version with a clear replacement message.

Application maintainers shipping an Oro app should separately review the production hardening
guidance in [SECURITY.md](SECURITY.md).
