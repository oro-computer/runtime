# Oro Language Server

`oroc language-server` wraps installed language servers over stdio. **Use the
directory containing `oro.toml` as the editor's project root.** This gives every
editor the same configuration, working directory and program selection.
Oro exports compilation models for Kotlin Android, C-family native sources,
TypeScript/JavaScript and Swift. Models are separate from the backend so other
editors and servers can consume them.

## Language programs and project configuration

`--language` is required. There is no default language, and the wrapper does not
infer one from the project. Omitting it reports a usage error before loading the
project or starting build tools. `oroc language-server --help` remains available
without a language selection.

Choose one language or language family per server process:

```sh
oroc language-server --language kotlin
oroc language-server --language typescript
oroc language-server --language cpp
oroc language-server --language objc
oroc language-server --language swift
```

An editor can run several of these processes together for the same project, such
as Kotlin, TypeScript and C++. Each process wraps one backend; the wrapper does
not multiplex unrelated backends over a single LSP connection. The Coc example
below configures separate processes with their corresponding filetypes.

The defaults assume these programs are installed and available on the editor's
`PATH`. Oro does not download or install servers automatically.

| Language selection | Default executable | Default arguments | Shared configuration |
| --- | --- | --- | --- |
| `kotlin`, `kt` | `kotlin-language-server` | none | `language_server.kotlin` |
| `typescript`, `javascript`, `typescriptreact`, `javascriptreact`, `web`, `ts`, `js`, `tsx`, `jsx` | `typescript-language-server` | `--stdio` | `language_server.typescript` |
| `c`, `cpp`, `c++`, `objc`, `objcpp`, `objective-c`, `objective-c++` | `clangd` | none | `language_server.cpp` |
| `swift` | `sourcekit-lsp` | none | `language_server.swift` |

