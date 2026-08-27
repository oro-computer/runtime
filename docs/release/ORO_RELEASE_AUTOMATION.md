# Oro Runtime release automation

Oro Runtime uses one signed-tag release chain across the artifact and npm workflows. Every release
operation uses the exact commit named by a reviewed, verified signed tag. Platform archives and npm
tarballs are built on their native runners, npm publication uses short-lived GitHub OIDC credentials, and the
GitHub release becomes public only after the complete package family is available. The workflows
also support non-publishing manual package and artifact smoke builds.

## Version synchronization

`VERSION.txt` is the human-readable release version. The same version is also stored in
`clib.json`, all `@oro-computer/runtime*` npm manifests, and `rust/oro-iroh/Cargo.toml`.

```sh
node bin/set-release-version.js 0.1.0
node bin/check-release-version.js 0.1.0
```

`bin/version.sh` is a compatibility wrapper around the version setter. The checker also verifies
the internal platform-package dependency ranges and requires a matching changelog heading.

## Artifact workflow

Pushing a signed `v<version>` tag starts `.github/workflows/release-artifacts.yml`. It:

1. verifies the signed tag and synchronized release metadata;
2. calls the reusable CI workflow and waits for lint, generated-output checks, and every supported
   host/mobile test leg;
3. builds supported desktop and mobile-SDK distributions on their native runners;
4. smoke tests the staged `oroc` executable;
5. creates archives, SHA-256 checksum files, and SPDX SBOMs;
6. uploads artifacts and records GitHub artifact attestations;
7. requires the exact eight archive names and validates every checksum and SPDX document;
8. invokes the npm packaging and publication workflow only after that archive gate passes;
9. validates the downloaded assets again and publishes the GitHub release only after npm succeeds.

The trust anchor keeps the `v<version>` tag spelling, while package metadata and archive filenames
use the normalized SemVer without the tag's `v` prefix (for example, tag `v0.1.0` produces
`oro-runtime-0.1.0-linux-x64-desktop.tar.gz`). Manual and signed-tag runs therefore use the same
artifact naming convention.

Before archiving, each job validates its staged target directories against the
declared `desktop`, `android`, or `ios` family. Desktop artifacts must contain
neither mobile family, Android SDK artifacts must contain Android and no iOS
directories, and iOS SDK artifacts must contain Apple-mobile and no Android
directories. Android artifacts must include both `arm64-v8a` and `x86_64`;
Apple artifacts must include arm64 iOS device and x86_64 Simulator targets, plus
an arm64 Simulator target on Apple Silicon.

The build matrix controls mobile target families independently. `NO_ANDROID=<non-empty>` disables
only Android bootstrap and artifacts. `NO_IOS=<non-empty>` disables only iOS and iOS Simulator
work on macOS. Desktop-only jobs set both; Android SDK jobs unset `NO_ANDROID` while retaining
`NO_IOS`; iOS SDK jobs unset `NO_IOS` while retaining `NO_ANDROID`. Values such as `0` and `false`
are still non-empty and still disable the named target. See
[Source-build environment](../BUILD_ENVIRONMENT.md).

## npm workflow

`.github/workflows/publish-npm.yml` is a reusable release workflow and a manual package-inspection
workflow. Manual dispatch checks a signed tag, builds every package, and retains the tarballs
without publishing. A signed-tag `Release Artifacts` run calls the same workflow with publication
enabled.

Each platform package is built and packed on its native hosted runner. Before upload, that runner
installs its exact platform tarball together with the exact Node adapter and meta-package tarballs,
runs the packaged `oroc --version` and `oroc --help`, and loads the packed CommonJS Node adapter. A
single Ubuntu publication job then downloads those same tarballs, requires all seven expected
files, and verifies each embedded package name and version. It enters the `npm-publish` GitHub
environment and publishes in dependency order:

1. the five platform packages;
2. `@oro-computer/runtime-node`;
3. the `@oro-computer/runtime` meta-package.

The job uses npm trusted publishing with `id-token: write`; it has no long-lived `NPM_TOKEN` or
`NODE_AUTH_TOKEN`. npm automatically attaches provenance when the repository and packages are
public. If a workflow is rerun after a partial registry publication, it skips an existing version
only when npm's published integrity exactly matches the local tarball; a mismatch stops the
release.

