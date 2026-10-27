# Oro Runtime source-build environment

This document defines the environment controls used while building the Oro
Runtime source tree. These controls affect runtime bootstrap, native dependency
work, and the runtime libraries staged by `bin/install.sh` or `npm run relink`.
They do not select the target for a downstream application build; use
`oroc build --platform=<platform>` for that.

## Mobile target exclusion controls

`NO_ANDROID` and `NO_IOS` are two independent, presence-based environment
variables. They are intentionally not aliases and there is no combined
"disable mobile" variable.

| Variable     | Exact effect                                                                                                                                                                                                 | What it does not do                                                                   |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------- |
| `NO_ANDROID` | When set to any non-empty value, disables Android setup prompts, Android SDK/NDK bootstrap work, Android ABI runtime-library builds, and Android artifacts staged by the source installer and npm packaging helper. | It does not disable iOS, the iOS Simulator, macOS desktop, or another desktop target. |
| `NO_IOS`     | When set to any non-empty value, disables iOS and iOS Simulator dependency, runtime-library, prebuild, and staging work on macOS. It is harmless but has no additional target to suppress on Linux or Windows.    | It does not disable Android, macOS desktop, or another desktop target.                |

Both variables are presence switches. `NO_ANDROID=0`, `NO_ANDROID=false`,
`NO_IOS=0`, and `NO_IOS=false` are still non-empty and therefore still disable
their respective target. To enable a target, leave its variable unset or set it
to the empty string.

The variables may be used separately:

```sh
# Build desktop and any eligible Apple targets, but no Android artifacts.
NO_ANDROID=1 ./bin/install.sh

# On macOS, build desktop and any eligible Android artifacts, but no iOS or
# iOS Simulator artifacts.
NO_IOS=1 ./bin/install.sh
```

For an explicitly desktop-only source build, set both variables. This is
portable and makes CI intent clear even though `NO_IOS` only suppresses extra
work on macOS:

```sh
NO_ANDROID=1 NO_IOS=1 ./bin/install.sh
```

Use both controls for a desktop-only relink as well:

```sh
VERBOSE=1 DEBUG=1 NO_ANDROID=1 NO_IOS=1 npm run relink
```

Do not infer one variable from the other in scripts, examples, or workflows.
A job that wants to exclude both mobile target families must set both names
explicitly.

Do not assign a YAML or other matrix boolean directly to either environment
variable: the rendered string `false` is non-empty and therefore disables the
target. Model intent with neutral names such as `EXCLUDE_ANDROID` and
`EXCLUDE_IOS`, then explicitly set the corresponding `NO_*` variable to a
non-empty value or leave it unset/empty in the shell step.

### `--no-android-fte` is not `NO_ANDROID`

The installer option `--no-android-fte` only suppresses the interactive Android
first-time setup path. When no Android SDK is configured, that can cause the
current run to omit Android work; when a usable Android toolchain is already
configured, Android artifacts may still be built. It is therefore not a stable
target-exclusion control.

Use `NO_ANDROID=<non-empty>` whenever the intent is to exclude Android setup
and artifacts, including non-interactive and release builds. There is no iOS
equivalent of the Android first-time-experience option; use
`NO_IOS=<non-empty>` to exclude iOS and iOS Simulator work.

### Precedence

An inherited non-empty exclusion variable wins over setup discovery and CI
enablement. In particular, `NO_ANDROID` suppresses Android work even if an
Android SDK is installed or `ORO_ANDROID_CI` requests Android setup.
`NO_IOS` suppresses the Apple-mobile paths even when the host is capable of
building them. Unset the corresponding variable to opt the target back in.

## Application build caches

Application builds opt in to supported build result caches with `oroc build --cache`
or a project default in `oro.toml`:

```toml
[build]
cache = true

[android]
# Keep the Gradle task output and native compiler caches, but configure every build.
configuration_cache = false

[ios]
cache = false
```

All optional build result caches default to off. `[android] cache = true` enables
only Android caching, and `[android] configuration_cache = true` enables only
Gradle configuration caching. Android emulator builds inherit `[android]` settings;
iOS simulator builds inherit `[ios]`. The other platform sections are `[linux]`,
`[mac]`, and `[win]`.