These defaults follow the upstream launch conventions for
[TypeScript/JavaScript](https://github.com/typescript-language-server/typescript-language-server),
[clangd](https://clangd.llvm.org/installation) and
[SourceKit-LSP](https://github.com/swiftlang/sourcekit-lsp).
Install TypeScript itself alongside `typescript-language-server` as its upstream
installation instructions require. The default adapters are `fwcd`, `typescript`,
`clangd` and `sourcekit`, respectively. They export the selected Oro model before
launching the server. Set `adapter = "stdio"` to retain a backend's own project
discovery, for example SourceKit-LSP's native SwiftPM support.

Override only what your application needs in `oro.toml`:

```toml
[language_server.kotlin]
command = "kotlin-language-server"
args = []
adapter = "fwcd"

[language_server.typescript]
command = "typescript-language-server"
args = ["--stdio"]

[language_server.cpp]
command = "clangd"
args = ["--background-index"]

[language_server.objc]
command = "clangd"
args = []

[language_server.swift]
command = "sourcekit-lsp"
args = []
```

The C-family defaults apply to C, C++, Objective-C and Objective-C++; a specific
`language_server.objc`, `objcpp` or `c` table overrides the shared `cpp` table.
JavaScript/React tables similarly override the `typescript` table. Use canonical
language names for table names; short aliases are for the command line.

`command` is one executable, not a shell command. A bare name is found on `PATH`;
a relative path such as `./tools/my-server` is resolved relative to `oro.toml`.
`args` is a TOML array of strings, passed literally in order. Spaces, empty
arguments and literal dollar signs are preserved; there is no shell expansion.
Providing `args` replaces the default array, so keep `--stdio` if your server
requires it. On Windows, `.cmd`/`.bat` launchers reject arguments containing
shell expansion characters instead of silently altering their meaning; native
executables do not have that batch-file restriction.

Selection precedence is `--server`, then the language-specific table, then the
family table, then the built-in default. Kotlin also accepts
`ORO_KOTLIN_LANGUAGE_SERVER` as an override below `--server`. Arguments still come
from the selected configuration when the executable is overridden.

Any other installed stdio server can be registered without modifying Oro:

```toml
[language_server.rust]
command = "rust-analyzer"
args = []
```

Then run `oroc language-server --language rust`. The generic `stdio` adapter is
the default for custom languages. To wrap a different Kotlin implementation,
set `language_server.kotlin.adapter = "stdio"` and configure its command/args.
That server owns Kotlin project discovery; the fwcd-specific classpath and
source-scope adaptation is not applied. Use `oroc tooling kotlin` separately to
export the Oro Android compilation model. Do not assume another server consumes
that model until it has an explicit adapter.

All languages require Node.js 22+. Kotlin's Android `fwcd` adapter additionally
requires the JDK/SDK/Gradle prerequisites below. Web, desktop native and generic
stdio launches do not query Gradle or require an Android SDK. `--prepare`,
`--refresh` and `--offline` are rejected for `stdio` adapters, since Oro does not
own their compilation models.

## Tooling commands for each platform

All paths below are relative to the directory containing `oro.toml`. Exported
models and helper files live under `[build] output` (default `build`), inside
`.oro-tooling/<language>/`. Commands print JSON; build output goes to stderr.
Neither application sources nor application compiler configuration is rewritten.

```sh
oroc tooling cpp --prepare
oroc tooling cpp --platform android --arch arm64 --prepare
oroc tooling cpp --platform ios-simulator --arch arm64 --prepare
oroc tooling typescript --prepare
oroc tooling swift --platform ios-simulator --arch arm64 --prepare
oroc tooling doctor
oroc tooling doctor --language cpp --platform android --arch arm64 --json
```

`--platform desktop` selects the host. Native architectures default to the host,
except Android and iOS devices default to `arm64`; select an emulator/simulator
architecture explicitly when it differs. `--prod` selects release flags. Use the
same target options on `language-server` as on `tooling`. There is still no
default **language**. A single process wraps one language family.

### C, C++, Objective-C and Objective-C++

The default provider reads native extension declarations and compiler flags from
Oro configuration, including platform and extension-local settings. Android also
reads `android.native_sources`. It uses installed SDK headers, Linux `pkg-config`
flags, the selected Android NDK, or `xcrun`'s Apple SDK and target. Apple tooling
requires macOS and Xcode. Android NDK selection uses `tooling.cpp.ndk`, then
`ANDROID_NDK_HOME`/`ANDROID_NDK_ROOT`, then the NDK version exported by the selected
Oro SDK metadata. It never chooses an arbitrary installed NDK.

```toml
[build]
name = "example"
output = "out"

[build.extensions]
bridge = "native/bridge.cc native/helper.c"
bridge_compiler_flags = "-I native/include -DFEATURE_ENABLED=1"

[tooling.cpp]
platform = "desktop"
```

The result includes `compileCommands`, `commands`, `originalSources`, target
identity and toolchain details. Clangd receives the generated database directory
automatically. Do not add `--compile-commands-dir` to its args with the `clangd`
adapter. File paths remain original, so navigation and edits reach application
files. Headers are resolved from the compiler's include paths. Unlisted source
snapshots and copied application sources are not compilation units.

For a custom build, use its actual database instead of duplicating flags:

```toml
[tooling.cpp]
compilation_database = "native-out/compile_commands.json"
# Optional allowlist of original translation units from that database:
sources = ["native/bridge.cc", "native/helper.c"]
```

Or let Oro configure CMake in the isolated editor generation:

```toml
[tooling.cpp]
cmake = "native"
configure_args = ["-G", "Ninja", "-DENABLE_EDITOR_HEADERS=ON"]
prepare_targets = ["generated_headers"]
# Cross targets require the project's actual toolchain file:
# toolchain = "cmake/toolchains/android.cmake"
```

`--prepare` builds only named `prepare_targets`; there is no implicit application
build. Existing database providers do not replay compiler commands. Native
extension build/configure scripts cannot be inferred and produce a recovery
message directing you to a database or CMake provider. Generate custom headers
with their real producer. The declaration provider does not execute shell
substitutions or arbitrary application build scripts.

CMake `--offline` disables FetchContent fetching/updating. Project-authored CMake
commands and preparation targets remain responsible for avoiding their own
network calls. Missing dependencies must be installed first. The declaration
and database providers perform no downloads.

### TypeScript and JavaScript

TypeScript 5+ must be installed in the application or discoverable through
`tsserver` on the editor's PATH. Oro selects `tsconfig.json`, then `jsconfig.json`,
and follows their `extends` and project references. It keeps compiler options and
original file scope. Without either config it uses `tooling.typescript.sources`,
or the source side of `build.copy` mappings, excluding the build output and
`node_modules`.

When none of these declarations exists, the adapter starts with TypeScript's
inferred projects: opened JS/JSX/TS/TSX documents and their imports determine the
source scope. This supports projects that bundle through `build.script` without
requiring an extra compiler config just to start the editor. Oro still supplies
its SDK declarations and keeps original file URIs. It does not scan the whole
repository, execute the build script, or treat asset/dependency copy maps as an
application source root. Unopened historical snapshots and generated copies are
not added as root files; opening or importing one explicitly can include it.

Inferred projects use the backend's compiler defaults. Add a `jsconfig.json` or
`tsconfig.json` when the application needs explicit JSX, alias, type-checking or
other compiler settings; arbitrary build-script semantics cannot be inferred.
An invalid explicit config or missing declared source still produces an error.

```toml
[build]
copy = "web"
output = "out"

[tooling.typescript]
# Optional; normal root tsconfig.json/jsconfig.json discovery needs no setting.
tsconfig = "web/tsconfig.json"
# If no tsconfig/jsconfig exists, explicit original paths may be used:
# sources = ["web", "shared/client.ts"]
# Optional installed TypeScript package directory:
# typescript = "node_modules/typescript"
```

The model names the selected SDK's `typings`, installed `compiler`,
`originalSources`, generated `editorConfig`, and `sourceMode` (`configured`,
`declared` or `inferred`). In inferred mode `originalSources` is empty; the
backend receives opened documents and the exported static model does not inventory
the backend's live projects. Its `editorConfig` contains the SDK declarations
only. Preparation materializes that
config and an Oro-owned tsserver plugin; it does not install packages, emit app
JavaScript or regenerate SDK declarations. Missing SDK declarations require a
complete SDK installation.

The default LSP adapter loads this plugin, which adds the SDK's ambient `oro:*`
declarations to the original TypeScript project. Imports such as `oro:crypto`
resolve without application `paths` lists or edits to `tsconfig.json`. Existing
compiler options and other plugins remain active. `--offline` also disables
tsserver automatic typings acquisition. A generic `stdio` backend must consume
the exported model itself if it needs these declarations.

### Swift

For a declared Swift module, specify its original files and installed dependency
search paths. No application source is copied:

```toml
[tooling.swift]
platform = "ios-simulator"
arch = "arm64"
module = "ApplicationBridge"
sources = ["apple/Bridge.swift", "apple/Models.swift"]
deployment_target = "15.0"
# Optional, only when the application uses these dependencies:
# bridging_header = "apple/Bridge.h"
# module_paths = ["dependencies/Modules"]
# framework_paths = ["dependencies/Frameworks"]
# compiler_flags = ["-D", "FEATURE_ENABLED"]
```

Oro discovers the actual compiler, SDK path and target triple. `--prepare` invokes
`swiftc -emit-module` into the editor generation, without linking an executable,
signing, installation or package downloads. The model exports `moduleFile`,
`compileCommands`, `toolchain` and `originalSources`. Dependency modules and
frameworks must already be prepared for that target. Compiler errors retain the
original paths and report incompatible or missing modules.

For an Xcode/custom build, `tooling.swift.compilation_database` imports its
exported Swift commands. Its own build system must prepare dependency modules;
`--prepare` is rejected for external Swift databases because replaying build
commands could link or sign an app. Oro does not currently derive Swift commands
from `.xcodeproj` itself. For SwiftPM, use SourceKit-LSP's package model directly:

```toml
[language_server.swift]
adapter = "stdio"
```

The `sourcekit` adapter selects its exported compilation database through LSP
initialization options. A project's `.sourcekit-lsp/config.json` can override
SourceKit-LSP initialization settings; remove conflicting build-system/search-path
settings when using the Oro adapter.

### Doctor and freshness

`tooling doctor` inspects declared languages, or one explicit `--language`. It
reports the resolved project marker, SDK, server executable, required toolchain,
and cached model status. `--json` returns `{schemaVersion, projectRoot, ok, checks}`.
Errors produce exit status 1; absent/stale models are warnings. Doctor does not
run Gradle tasks, CMake configuration, compilation, installs or model writes.

Native and web models fingerprint configuration, exporter code, SDK/compiler
identity and build-provider inputs. TypeScript includes extended/referenced
configs and SDK declarations; Swift includes source contents for prepared
modules. Targets and debug/release modes have separate pointers. `--refresh`
allocates a new generation; existing sessions can finish with the old one.
Running the tooling command again refreshes inputs, and each LSP launch exports
its model. Active adapters watch configuration, SDK/model inputs and publication
pointers. A change prepares a fresh model and restarts the backend inside the
existing editor connection. Open buffers, unsaved text, document versions and
configuration are replayed. Source text edits continue through LSP.

For deeply nested custom CMake inputs, generated headers, or dependency artifacts
changed outside Oro, run the preparation command with `--refresh` and restart the
language client. This avoids assuming that arbitrary build scripts expose all
their inputs. Models contain absolute local paths and should not be committed.

## Project-root selection

Open the application directory containing `oro.toml` and set your editor's root
marker to `oro.toml`. `oro.ini` is supported for legacy projects; when both files
exist in one directory, `oro.toml` wins. Oro does not use `.git` or `package.json`
as a project marker.

The language-server wrapper waits for the editor's LSP `initialize` request
before loading project configuration or starting build tools. Root selection is:

1. An explicit `--config` selects that file.
2. An explicit project directory selects a marker directly in that directory.
3. Otherwise, Oro searches upward from the editor's `workspaceFolders`. If those
   folders do not resolve to an Oro project, or none are provided, it searches
   upward from `rootUri`, or legacy `rootPath` when `rootUri` is absent.
4. Only when the editor supplies none of those roots does Oro search upward from
   the process working directory. `oroc tooling kotlin` also uses this search.

This allows Coc to launch the process from a different working directory while
sending the application root during initialization. Coc can send an enclosing
repository in `workspaceFolders` alongside a nested application in `rootUri`;
the wrapper uses that application when the repository search finds no project.
Initial protocol messages are retained until the selected backend starts.
If the editor supplies roots
that do not resolve to an Oro project, the wrapper reports those starting paths
and its launch directory, with instructions to select `--config`; it does not
silently choose another project from the working directory. Multiple distinct
Oro projects in the workspace require an explicit selection. Searches walk
toward parents, never into sibling or child applications.

For monorepos, run one server process per Oro application and language family.
Pass the application directory as the final argument, or use
`--config ./apps/mobile/oro.toml` from the repository root. An explicit directory
must itself contain the project file. Do not use the enclosing repository as the
root merely because it contains Git metadata. The general wrapper sets LSP
`rootUri`, legacy `rootPath` and initial `workspaceFolders` to the selected Oro
root while keeping document URIs unchanged. The Kotlin adapter further scopes
its backend to the exact Android source files described below.

Configuration is reread when its file changes. Moving project roots or changing
backend capabilities requires restarting the language client. After changing environment variables,
restart the editor so its child process inherits the new values.

## Prepare and inspect an Android project

From the application root (`.`):

```sh
oroc tooling kotlin --prepare --variant devDebug
oroc tooling kotlin --offline --variant devDebug
oroc language-server --language kotlin --variant devDebug --offline
```

The tooling command prints JSON. `--classpath` prints the platform-separated
classpath instead. Build output goes to stderr. `--config ./oro.toml` selects an
explicit configuration; otherwise the command finds `oro.toml` or `oro.ini` by
walking up from the working directory. An explicit project directory selects
that directory without walking to a different project.

Install the Oro SDK's supported Gradle version, Android platform/build-tools
packages and a compatible JDK, and accept the Android SDK licenses first.
`JAVA_HOME`, `GRADLE_HOME` and `ANDROID_HOME` may come from the shell or Oro's
local environment configuration. `GRADLE_HOME` is optional when `gradle` is on
`PATH`. Use `oroc env --json` to inspect the effective environment locally; it
may include private configuration, so do not publish that output.

Preparation uses the normal Oro Android generator in an isolated directory
under `[build] output` (default `build/.oro-tooling/kotlin`). Custom output
directories are supported. It retains the application's resources, manifest
overlays, dependencies and generated runtime sources. The exact original
application files declared by `[android] sources` are exported for analysis and
excluded from this preparation compilation. The editor-only extension registry
is empty, so it does not instantiate application extension classes. It does not
scan the repository or Gradle caches to guess dependencies.

`--prepare` runs the selected variant's Kotlin and Java compile task providers
and their resource prerequisites for SDK classes only. Application JVM sources
may contain syntax or type errors; the language server diagnoses those original
files after startup. SDK sources and resources must still compile. Native
extension build scripts, C/C++ compilation, web build hooks, APK assembly,
signing and installation are not invoked. Android SDK installation and license
acceptance are explicit prerequisites, not actions performed by this command.

Every model export also runs the producers of its source and Kotlin library
file collections. This includes generated `BuildConfig` sources, Android resource
symbols and any JVM project dependencies needed for that compile classpath.
Gradle requires these dependencies before reading their mapped providers, even
when a previous invocation already generated the files; unchanged producers are
checked incrementally. Oro derives these tasks from the collections rather than
hardcoding task names or output paths. See Gradle's
[file collection producer dependencies](https://docs.gradle.org/current/javadoc/org/gradle/api/file/FileCollection.html).

`--offline` is passed to Gradle. It uses installed SDK packages, plugins and
cached resolved dependencies. The first preparation needs network access if
those artifacts are not installed. Omitting `--prepare` exports the model and
reports missing SDK outputs without compiling the runtime Kotlin/Java tasks.
Generated-source, resource and dependency producers still run as described above.
Kotlin language-server startup automatically prepares missing SDK/resource classes;
`--prepare` remains available for explicit preparation outside an editor.

## Source scope and identity

Declare every production application source in `[android] sources`, using the
same file list as the application build. The adapter indexes only those files.
Historical snapshots, fixtures and generated application copies are excluded,
even when they sit beside a production file. No application source is rewritten
and no application classpath hook is installed.

The pinned fwcd adapter internally supplies an empty working directory followed
by exact file roots to its source scanner. This is an adaptation of that server's
implementation, not an LSP workspace configuration editors must reproduce.
Original canonical file URIs remain unchanged in diagnostics, navigation,
rename and other edits. The runtime and generated resource classes are resolved
from compile task outputs. Declared Java files are available as sibling sources;
this is not a Java language-server adapter.

Sources containing Oro template substitutions cannot currently preserve an
identity mapping and are rejected with the filename. Use a future adapter with
source maps for such projects. This limitation does not require changes to
ordinary Kotlin application sources.

## Coc configuration

Install Node.js 22+ and the
[fwcd 1.3.13 distribution](https://github.com/fwcd/kotlin-language-server/releases/tag/1.3.13).
Put its `bin` directory on the editor's `PATH`, or set
`ORO_KOTLIN_LANGUAGE_SERVER` to its launcher. `--server` can also select the
launcher. Oro validates the fwcd distribution and invokes the selected launcher
with the configured arguments. The server binary is installed separately; the adapter
and classpath hook are shipped with Oro.

The editor must inherit `JAVA_HOME` pointing to JDK 21 or later and the SDK/Gradle
environment used for preparation. GUI editors may not inherit an interactive
shell's environment. Launch the editor from that shell or configure its user
environment. Keep machine-specific paths out of committed project settings.
`ORO_NODE_EXECUTABLE` selects Node when it is not on the CLI's `PATH`.

Add this to Coc's language-server configuration:

```json
{
  "languageserver": {
    "oro-kotlin": {
      "command": "oroc",
      "args": [
        "language-server",
        "--language", "kotlin",
        "--variant", "devDebug",
        "--offline"
      ],
      "filetypes": ["kotlin"],
      "rootPatterns": ["oro.toml", "oro.ini"],
      "requireRootPattern": true
    },
    "oro-web": {
      "command": "oroc",
      "args": ["language-server", "--language", "typescript"],
      "filetypes": ["javascript", "javascriptreact", "typescript", "typescriptreact"],
      "rootPatterns": ["oro.toml", "oro.ini"],
      "requireRootPattern": true
    },
    "oro-native": {
      "command": "oroc",
      "args": ["language-server", "--language", "cpp"],
      "filetypes": ["c", "cpp", "objc", "objcpp"],
      "rootPatterns": ["oro.toml", "oro.ini"],
      "requireRootPattern": true
    },
    "oro-swift": {
      "command": "oroc",
      "args": ["language-server", "--language", "swift"],
      "filetypes": ["swift"],
      "rootPatterns": ["oro.toml", "oro.ini"],
      "requireRootPattern": true
    }
  }
}
```

Keep only the entries for languages you use. If Objective-C has its own
`language_server.objc` settings, give it a separate Coc entry with
`--language objc` and remove `objc` from the shared native entry. Apply the same
rule to any language-specific override; a process launched with `--language cpp`
uses the shared C-family settings for every buffer it serves.

Prepare the selected language from the application root before starting Coc. The `rootPatterns`
setting lets Coc supply that application root in its initialization request;
the wrapper's process working directory does not have to match. Append the
project directory to `args` when a larger workspace contains several Oro
applications and the client cannot launch a separate process for each root.
Disable any second Kotlin server
for the same buffers. Also disable competing TypeScript/clangd providers for
buffers assigned to these entries. `:CocRestart` restarts after configuration,
preparation or target/variant changes. Kotlin prepares its SDK/resource classes on startup. Add `--prepare` for other
Oro model adapters when their configured preparation targets should run. Use the same `--platform`,
`--arch`, `--prod` or `--variant` options as the tooling command. Plain `stdio`
adapters do not accept preparation options.

If an older wrapper reports `No oro.toml or oro.ini in /`, the slash is the end
of its upward search, not necessarily Vim's working directory. Upgrade or rebuild
Oro to use initialization-based discovery. As a workaround with that older
wrapper, append `--config` and the absolute path to the application's `oro.toml`
to the server's `args` in your local Coc settings, then run `:CocRestart`.
For a layout such as `desktop/oro.toml` and `desktop/android/*.kt`, select
`desktop` as the root. Avoid `.git` or the enclosing repository as the root
marker for this entry. Keep machine-specific absolute paths in user settings.

The repository includes a VS Code client in `editors/vscode`. It starts one
process per nearest Oro project marker and language family, ignores ordinary
projects, and suppresses initialization retry loops. It requires a trusted workspace.
To run the client locally, install its dependencies with `npm ci --prefix editors/vscode`
and launch `code --extensionDevelopmentPath=./editors/vscode .` from the repository root.
For distribution, package that directory with the VS Code extension packaging tool.
Use the command **Oro: Restart Language Servers** after changing the editor environment.

`oro.languageServer.executable`, `oro.languageServer.environment`,
`oro.languageServer.languages` and per-family `oro.languageServer.arguments`
configure the client. Disable competing providers for these language families
in Oro workspaces. On Windows, set the executable to the native `oroc.exe` when
your `PATH` only exposes an npm batch launcher. Keep that path in user settings.
Other configurable LSP clients use the same executable and arguments. One process serves one
Oro application and language family (and one Android variant for Kotlin).
In a configurable VS Code client, use the following launch settings:

| Client setting | Value |
| --- | --- |
| Executable | `oroc` |
| Arguments for web | `language-server`, `--language`, `typescript` |
| Arguments for C family | `language-server`, `--language`, `cpp` |
| Arguments for Kotlin | `language-server`, `--language`, `kotlin`, `--variant`, `devDebug`, `--offline` |
| Transport | stdio |
| Root marker | `oro.toml` (optionally `oro.ini` for legacy projects) |
| Working directory | Directory containing the selected project file |

If the VS Code client does not support custom root detection, open the Oro
application as its own workspace folder and append `--config` plus the selected
`oro.toml` path to the argument list. Changing settings in `oro.toml` configures
Oro's backend; it does not itself install or activate an editor extension.
Other clients, including Neovim's built-in LSP client and Emacs Eglot, use the
same launch configuration. Each client must provide the application root during
initialization or select it explicitly in the launch arguments.

## Freshness and compatibility

Every tooling invocation and language-server launch queries Gradle again.
Configuration, source declarations, resource contents, SDK/JDK selection, variant,
exporter script contents and the Oro distribution fingerprint select a generation. Runtime upgrades
select a new generation. Gradle owns dependency resolution and incremental
compilation. No intermediate class-output directory is hardcoded.

```sh
oroc tooling kotlin --prepare --refresh --variant liveRelease --offline
```

`--refresh` creates a new generation without replacing an active editor's
project. Select the same variant in the editor launch arguments and restart.
During a session, changed configuration/resources/runtime inputs, a newly
published preparation, source-file creation/removal, or changed/missing classpath
artifacts trigger a supervised backend refresh. The editor connection stays open
and unsaved buffers are replayed. In-flight requests receive a retryable cancellation;
stale replies and diagnostics from the previous backend are discarded. Environment changes also require
restarting the editor process to inherit the new environment. Changes to ordinary
open source buffers travel over LSP and do not trigger a build.

| Failure | Recovery |
| --- | --- |
| Missing or ambiguous Oro project root | Set the editor root marker to `oro.toml`, or pass `--config` with the application configuration. Check the reported search paths and launch directory. |
| Missing Gradle/JDK/SDK | Install the versions supported by the Oro SDK; set `JAVA_HOME`, `GRADLE_HOME` and `ANDROID_HOME` locally. |
| Missing offline dependency | Run preparation once without `--offline` on a connected machine using the same installed dependency cache. |
| Missing generated classes/resources | Run `oroc tooling kotlin --prepare --variant <variant>`. |
| A mapped provider was queried before its producer completed | This indicates a dependency error in the exporter. Update/relink Oro, prepare with `--refresh`, and restart the editor. Report the full Gradle error if it persists; do not edit generated Gradle projects. |
| Unknown variant | Select a generated Android variant, such as `devDebug` or `liveRelease`. |
| Wrong server distribution | Select fwcd 1.3.13's launcher or install an Oro adapter supporting the desired backend. |
| Unsupported Kotlin metadata | The error names the artifact/class and metadata version. Upgrade to a compatible adapter; do not suppress compiler metadata checks. |
| File outside declared sources | Correct `[android] sources`, prepare and restart. |

The fwcd server is [deprecated upstream](https://github.com/fwcd/kotlin-language-server).
Its 1.3.13 distribution uses Kotlin compiler 2.1.0. Oro checks actual class
annotations in the resolved JARs and output directories before launch, including
Kotlin's strict metadata flag. This compiler accepts metadata through 2.2 (2.1
with strict semantics), and JVM targets through 23. Metadata compatibility is
not a guarantee that newer Kotlin source syntax or compiler plugins are supported.
The exported model retains compiler options for future adapters; fwcd exposes
only a JVM-target setting, so compiler plugins and additional analysis options
are not supported by this adapter.

Generated editor projects and session caches are local build artifacts. They
can be removed with the application's build output when no editor is using them.
Preparation is serialized per project/language using a loopback socket held by
the process, so process death releases the lock. Port collisions serialize unrelated
preparations safely. New generations carry ownership markers and active-session
leases. Oro retains eight inactive marked generations, preserves current pointers
and live sessions, and refuses new preparation at 64 protected generations.
Abandoned session directories are reclaimed after their process exits. Older
unmarked directories are left intact.

## Model format and extension points

Schema version 1 exports these editor-independent fields:

| Field | Meaning |
| --- | --- |
| `language`, `platform`, `projectRoot`, `configuration`, `variant` | Selected compilation identity. |
| `originalSources` | Canonical application file paths from Oro configuration. |
| `generatedSources` | Selected compile-task sources other than original application files. |
| `compileClasspath` | Selected Kotlin compile task's libraries, including SDK, AndroidX, resource symbols and resolved dependencies. Kotlin/Java compilation outputs are separate in `runtimeClasses`. |
| `runtimeClasses` | Selected Kotlin/Java task destination directories, including runtime classes. |
| `compiler` | Effective JVM target, language/API versions, free compiler arguments and Java source/target. |
| `jdk` | Gradle JVM home/version and the Java compile task's JDK home/version requirement. |
| `android`, `gradleVersion`, `tasks` | Selected SDK and compile task identities. |
| `runtime`, `fingerprint` | Oro distribution evidence and generation fingerprint. |
| `prepared`, `missingArtifacts` | Whether compilation was requested and which artifacts are absent. |

`generation`, `modelFile`, `pointer` and `freshnessInputs` are local adapter
bookkeeping, not portable project configuration. Models contain absolute paths
and must be regenerated after moving a project or changing machines.

Add another language by providing its compilation-model producer and stdio
adapter behind the common command. Consumers must check `schemaVersion` and
explicitly reject unsupported models. They should preserve original file URIs
and reserve stdout for the selected machine-readable protocol.

## Acceptance qualification

The checked-in protocol regression tests cover fragmented UTF-8 frames, malformed
lengths, incomplete messages and preservation of input during startup. Root
discovery cases cover editor roots that differ from cwd, explicit overrides,
legacy markers, encoded paths and ambiguous workspaces.
`test/scripts/test-language-server.js` exercises
the real installed CLI, Gradle and Kotlin LSP backend against a small project
with duplicate historical/fixture sources. It checks zero error diagnostics,
Android/AndroidX/Oro/sibling hover and sibling navigation, default and custom
output directories, offline preparation and variant changes. It launches from
an unrelated directory and checks both initialization-based root discovery and
an explicit project argument. Before preparation, it exports a fresh model and
checks that classpath producers have run while runtime compilation outputs
remain absent. After preparation, it exports again without `--prepare` in a new
Gradle invocation to cover producer ordering for already generated artifacts.

After rebuilding/providing the packaged CLI and dependencies, run from the
repository root:

```sh
node test/scripts/test-language-server.js
node test/scripts/test-language-tooling.js typescript
node test/scripts/test-language-tooling.js typescript --inferred
node test/scripts/test-language-tooling.js cpp
```

The web/native qualification requires their installed backend programs and SDKs.
It uses a custom output path containing spaces, excludes a historical source
copy, sends initialization immediately from an unrelated working directory,
checks that an introduced diagnostic clears after an edit, and resolves an Oro
symbol through hover. TypeScript is taken from this repository's development
dependencies for the qualification fixture. It never changes an application.
Unit coverage in `test/unit/language-server.test.js` also covers model refresh
identity, compiler-config inheritance, literal database arguments, external Swift
database preservation, doctor inspection and npm startup stdin ownership.

The Android fixture also changes a private copy of the SDK, verifies a new
fingerprint/generation and repeats its LSP checks. A deliberately uncached dependency
checks offline recovery instructions. `test/scripts/test-kotlin-compatibility.js`
uses real JVM classfiles to verify metadata limits and checks missing-SDK recovery.
Set `ORO_LANGUAGE_TEST_OLD_JAVA_HOME` to an installed JDK below 21 to exercise Java
rejection; CI also recognizes `JAVA_HOME_17_X64`. Retain results for each installed backend and OS; exporting a model does not
establish diagnostics, navigation or editing behavior. The lifecycle workflow
runs on Linux, macOS and Windows with Node 22 and 24. The npm release workflow
sets `ORO_REQUIRE_EDITOR_QUALIFICATION=1` and runs real LSP checks against the
installed package before uploading it. Missing required backends fail that gate.
Apple runners additionally qualify Objective-C, Objective-C++ and Swift. The
Android SDK package qualifies Kotlin against Gradle and kotlin-language-server.
Unsupported backend features are recorded explicitly and are not counted as passes.

## Startup and recovery

`--startup-timeout` sets the total preparation/initialization deadline in milliseconds
(default 300000, range 1000–1800000). The wrapper reports preparation through the
initialize request's work-done token when provided, otherwise through LSP log messages.
Toolchain/server checks precede Gradle work. Initialization failures return a structured
error with `retry: false`, so clients can show the recovery message without a crash loop.
Editor disconnect, shutdown and cancellation terminate preparation and backend process
trees, including Windows launchers. Runtime crashes get at most three retries per minute.
Editor Gradle invocations use a single-use Gradle process and compile Kotlin in that
process, avoiding a separate persistent Kotlin daemon for editor preparation.
For fwcd 1.3.13, Oro also waits for dependency indexing to finish before completing
initialization. This prevents the backend's indexer and hover analyzer from
deadlocking on Kotlin compiler locks. Indexing remains enabled, and its progress
is included in the startup deadline.

During refresh, Oro retains up to 64 MiB of open text, with a 16 MiB limit per document
and protocol message. It preserves UTF-16 incremental edits and document versions.
Responses for an older document version are cancelled. Rename, code-action and apply-edit
results are checked against original source scope before reaching the editor; navigation
may still target dependency sources. Dynamic backend capability registration is disabled;
a change in static capabilities requires restarting the client.

## Alternative Kotlin backend

`kotlin-language-server` remains the default, using the pinned fwcd 1.3.13 distribution.
The alternative `kotlin-lsp` adapter exports a separate `workspace.json` using the
[upstream JSON importer schema](https://github.com/Kotlin/kotlin-lsp/blob/main/workspace-import/src/com/jetbrains/ls/imports/json/model.kt).
It uses original source directories with explicit exclusions for undeclared siblings,
the Gradle classpath and the selected JDK. It creates no application Gradle files.

```toml
[language_server.kotlin]
command = "kotlin-lsp"
args = ["--stdio"]
adapter = "kotlin-lsp"
```

Select the installed launcher name/path when its distribution uses a different name.
The [upstream project](https://github.com/Kotlin/kotlin-lsp) is alpha; this adapter's
JSON exporter is covered by regression checks, but a given backend distribution must
pass real LSP qualification before it is treated as supported. Some newer distributions
require the user to accept their own terms before launch. Oro does not accept those
terms, supply a license or configure telemetry on the user's behalf. Keep any required
launcher arguments in local configuration. `adapter = "stdio"` remains available when
a backend should own all project discovery.

## Coexistence with ordinary projects in Coc

Coc does not select a server by configuration order. Keep `requireRootPattern: true`
on the Oro entries and use only `oro.toml`/`oro.ini` as their root patterns. A separate
ordinary Kotlin entry can require `settings.gradle`, `settings.gradle.kts` or `pom.xml`.
That entry serves those build-system projects while the Oro entry serves Oro projects.
If a project matches both sets of markers, disable its competing provider explicitly;
there is no implicit last-entry-wins rule. `:CocList services` shows which clients started.

## Supported combinations and qualification

| Adapter | Expected program/toolchain | Qualification |
| --- | --- | --- |
| fwcd | kotlin-language-server 1.3.13, compiler 2.1.0, JDK 21+ | Real Android fixture, including invalid application sources, variants and offline reuse |
| kotlin-lsp | Backend with the upstream JSON workspace importer | Exporter checked; backend acceptance required for each distribution |
| typescript | TypeScript 5+, typescript-language-server; CI pins 5.9.3 and 6.0.2 | Configured and inferred projects; original-file editing |
| clangd | Installed native clangd; CI pins 19.1.2 on x64 and uses native packages on ARM64 | C/C++, plus Objective-C families on Apple runners |
| sourcekit | SourceKit-LSP and matching installed Swift/Xcode toolchain | Swift on Apple runners; external databases retain their build-system prerequisites |

The generic qualification script also accepts `c`, `objc`, `objcpp` and `swift`.
It introduces an unsaved error, observes and clears diagnostics, checks hover and
exercises advertised completion, references, rename, formatting and code actions.
Run it on the target OS; a Linux result does not qualify Apple or Windows SDK behavior.

Editing qualification applies returned edits to unsaved original buffers. Rename
must update the fixture's declaration and all uses without unrelated changes.
Formatting must change deliberately misformatted code, preserve its tokens and
be stable on a second application. Quick fixes must produce a matching action,
apply its edit (resolving the action first when required), and clear the diagnostic.
The fixtures cover a missing Kotlin import, a TypeScript spelling error and a
missing C-family semicolon. Empty edits do not pass. Each resulting buffer is
analyzed, and the on-disk originals must remain unchanged. The test client
introduces a temporary error before checking a valid result because servers may
suppress repeated empty diagnostic notifications.

The VS Code client uses the published `vscode-languageclient` interfaces and is
checked against the minimum supported VS Code API declarations. Run
`npm ci --prefix editors/vscode` and `npm run check --prefix editors/vscode` to
perform that check without installing VS Code. Lifecycle and interface checks
run in the Linux/macOS/Windows workflow; actual extension-host behavior remains
a separate qualification. Local `act` Linux containers exercise the Linux job.
Windows and Apple SDK behavior requires the corresponding native runner, either
in GitHub Actions or on a machine running that OS.

Long-running performance and large-project profiling remain open follow-up work.
Downstream reports should include the host OS, Oro/backend versions, language,
source/dependency counts, and whether the delay occurred during startup, refresh
or editing, along with relevant logs stripped of private information.
