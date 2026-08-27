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
the emulator and exactly one Google APIs system image matching the host
architecture when its versioned AVD is absent. Likewise, an application build
for `android-emulator` requests one host-compatible image, while an `android`
device build does not.

## Related source-build controls

- `DEBUG=<non-empty>` enables debug runtime artifacts. On Windows it also
  selects the separate debug library layout.
- `VERBOSE=<non-empty>` prints additional source-build diagnostics.
- `CPU_CORES=<count>` overrides detected build parallelism.
- `ORO_HOME=<path>` chooses the staged runtime home.
- `PREFIX=<path>` chooses the installation/link prefix.

Like the mobile exclusion controls, `DEBUG` and `VERBOSE` are interpreted by
their presence rather than by parsing boolean words. Production release builds
must leave both unset.

Run `./bin/install.sh --help` for the concise command-line form of this contract.
When an installed `oroc` is available, `oroc env` reports `NO_ANDROID` and
`NO_IOS` when they are present, but the variables still control source-runtime
bootstrap rather than `oroc build --platform`.