| Cache | Configuration keys | CLI overrides |
| :--- | :--- | :--- |
| All supported build result caches | `[build] cache`, `[<platform>] cache` | `--cache`, `--no-cache`; Android also accepts `--android-cache`, `--no-android-cache` |
| Native object compilation | `[build] compiler_cache`, `[<platform>] compiler_cache` | `--compiler-cache`, `--no-compiler-cache` |
| Gradle task outputs | `[android] build_cache` | `--android-build-cache`, `--no-android-build-cache` |
| Gradle configuration | `[android] configuration_cache` | `--android-configuration-cache`, `--no-android-configuration-cache` |

Explicit CLI choices override configuration. Within the CLI, a cache-specific flag
overrides `--android-cache`/`--no-android-cache`, which override `--cache`/`--no-cache`.
Using both the positive and negative form of the same flag is an error.
Within configuration, a platform's cache-specific key overrides its `cache` key,
then the corresponding `[build]` cache-specific key, then `[build] cache`.
Omit a key to inherit; set it to `false` to disable it at that level.
For example, `oroc build --platform=android --cache --no-android-configuration-cache`
enables the Gradle task output cache and native compiler caching while disabling
the configuration cache for that invocation.

Native compiler caching uses `ccache` from `PATH`, falling back to `sccache`;
Windows prefers `sccache`. Oro caches the desktop application's configuration
object, desktop and iOS extension objects, and Android NDK compilation. The
desktop configuration object is compiled separately before linking so the
launcher can cache it. Both manual NDK builds and Gradle's `externalNativeBuild`
receive the resolved `NDK_CCACHE` choice. If neither launcher is installed, Oro
reports that compiler caching is unavailable and compiles directly. Once selected,
launcher or compiler failures fail the build. Existing launcher settings, cache
locations, size limits, and storage backends remain managed by the launcher.
See the [ccache manual](https://ccache.dev/manual/latest.html) and
[sccache documentation](https://github.com/mozilla/sccache).

Gradle receives explicit enable/disable switches on every application invocation,
and the generated `gradle.properties` records the same values. Configuration cache
compatibility problems fail the build; warning mode is not enabled. Wrapper
generation always disables both Gradle caches. Project caches remain in the
generated Android output directory between builds; deleting that directory also
removes its configuration cache. See Gradle's
[configuration cache](https://docs.gradle.org/current/userguide/configuration_cache_enabling.html)
and [build cache](https://docs.gradle.org/current/userguide/build_cache.html) guides.

Custom build hooks receive `ORO_BUILD_CACHE` and `ORO_COMPILER_CACHE` as `1` or `0`
to reflect the resolved policy. Hooks must map these choices to their own build
tools. Oro does not inject flags into arbitrary scripts. Xcode-managed compilation
and other build systems without an Oro cache adapter retain their existing
behavior. These application options do not control runtime source bootstrap
(`bin/install.sh`), dependency download caches, runtime data caches, or toolchains'
ordinary incremental builds. Disabling a cache bypasses it without deleting entries.

Incremental runtime builds compare version/revision flags beside the version
object and in the archive input key. A revision change refreshes native identity
even with unchanged source timestamps or `--ignore-header-mtimes`; archive
refresh also works within the same timestamp second. Other unchanged objects
remain reusable. Commit labels identify the base revision; use the separately
bound source/input and APK hashes to identify uncommitted development snapshots.
Object paths retain their source extension before the object suffix. Mixed
sources with the same basename remain distinct, including Objective-C++ files,
and parent directories containing `.c`, `.cc`, spaces or other dots are preserved.

## Android build and emulator packages

Android source builds install only packages required to compile runtime
artifacts: Android SDK Platform 37.0, Build Tools 36.0.0, Platform-Tools,
Command-line Tools 23.0, and NDK r29. The generated Android project uses Android
Gradle plugin 9.3.2 with Gradle 9.5.0 and Java 17 bytecode. These versions are
an Android-documented compatibility set; do not advance one component without checking the
Android Gradle plugin compatibility table and synchronizing the shell bootstrap
and generated project templates.

The app compiles and targets API 37 while retaining `minSdk 26`. Native
runtime libraries therefore use NDK API 26 as their ABI floor; the SDK used to
compile application sources must not be reused as the NDK minimum platform.

The normal `./bin/install.sh` and `npm run relink` paths do not install the
Android Emulator or any system image. Those downloads are test/run
dependencies, not build dependencies. `npm run test:android-emulator` installs
only missing emulator components and exactly one Google APIs system image
matching the host architecture, then creates a versioned AVD when it is absent.
Setup and boot waits emit progress and have bounded timeouts. Likewise, an
application build for `android-emulator` requests one host-compatible image,
while an `android` device build does not.

The Android device test runner uses `ANDROID_SERIAL` when supplied and rejects
an ambiguous, offline or unauthorized target before uninstalling or installing
its test package. Fixture extraction and log polling keep that selected device.
The CLI and emulator runner start their own emulator on port 5554 by default; use an even
`ORO_ANDROID_EMULATOR_PORT` from 5554 through 5584 to select another port. An
explicit `ANDROID_SERIAL` must match `emulator-<port>`. An already occupied
target is rejected. The runner stops only its own bootstrap/emulator processes,
including on failure or timeout; it does not stop other emulators or the shared
ADB server. The CLI requires the literal `sys.boot_completed` value `1`, its
selected AVD identity and both package/activity services. ADB probes, installation
and activity launch use bounded clients and the selected serial. Boot readiness
does not establish a healthy WebView or a passing application qualification.

Android setup caches successful completion only. Configuration writes, package
installation and license failures propagate to the source installer, native
library builder and compiler-flag generator. SDK executable paths and package
identifiers are passed as individual arguments, including paths with spaces.
Fresh setup still requires writable user configuration and the third-party
tools' own writable caches; a build with cached prerequisites does not qualify
missing-dependency installation.

## Related source-build controls

- `DEBUG=<non-empty>` enables debug runtime artifacts. On Windows it also
  selects the separate debug library layout.
- `VERBOSE=<non-empty>` prints additional source-build diagnostics.
- `CPU_CORES=<count>` overrides detected build parallelism.
- `ORO_CI_FAST_COMPILE=<non-empty>` keeps CI correctness builds unoptimized
  while omitting debug-symbol generation. Debug builds still define
  `ORO_RUNTIME_BUILD_DEBUG`; release and distribution builds must leave this
  variable unset.
- `ORO_RUNTIME_SOURCE_PREFLIGHT=0` disables the Windows syntax-only preflight
  when a following archive and executable build will compile the same source
  surface. Set it to `1` to run the standalone source audit before dependency
  compilation.
- `ORO_HOME=<path>` chooses the staged runtime home.
- `PREFIX=<path>` chooses the installation/link prefix.

## Installed CLI configuration

The npm CLI keeps new first-run toolchain configuration outside the installed
SDK. `ORO_ENV_FILE` selects an explicit configuration file; relative values are
resolved against the invoking directory. Otherwise `ORO_CONFIG_HOME` selects
its directory. The default directory is `$XDG_CONFIG_HOME/oro` (or
`~/.config/oro`) on Linux, `~/Library/Application Support/oro` on macOS, and
`%LOCALAPPDATA%\oro` (or `~/AppData/Local/oro`) on Windows. The default filename
is `.oro.env`; `ORO_ENV_FILENAME` can choose a different name.

The launcher passes the selected absolute `ORO_ENV_FILE` to dependency setup and
the native CLI. Setup creates a private user directory/file when required and
never needs to create `.oro.env` inside a read-only SDK. An existing user file
wins over a legacy SDK-local configuration. If there is no user file and no
explicit override, an existing SDK-local file remains readable for compatibility;
it is not copied or rewritten by first-run setup. Inspection commands such as
`--version` and `--help` do not create configuration or run setup.
Setup logs also use the selected configuration directory unless
`ORO_INSTALL_SH_LOGFILE` explicitly selects another path.

The native CLI fills unset variables from the project's `.oro.env`, then the
selected `ORO_ENV_FILE`, then legacy SDK defaults. Existing process environment
variables take precedence. A directly invoked native binary can use
`ORO_ENV_FILE` too. Source bootstrap retains the source-local `.oro.env` default
unless that override is set. An external configuration is not copied into the
SDK by the source installer. SDK/NDK/JDK installations and user caches still need
writable locations; read-only SDK support does not install missing host tools.

On Unix hosts, Android first-run setup checks the selected NDK, platform, platform-tools
and build-tools packages for matching installed-package metadata and required
tools. When they are already installed, setup can write the external user
configuration without contacting the SDK repository or requesting licenses for
new packages. Standard Android builds use the same installed-package check.
Missing metadata or required files still triggers SDK manager
provisioning; failed provisioning is reported as failure and can be retried.
This offline setup path uses existing tools and does not acquire missing build
dependencies or Gradle artifacts.

## Stable Linux checkout installation

`pnpm relink` (`npm run relink`) publishes a complete copied Linux SDK/API
pair under `.local/share/oro-sdk/linux`, outside disposable `build/` output.
Override this location with `ORO_LOCAL_SDK_HOME`; keep it outside directories
that cleanup removes. The source builder prepares a fresh private staging
directory. It does not remove the running installation or link its command
to an incomplete build. Partial `--only-platforms`/`--only-top-level` selections
cannot be combined with Linux `--link`.

Publication recursively follows source, API, header, object and archive links,
then verifies the copied bytes against both the input capture and compiled
SDK metadata. A changed input, link cycle, unsupported file or mismatched
compiler prevents activation. The payload contains no workspace links; it is
read-only after validation. Editing workspace source therefore does not update
the active SDK. Rebuild/relink to publish the change. A CLI source fingerprint
identifies its source inputs; it does not imply identical compiler or archive
bytes. Each generation also records hashes of every payload file.

The stable launcher resolves `current` once per invocation. API imports, native
binaries and archives then come from that retained physical generation. A
single atomic pointer switch exposes the next complete pair. In-flight commands
keep their previous generation. Concurrent publishers serialize activation;
failed post-activation verification restores the previous pointer. Linux kernel
leases protect generations used by the managed launcher and its normal Node,
shell and native subprocesses, including detached children and interrupted
supervisors. A journal preserves rollback information across an interrupted
activation. Existing publication and generation lock files are opened read-only by the
launcher; acquiring a shared kernel lease writes no SDK file. The publisher
provisions those files before activation. A sealed installation can therefore
run metadata and build commands without cache write permission. Automatic
collection requires a writable installation and is deferred when that access
is unavailable. The launcher requires Python 3 in addition to Node; source builds
already use Python. Released npm packages do not load these local helpers.

Global command/package links point to the stable installation. Initial migration
uses atomic link replacement, with the launcher updated before compatibility
package aliases. Later publications do not need to recreate the live npm command.
The external user configuration selected by `ORO_ENV_FILE`/`ORO_CONFIG_HOME`
remains independent. Legacy SDK configuration is copied to private writable
`configuration/sdk.env` when present; publication never overwrites an existing
user configuration with a staging copy. Explicit `ORO_HOME` and `ORO_HOME_API`
remain overrides; the automatic pair guarantee applies to default selection.

To publish an existing complete staged pair without rebuilding:

```sh
PREFIX=/your/install/prefix pnpm relink --no-rebuild
```

For an independently staged SDK/API pair, prepare and inspect it first:

```sh
python3 bin/publish-runtime-sdk.py --sdk /path/to/runtime-linux-x64 --api /path/to/runtime
```

Add `--activate-local` to switch the checkout's stable generation. Add
`--global-prefix /your/install/prefix` to migrate its command and package links.
If a generation has already been prepared and activated, migrate only its links:

```sh
python3 bin/publish-runtime-sdk.py --link-current --global-prefix /your/install/prefix
```

Global migration needs write access to that prefix. A sandbox that permits only
checkout writes can prepare and qualify the generation and a private prefix;
it cannot update `/usr/local`. The command prints generation paths, receipt
hashes and retained history for review. After one successful migration,
removing or rebuilding disposable output does not remove the active command.
This path is currently qualified for Linux; other host publication paths retain
their existing behavior and need their own acceptance.

### Automatic source SDK retention

The local publisher keeps the active SDK and one previous SDK for rollback.
An identical payload reuses its content-addressed generation. Generations in
use, explicitly pinned generations and the latest prepared candidate are also
retained. A successful publication collects older unused generations; command
exit checks for collection at most once every five minutes. No background
service is required. Cleanup resumes on later use after an interruption.

New Linux `--link` builds register private staging directories and hold kernel
leases while they run. Successful and failed builds collect their own staging;
abandoned staging is collected after its last process releases the lease.
Collection validates directory identity and ownership metadata, detaches the
owned directory atomically, and removes it without following payload links.
An interrupted deletion remains registered for retry. Unmarked staging,
normal build outputs, CI artifacts and external user configuration are excluded.
The small previous global-link records are retained for migration history.

Before allocating staging or copying another SDK, the installer checks a
20 GiB local cache budget and requires 1 GiB of free disk space beyond the
estimated allocation. Set `ORO_LOCAL_SDK_MAX_BYTES` to a positive byte count to
change that budget. The check counts retained generations, reserved staging and
pending deletion. It refuses another allocation when protected copies prevent
it fitting; it never removes an in-use SDK to satisfy a quota. This is an
allocation limit, not a filesystem quota: unexpectedly larger compiler output
and unrelated build/dependency caches can still consume additional space.

Inspect or trigger collection explicitly:

```sh
python3 bin/publish-runtime-sdk.py --gc --dry-run
python3 bin/publish-runtime-sdk.py --gc
```

Both accept `--stable-home /path/to/installation`. Collection reports removed
and retained entries and saves its latest result in `gc-status.json`. A dry run
may initialize small bookkeeping files but does not delete or change payloads.
Older generations installed before lease tracking remain protected until the
next reboot. If all commands using those earlier copies have stopped, use
`--gc --legacy-idle` (add `--dry-run` to preview) to confirm they can be collected
on this boot. That option does not override a live tracked lease.

Use the stable launcher for managed commands. Invoking a physical generation's
binary directly, closing inherited lease descriptors or importing its packages
outside that launcher bypasses automatic usage tracking. Pin such a generation
before using it, then unpin it when finished:

```sh
python3 bin/sdk-cache.py pin --home .local/share/oro-sdk/linux --generation sdk-IDENTITY
python3 bin/sdk-cache.py unpin --home .local/share/oro-sdk/linux --generation sdk-IDENTITY
```

The release pipeline uses pack-only invocations without `--link`. It neither
initializes this source installation nor runs its launcher or collector. The
Linux publication regression exercises real platform, API and Node adapter npm
tarballs through that path; OS-specific release builds and registry publication
still require their usual CI jobs.

The older `bin/activate-runtime-sdk.py` helper only exchanges a generated
`build/npm/linux` tree for staging inspection. It preserves its old tree and
configuration but does not move global links out of disposable output. Use the
stable publisher for active checkout installations. SDK publication does not
qualify production signing, device behavior or downstream application adoption.

## Build storage and cleanup

The source build separates disposable staging from caches that shorten later
builds. Inspect a cleanup category before applying it:

```sh
./bin/clean.sh --staging --dry-run
./bin/clean.sh --cache --dry-run
./bin/clean.sh --full --dry-run
```

- With no arguments, `bin/clean.sh` removes only generated `.oro.env` files.
  `--only-env` makes the same intent explicit and preserves compiled targets.
- `--staging` removes runtime archive staging and outputs from disabled Llama
  tool targets. These files are not incremental inputs, so this mode does not
  make the next library build cold.
- `--cache` removes the Iroh and cr-sqlite Cargo targets, downloaded Rust
  toolchains stored under `build/`, and Go build/module caches. Use it when disk
  recovery is more important than the next build's compile and download time.
  Compiler caches such as `CCACHE_DIR` and package-manager caches are retained.
- `--full` removes `build/`, the Iroh Cargo target, and repository Go caches.
  Source and dependency intermediates start cold, while an external compiler
  cache may still accelerate the rebuild.
- `--dry-run` reports existing targets and their current sizes without deleting
  them.

Android CI sets `ORO_PRUNE_TRANSIENT_BUILD_OUTPUTS=true` after deciding that the
job will not perform another source build. This removes the pinned cr-sqlite
Rust toolchain and Cargo target after both ABI extensions are staged. Normal
developer builds leave this setting unset and retain those caches.

Like the mobile exclusion controls, `DEBUG` and `VERBOSE` are interpreted by
their presence rather than by parsing boolean words. Production release builds
must leave both unset.

Run `./bin/install.sh --help` for the concise command-line form of this contract.
When an installed `oroc` is available, `oroc env` reports `NO_ANDROID` and
`NO_IOS` when they are present, but the variables still control source-runtime
bootstrap rather than `oroc build --platform`.