The npm platform matrix also states both mobile controls explicitly. Linux x64
enables Android by leaving `NO_ANDROID` empty, sets `NO_IOS=1`, opts into the
non-interactive Android CI bootstrap, and accepts supported installer prompts.
Linux arm64 and Windows set both exclusions for desktop-only packages. macOS
sets `NO_ANDROID=1` while leaving `NO_IOS` empty so its advertised iOS
device/simulator libraries are included. Empty values opt a target in; false-like
non-empty values are never assigned directly to either presence flag.

`bin/publish-npm-modules.sh` supports the workflow through `--only-platforms`,
`--only-top-level`, `--no-rebuild`, `--yes-deps`, and `--dry-run`. It is deliberately pack-only and
cannot publish to a registry; `--dry-run` remains an explicit compatibility name for that pack-only
behavior. Local linking remains available through `npm run relink` and does not publish packages.
When relinking for desktop only, state both target exclusions explicitly:
`NO_ANDROID=1 NO_IOS=1 npm run relink`. Registry writes are limited to the guarded one-time package
reservation command below and the protected OIDC publication job.

## One-time repository and registry activation

The checked-in workflow is complete, but GitHub and npm trust relationships cannot be created from
repository code. Complete these account-level steps before pushing `v0.1.0`:

1. Make the repository public before the release so npm can generate public provenance.
2. In GitHub, create an environment named exactly `npm-publish`. Restrict deployment tags to the
   `v*.*.*` release-tag pattern used by this repository. Add required reviewers only if releases
   should pause for approval; with no reviewer rule, a valid signed tag proceeds without human
   input.
3. Add a repository ruleset for `v*.*.*` that restricts release-tag creation and deletion to the
   release maintainers. The workflow also rejects lightweight, unsigned, unverified, indirect, or
   version-mismatched tags.
4. Ensure organization and repository Actions policy permits the declared `contents`,
   `attestations`, and `id-token` write permissions.
5. Ensure all seven package names exist under the `@oro-computer` npm scope. npm requires a package
   to exist before a trusted publisher can be attached. For a brand-new package family, reserve
   each name with the guarded local bootstrap below; it uses
   `0.0.0-trusted-publishing-bootstrap.0` and the non-default `bootstrap` dist-tag, never version
   `0.1.0`:

   ```sh
   npm run release:bootstrap-npm
   npm run release:bootstrap-npm -- --publish --yes
   ```

   The first command is read-only and lists missing names. The second performs the irreversible
   package reservations using the maintainer identity already authenticated by the local npm CLI.
   This script is not called from GitHub Actions and is safe to rerun after a partial reservation.
6. In the settings for each package below, add the same GitHub Actions trusted publisher:
   - GitHub organization or user: `oro-computer`
   - repository: `runtime`
   - workflow filename: `release-artifacts.yml`
   - environment: `npm-publish`
   - allowed action: `npm publish`
7. Remove or revoke any write token used to bootstrap the names. The release workflow does not use
   it.

Configure the publisher for:

- `@oro-computer/runtime-linux-x64`
- `@oro-computer/runtime-linux-arm64`
- `@oro-computer/runtime-darwin-x64`
- `@oro-computer/runtime-darwin-arm64`
- `@oro-computer/runtime-win32-x64`
- `@oro-computer/runtime-node`
- `@oro-computer/runtime`

The trusted publisher must name `release-artifacts.yml`, not `publish-npm.yml`. npm validates the
calling workflow when `workflow_call` is involved. Both workflows already grant the required OIDC
permission. See npm's [trusted publishing documentation](https://docs.npmjs.com/trusted-publishers/)
for the account-side fields and current registry requirements.

## Release and recovery behavior

Once activation is complete, pushing one annotated, verified signed `v<version>` tag is the only
release trigger. A CI or build failure publishes nothing. A failure before the npm job publishes
nothing. A partial npm failure is recoverable by rerunning the same workflow because published
tarballs are integrity-checked. The GitHub release remains absent until the full test matrix, all
npm packages, and all release assets pass. Published npm versions and signed tags are immutable;
correct a defect with a new patch version.

Follow [RELEASE_CHECKLIST.md](../../RELEASE_CHECKLIST.md) for the complete release gate and
recovery policy.
