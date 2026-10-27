# Command Line Interface

These commands are available from the command line interface (CLI).

## oroc

Web docs: https://oro.computer/runtime/docs/?p=cli%2Foroc

Build, run, and manage Oro applications.

### Usage

```bash
oroc <command> [options]
oroc help [query...]
```

### Commands

| Command | Description |
| --- | --- |
| `build` | build project |
| `config` | inspect configuration values and docs |
| `env` | print relevant environment variables |
| `help` | search commands, options, and platform notes |
| `init` | create a new project (in the current directory) |
| `install-app` | install app to the device |
| `install` | alias for install-app |
| `language-server` | run Oro Language Server over stdio |
| `list-devices` | get the list of connected devices |
| `devices` | alias for list-devices |
| `mcp` | run an MCP server for agent tooling |
| `print-build-dir` | print build path to stdout |
| `push` | run a local UnifiedPush development server |
| `run` | run application |
| `setup` | install build dependencies |
| `tooling` | export and prepare editor compilation models |
| `update` | update tooling (keys, manifests, bundles) |
| `version` | inspect or bump project version |
| `versions` | print runtime and dependency versions |

### General options

| Option | Description |
| --- | --- |
| `-h, --help` | show help |
| `--prefix` | print install path |
| `-v, --version` | print program version |
| `-q, --quiet` | hint for less log output |
| `-V, --verbose` | enable verbose output (can be global) |
| `-D, --debug` | enable debug output (can be global) |
| `--no-color` | disable colored log output |
| `--json` | enable structured JSON logs on stdout |
| `--log-file=<path>` | mirror logs to a JSON file |

### Environment

| Variable | Description |
| --- | --- |
| `ORO_DEBUG` | enable debug mode (like -D) |
| `ORO_VERBOSE` | enable verbose logs (like -V) |
| `ORO_LOG_NO_COLOR` | disable colored log output |
| `ORO_LOG_JSON` | enable structured JSON logs on stdout |
| `ORO_LOG_FILE` | mirror logs to a JSON file |
| `ORO_ALLOW_EXEC` | allow native extension commands during builds |
| `ORO_ENABLE_SANITIZERS` | enable ASan/UBSan on desktop builds |
| `NO_ANDROID` | source bootstrap: disable only Android work when non-empty |
| `NO_IOS` | source bootstrap: disable only iOS/iOS Simulator work when non-empty |

### Notes

Run 'oroc <command> --help' for usage and options; see the command's man page for examples and platform guidance.

Use 'oroc help <query>' to search commands, options, examples, and workflow notes by topic.
'oroc <subcommand> --json' emits structured command results when that subcommand supports it.

The global 'oroc --json <subcommand>' form is reserved for JSON log streaming.

Use '--log-file=<path>' when you need machine-readable stdout plus a separate log stream.

NO_ANDROID and NO_IOS are independent presence flags for building the runtime source tree;
they do not select an application target for 'oroc build --platform'. Values such as
0 and false are non-empty and still disable the named target.

### Examples

```bash
oroc init my-app
```

create a starter Oro project in ./my-app

```bash
oroc run .
```

run the app rooted at the current directory

```bash
oroc build --platform=ios --prod .
```

build a production iOS artifact from the current project

```bash
oroc config --describe meta.version
```

inspect documentation for a single configuration key

```bash
oroc help ios signing
```

search for commands and notes related to iOS signing and provisioning

### Update tooling

| Command | Description |
| --- | --- |
| `oroc update keygen` | generate an Ed25519 keypair for signing update manifests |
| `oroc update init` | scaffold a manifest JSON file |
| `oroc update server` | run an update server over HTTP/TCP/UDP |
| `oroc update info` | query update servers or static manifests over HTTP/TCP/UDP |
| `oroc update sign` | sign a manifest JSON file and emit a sidecar signature |
| `oroc update verify` | verify a manifest JSON file + signature using a public key |
| `oroc update bundle` | build a tar archive from a directory suitable as an update artifact |
| `oroc update extract` | extract an update bundle tar archive into a directory |
| `oroc update validate` | validate a manifest.json file against the update manifest schema shape |

## oroc language-server

Web docs: https://oro.computer/runtime/docs/?p=cli%2Flanguage-server

Oro Language Server: editor-independent build models and language adapters.

### Usage

```bash
oroc language-server --language <name> [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--language=<name>` | required: kotlin, typescript, cpp, c, objc, objcpp, swift, or configured language |
| `--variant=<name>` | Android variant (default devDebug) |
| `--server=<path>` | override the configured language server executable |
| `--prepare` | prepare native dependencies before launch (Kotlin prepares SDK classes automatically) |
| `--startup-timeout=<ms>` | preparation and startup deadline (300000) |
| `--refresh` | regenerate the selected editor model |
| `--offline` | use installed dependencies only |
| `--platform=<name>` | native target platform |
| `--arch=<name>` | native target architecture |
| `--prod` | release compiler flags for native models |
| `--config=<path>` | explicit oro.toml or oro.ini |
| `-h, --help` | show help |

### Notes

Language selection is required; there is no default language.

Run one wrapper process per language or language family to use several servers together.

Requires Node.js 22+ and the selected language server on PATH.

The directory containing oro.toml is the recommended project root (oro.ini is also supported).

Root discovery uses the editor's initialize workspace roots, falling back to cwd only if absent.

An explicit --config or project directory overrides editor root discovery.

Configure [language_server.<language>] command and args in oro.toml.

Defaults: kotlin-language-server; typescript-language-server --stdio; clangd; sourcekit-lsp.

The sourcekit adapter requires Swift 6.0+ (Xcode 16+ on macOS) for compiler settings, rename, and formatting support.

C-family language servers are unavailable on Windows in this release. TypeScript editing, tooling cpp model export, and Windows runtime builds remain available.

Kotlin Android uses the fwcd adapter; adapter="stdio" delegates discovery to another backend.

Stdout is reserved for LSP. Build and adapter messages go to stderr.

See docs/LANGUAGE_SERVER.md for Coc, VS Code, preparation and compatibility.

## oroc tooling

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling

Export and prepare editor compilation models.

### Usage

```bash
oroc tooling <command> [options]
```

### Commands

| Command | Description |
| --- | --- |
| `kotlin` | export or prepare a Kotlin model |
| `cpp` | export C-family compiler commands |
| `typescript` | export web sources and Oro SDK typings |
| `swift` | export commands and prepare Swift modules |
| `doctor` | inspect editor setup and model freshness |

### Options

| Option | Description |
| --- | --- |
| `-h, --help` | show help |

### Notes

Use 'oroc tooling <command> --help' for model export and preparation options.

Tooling models are JSON. Doctor defaults to readable text and also accepts --json.

## oroc tooling kotlin

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling%2Fkotlin

Export and prepare Oro editor compilation models.

### Usage

```bash
oroc tooling kotlin [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--prepare` | compile resources and JVM classes for analysis |
| `--variant=<name>` | Android variant (default devDebug) |
| `--refresh` | regenerate the Android editor project |
| `--offline` | resolve dependencies without network access |
| `--config=<path>` | explicit oro.toml or oro.ini |
| `--classpath` | print only the resolved editor classpath |
| `-h, --help` | show help |

### Notes

Prints the versioned JSON model by default. Uses Oro configuration and Gradle task providers.

Preparation does not assemble, sign, install, or compile native libraries.

Gradle, a compatible JDK, Android SDK packages and dependency artifacts must be installed.

## oroc tooling cpp

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling%2Fcpp

Export C, C++, Objective-C and Objective-C++ compiler commands.

### Usage

```bash
oroc tooling cpp [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<name>` | desktop, linux, mac, win, android, ios or ios-simulator |
| `--arch=<name>` | target architecture (host by default; arm64 for Android/iOS) |
| `--prod` | select release compiler flags |
| `--prepare` | build explicitly configured CMake preparation targets |
| `--refresh` | allocate a fresh editor generation |
| `--offline` | disable CMake FetchContent downloads and updates |
| `--config=<path>` | use an explicit oro.toml or oro.ini |
| `-h, --help` | show help |

### Notes

Uses Oro extension declarations, an existing compilation database, or a configured CMake project.

Generated commands target original source files and live under build.output/.oro-tooling.

Apple SDK discovery requires macOS and Xcode; Android requires the selected NDK.

Custom scripts and generated dependency headers require a build-system compilation database or CMake provider.

See docs/LANGUAGE_SERVER.md for configuration and editor examples.

## oroc tooling typescript

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling%2Ftypescript

Export web compiler context and selected Oro SDK declarations.

### Usage

```bash
oroc tooling typescript [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--prepare` | materialize the editor config and TypeScript plugin |
| `--refresh` | allocate a fresh editor generation |
| `--offline` | use installed packages; disable automatic typings acquisition in LSP |
| `--config=<path>` | use an explicit oro.toml or oro.ini |
| `-h, --help` | show help |

### Notes

Uses the original tsconfig.json/jsconfig.json, including references, or explicit source declarations.

Without a declared scope, tsserver infers projects from opened documents and their imports.

Does not rewrite application configs, emit JavaScript, or install packages.

The default TypeScript LSP adapter adds Oro declarations through an Oro-owned tsserver plugin.

See docs/LANGUAGE_SERVER.md for configuration and editor examples.

## oroc tooling swift

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling%2Fswift

Export Swift compiler commands and prepare analysis modules.

### Usage

```bash
oroc tooling swift [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<name>` | desktop, mac, ios, ios-simulator, linux or win |
| `--arch=<name>` | target architecture |
| `--prod` | select release compiler flags |
| `--prepare` | emit the declared Swift module without linking or signing |
| `--refresh` | allocate a fresh editor generation |
| `--offline` | use installed toolchains and dependencies only |
| `--config=<path>` | use an explicit oro.toml or oro.ini |
| `-h, --help` | show help |

### Notes

Declare original sources and module settings under tooling.swift, or import an existing compilation database.

Apple targets require macOS and Xcode. External build systems own their dependency preparation.

Use language_server.swift.adapter="stdio" for native SwiftPM discovery.

See docs/LANGUAGE_SERVER.md for configuration and editor examples.

## oroc tooling doctor

Web docs: https://oro.computer/runtime/docs/?p=cli%2Ftooling%2Fdoctor

Inspect editor setup without preparing dependencies.

### Usage

```bash
oroc tooling doctor [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--language=<name>` | inspect one language family; otherwise inspect declared languages |
| `--platform=<name>` | select the native target |
| `--arch=<name>` | select target architecture |
| `--variant=<name>` | select Android variant (default devDebug) |
| `--prod` | inspect release model |
| `--json` | emit the structured report instead of readable text |
| `--config=<path>` | use an explicit oro.toml or oro.ini |
| `-h, --help` | show help |

### Notes

Checks project root, selected SDK, server executables, required tools and cached model freshness.

Does not install servers, generate models, run Gradle tasks or invoke preparation targets.

Returns a nonzero status when a required check fails; stale or absent models are warnings.

See docs/LANGUAGE_SERVER.md for configuration and editor examples.

## oroc push

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fpush

Local UnifiedPush development tooling.

### Usage

```bash
oroc push <command> [options]
```

### Commands

| Command | Description |
| --- | --- |
| `server` | run a local ntfy server for an Android UnifiedPush distributor |
| `distributor` | build or install the Oro Kotlin development distributor APK |

### Options

| Option | Description |
| --- | --- |
| `-h, --help` | show help |

### Notes

Run 'oroc push server --help' for configuration and HTTPS requirements.

## oroc push server

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fpush%2Fserver

Run a local UnifiedPush development server backed by ntfy.

### Usage

```bash
oroc push server [options]
```

### Options

| Option | Description |
| --- | --- |
| `--host=<ip>` | listen address (default: 127.0.0.1) |
| `--port=<port>` | listen port (default: 8787) |
| `--base-url=<origin>` | URL reachable by the distributor and application server |
| `--data-dir=<path>` | writable cache, tools and message storage directory |
| `--cert-file=<path>` | PEM certificate for direct HTTPS (requires --key-file) |
| `--key-file=<path>` | PEM private key for direct HTTPS (requires --cert-file) |
| `--ntfy-bin=<path>` | use an explicitly supplied ntfy executable without download |
| `--offline` | require the verified cached ntfy release or --ntfy-bin |
| `-h, --help` | show help |

### Notes

Requires Node.js 22 or later. On first use, downloads ntfy 2.33.1 from its official
GitHub release and verifies a pinned SHA-256. Later starts use the verified cache.

Automatic installation supports Linux x64/arm64 and Windows x64. On macOS, use
--ntfy-bin with a server-enabled development build; upstream's macOS release is client-only.

State defaults to the user's Oro data directory, outside the installed SDK.

The server runs in the foreground; Ctrl-C stops it. Cached messages expire after 12 hours.

This is an anonymous development server. Use public test data and private development networks.

No FCM, upstream push relay or system ntfy configuration is used.

Use 'push distributor' to build Oro Push Dev, or install the Android ntfy distributor.

Configure the selected distributor with this base URL; both use the same server.

LAN binding requires an explicit HTTPS base URL. Supply a certificate trusted by the
distributor, or terminate HTTPS with a local reverse proxy. HTTP loopback is for host checks;
Oro Android push registration requires HTTPS endpoints and encrypted Web Push payloads.

Application-server authentication, endpoint enrollment and notification handling remain app work.

See https://docs.ntfy.sh/config/ and https://unifiedpush.org/users/distributors/.

### Examples

```bash
oroc push server
```

start at http://127.0.0.1:8787

```bash
oroc push server --offline
```

restart from the verified cached release

```bash
oroc push server --host=0.0.0.0 --base-url=https://push.dev.example:8787 --cert-file=./cert.pem --key-file=./key.pem
```

serve HTTPS on port 8787 for an Android distributor

```bash
oroc push server --base-url=https://push.dev.example
```

run loopback HTTP behind a local HTTPS reverse proxy

## oroc push distributor

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fpush%2Fdistributor

Build or install Oro Push Dev, the Kotlin Android UnifiedPush development distributor.

On Linux, '--platform=linux' runs a native Distributor2 service instead of building an APK.
It needs a session D-Bus, an unlocked Secret Service and an ntfy-compatible server.
It runs until interrupted; it does not install or enable a login service. No Node.js,
Gradle or Android SDK is required by this Linux distributor command.

Linux transport uses bounded polling every two seconds, reconnects with two-to-thirty
second delays and retains ciphertext in an OS-key-encrypted journal. Limits are 32
registrations, 32 pending messages of at most 4096 bytes, 1024 boundary-second deduplication IDs
and seven days of pending retention. The encrypted journal has a 256 KiB aggregate
limit. Storage or deduplication-window saturation pauses intake. An acknowledged
connector message means durable local receipt, not completed application work.

Linux HTTPS uses system trust, or only the explicitly supplied PEM CA database with
'--ca-cert'. Redirects are rejected. '--allow-loopback-http' permits literal loopback
addresses only; it never permits LAN HTTP or DNS names. Apps must separately opt into
'[push] development_allow_loopback_http = true' to accept these development endpoints.
Use HTTPS for phone distributors. Neither anonymous topics nor this local transport
establish trusted enrollment, server authentication or remote revocation.

### Usage

```bash
oroc push distributor [options]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<android\|linux>` | Android APK build (default), or Linux service |
| `--allow-loopback-http` | Linux only: allow literal loopback HTTP |
| `--android-home=<path>` | Android SDK (defaults to ANDROID_HOME or ANDROID_SDK_ROOT) |
| `--gradle-home=<path>` | Gradle 9 installation (defaults to GRADLE_HOME) |
| `--java-home=<path>` | JDK installation (defaults to JAVA_HOME) |
| `--data-dir=<path>` | writable build state and persistent development signing key |
| `--output=<path>` | APK destination (default: data directory/oro-push-dev.apk) |
| `--server=<origin>` | prefill an HTTPS server in the app; user taps Save and start |
| `--ca-cert=<path>` | embed a development PEM CA, trusted only by this APK |
| `--offline` | build with cached Gradle/Android dependencies only |
| `--install` | install APK and open its settings on the selected device |
| `--device=<serial>` | explicit adb serial for installation (or ANDROID_SERIAL) |
| `-h, --help` | show help |

### Notes

Requires Node.js 22+, JDK 17+, Gradle 9, an installed Android platform 34 or newer, and build tools.

Run 'oroc setup --platform=android' to provision the normal Android toolchain first.

Uses the runtime's Android Gradle plugin with built-in Kotlin support. First build may
download Android/Kotlin dependencies; --offline requires those dependencies to be cached.

A debug signing key is retained outside the SDK so updates preserve the app's state.

Oro Push Dev is a separate sideloaded development APK. Your app still registers its native push handler.

It connects to 'push server' or an ntfy-compatible HTTPS server and implements UnifiedPush
registration, selection, unregistration and acknowledged delivery. Existing ntfy distributors
continue to work with the same server and oro:push APIs; apps choose one distributor.

The user must enable its visible foreground connection in its settings. Optional CA trust
is confined to this APK; hostname verification and Oro's HTTPS endpoint requirement remain.

Limits: 32 registrations, 256 pending ciphertexts (4096 bytes each), 512 recent ACK identities.

Unacknowledged ciphertext persists across process loss; a full inbox pauses further intake.

Reboot resumes an enabled service when Android permits it. Force-stop requires reopening it.

Doze, network reachability and Android background policy still require device qualification.

This is for public development data, not production server authentication or trusted enrollment.

### Examples

```bash
oroc push distributor --platform=linux --server=http://127.0.0.1:8787 --allow-loopback-http
```

run alongside a local 'push server'

```bash
oroc push distributor --offline
```

build from the cached Android/Kotlin toolchain

```bash
oroc push distributor --server=https://push.dev.example:8787 --ca-cert=./dev-ca.pem --install --device=emulator-5554
```

build, install, then review and start Oro Push Dev

## oroc help

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fhelp

Discover commands, options, and workflow entry points from the CLI help index.

### Usage

```bash
oroc help [query...]
oroc help [query...] [--json]
```

### Options

| Option | Description |
| --- | --- |
| `--json` | emit structured search results for automation and editor tooling |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

With no query, this subcommand prints the top-level CLI help.

Exact command matches print concise command help (for example, 'oroc help build' or 'oroc help update validate').

Free-form queries search command names, descriptions, options, notes, and examples, then rank the closest matches.

Use this for discovery topics such as 'oroc help tls', 'oroc help ios signing', 'oroc help json logs', or 'oroc help agent tooling'.
'oroc help --json <query>' returns a machine-readable result list with ranked matches and exact-help payloads.

### Examples

```bash
oroc help build
```

print usage and options for the build command

```bash
oroc help update validate
```

print usage and options for the nested update validate command

```bash
oroc help ios signing
```

search for commands and notes related to iOS signing and provisioning

```bash
oroc help json --json
```

return ranked JSON results for the query "json"

## oroc update

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate

Update tooling for manifests, signatures, and bundles.

### Usage

```bash
oroc update <subcommand> [options]
```

### Commands

| Command | Description |
| --- | --- |
| `keygen` | generate an Ed25519 keypair for update manifests |
| `init` | scaffold a manifest JSON file |
| `server` | run an update server over HTTP/TCP/UDP |
| `info` | query update servers or static manifests over HTTP/TCP/UDP |
| `sign` | sign a manifest JSON file and emit a sidecar signature |
| `verify` | verify a manifest JSON file + signature using a public key |
| `bundle` | build a tar archive from a directory suitable as an update artifact |
| `extract` | extract an update bundle tar archive into a directory |
| `validate` | validate a manifest.json file against the update manifest schema shape |

### Options

| Option | Description |
| --- | --- |
| `-h, --help` | show help |

### Notes

All subcommands support '--log-file=<path>' to mirror logs to a JSON file.

Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' on relevant subcommands
to override the default manifest.json filename (and derived sidecar names).

### Examples

```bash
oroc update init
```

scaffold a default manifest.json in the current project

```bash
oroc update bundle --manifest manifest.json
```

build a source bundle and record it in the manifest

```bash
oroc update keygen > key.json
```

generate a signing keypair JSON file

```bash
oroc update sign --keys key.json --manifest manifest.json
```

sign the manifest and write manifest.sig

```bash
oroc update verify --keys key.json --manifest manifest.json
```

verify manifest.json against manifest.sig with the public key

```bash
oroc update server --root ./updates
```

run an HTTP update server serving manifests and bundles

```bash
oroc update info --http --app-id com.example.app --follow-manifest
```

query an update server and fetch the referenced manifest

## oroc update init

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Finit

Scaffold a minimal update manifest JSON file.

### Usage

```bash
oroc update init [options]
```

### Options

| Option | Description |
| --- | --- |
| `--config=<path>` | use an explicit oro.toml/oro.ini file when deriving defaults |
| `--manifest-name=<name>` | filename for the manifest JSON (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

The generated manifest includes these defaults.
- schemaVersion = 1
- appId derived from your oro.toml ([meta] bundle_identifier, falling back to "com.example.app")
- generatedAt = current UTC timestamp
- channels = [update_channel or "stable"]
- updates = a single entry for the current version/channel (with an empty targets array)
Edit the generated file to add real targets, artifact metadata, or additional updates.

### Examples

```bash
oroc update init
```

create ./manifest.json using oro.toml metadata

```bash
oroc update init --manifest-name app-updates.json
```

create ./app-updates.json instead of manifest.json

## oroc update server

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fserver

Run an update server that speaks the Oro Application Update Protocol.

### Usage

```bash
oroc update server [options]
```

### Options

| Option | Description |
| --- | --- |
| `--root=<dir>` | directory containing manifest trees and artifacts to serve |
| `--host=<host>` | interface to bind (default: 0.0.0.0) |
| `--port=<port>` | TCP/UDP port to bind (default: 8080) |
| `--manifest-name=<name>` | manifest filename to look up under each appId (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME) |
| `--tcp` | run in TCP mode (binary OUP CHECK/RESPONSE) |
| `--udp` | run in UDP mode (binary OUP CHECK/RESPONSE) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

Default mode is HTTP. The server exposes these endpoints.
- GET /health           — readiness metadata
- POST /check           — accepts a CHECK JSON payload with "appId" and responds with a RESPONSE JSON
whose manifestUrl points at "/<appId>/<manifest-name>" when present
- GET /<path>           — serves files rooted under --root, including "<appId>/<manifest-name>"
and "<appId>/<manifest-name>.sig"
HTTP mode is designed to be run behind a load balancer or reverse proxy in production.

TCP and UDP modes implement the same CHECK/RESPONSE selection semantics using the binary OUP framing.

### Examples

```bash
oroc update server --root ./updates
```

serve manifests and bundles over HTTP on port 8080

```bash
oroc update server --root ./updates --tcp --port 9090
```

run a TCP OUP server on port 9090

```bash
oroc update server --root ./updates --udp --port 9090
```

run a UDP OUP server on port 9090

## oroc update info

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Finfo

Query update servers or static manifests over HTTP/TCP/UDP.

### Usage

```bash
oroc update info [--transport=<http|tcp|udp>] [options]
```

### Options

| Option | Description |
| --- | --- |
| `--config=<path>` | use an explicit oro.toml/oro.ini file for local CHECK request defaults |
| `--transport=<http\|tcp\|udp>` | transport to use (default: http) |
| `--http` | shorthand for --transport=http |
| `--tcp` | shorthand for --transport=tcp |
| `--udp` | shorthand for --transport=udp |
| `--follow-manifest` | when contacting servers, follow 'manifestUrl' in the RESPONSE and fetch/validate that manifest over HTTP(S) |
| `--timeout-ms=<ms>` | optional timeout for TCP/UDP CHECK requests (0 = no timeout) |
| `--manifest-url=<url>` | HTTP(S) URL of a manifest.json hosted statically |
| `--signature-url=<url>` | optional HTTP(S) URL of the corresponding signature JSON (default: derived from --manifest-url) |
| `--keys=<file>` | JSON file containing a public key ("publicKey" or "key" field) for manifest verification |
| `--public-key=<hex>` | Ed25519 public key as a hex string for manifest verification |
| `--host=<host>` | host for HTTP/TCP/UDP update servers (default: 127.0.0.1) |
| `--port=<port>` | port for HTTP/TCP/UDP update servers (default: 8080) |
| `--app-id=<id>` | application identifier to send in CHECK messages (default: active config meta.bundle_identifier when available) |
| `--channel=<name>` | update channel hint (default: active config update_channel or "stable") |
| `--current-version=<version>` | current application version hint (default: active config meta.version when available) |
| `--runtime-version=<version>` | runtime version hint advertised in CHECK (optional) |
| `--platform=<id>` | platform hint advertised in CHECK (optional) |
| `--arch=<id>` | architecture hint advertised in CHECK (optional) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

- With '--manifest-url', this command fetches and pretty-prints a manifest JSON and reports whether a signature file is reachable.
When '--keys' or '--public-key' is provided and libsodium is available, it also verifies the manifest signature before printing.
- With HTTP/TCP/UDP transports and no '--manifest-url', it sends a CHECK message to an update server and pretty-prints the RESPONSE JSON.
With '--follow-manifest', if the RESPONSE includes a 'manifestUrl', it will fetch, validate, and optionally verify that manifest as well.
When '--app-id' is provided, the fetched manifest must have a matching 'appId' or the command exits with an error.
- When building a CHECK request and '--config' is omitted, this command looks for `oro.toml` first and falls back to
`oro.ini` in the current workspace to derive app-id, channel, and current-version defaults. Outside a project
workspace, pass those values explicitly.
- When using TCP/UDP, '--timeout-ms' can be used to bound how long the client waits for a response.
- The flags '--http', '--tcp', and '--udp' are shorthands for '--transport=http', '--transport=tcp', and '--transport=udp' respectively.

### Examples

```bash
oroc update info --manifest-url https://cdn.example.com/app/manifest.json
```

inspect a statically hosted manifest

```bash
oroc update info --manifest-url https://cdn.example.com/app/manifest.json --keys app-pubkey.json
```

fetch and verify a statically hosted manifest + signature

```bash
oroc update info --http --host 127.0.0.1 --port 8080 --app-id com.example.app --follow-manifest
```

query an HTTP update server and then fetch the referenced manifest

```bash
oroc update info --tcp --host 127.0.0.1 --port 9000 --app-id com.example.app --follow-manifest
```

query a TCP update server using the binary OUP protocol

## oroc update keygen

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fkeygen

Generate an Ed25519 keypair for signing update manifests.

### Usage

```bash
oroc update keygen [options]
```

### Options

| Option | Description |
| --- | --- |
| `--out=<path>` | write keypair JSON to a file instead of stdout |
| `--key-id=<id>` | optional key identifier to embed in the keypair (default: "pk-1") |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

The generated JSON includes "keyId", "publicKey", and "privateKey" fields (hex-encoded).

Keep the private key secret; distribute only the public key with your application.

### Examples

```bash
oroc update keygen > key.json
```

generate a default keypair and save it to key.json

```bash
oroc update keygen --key-id pk-prod --out prod-key.json
```

generate a named keypair for production use

## oroc update sign

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fsign

Sign an update manifest and emit a detached manifest.sig file.

### Usage

```bash
oroc update sign (--keys=<file> | --private-key=<hex>) [options]
```

### Options

| Option | Description |
| --- | --- |
| `--manifest=<path>` | path to the manifest JSON file to sign (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| `--manifest-name=<name>` | manifest filename to use when --manifest is not provided |
| `--keys=<file>` | JSON file containing a signing key (with "privateKey" or "secretKey" field) |
| `--private-key=<hex>` | Ed25519 private key as a hex string |
| `--key-id=<id>` | optional key identifier to embed in manifest.sig (default: "pk-1") |
| `--out=<path>` | output path for manifest.sig (default: <manifest-without-extension>.sig, e.g. manifest.json -> manifest.sig) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

The signature file is JSON containing "schemaVersion", "algorithm", "keyId", and "signature" fields.

Clients verify manifest bytes against manifest.sig and the configured public key(s).

Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename.

### Examples

```bash
oroc update sign --keys key.json --manifest manifest.json
```

sign manifest.json using the private key in key.json

```bash
oroc update sign --private-key <hex-private-key> --manifest manifest.json --out manifest.sig
```

sign a manifest using a raw hex private key

## oroc update verify

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fverify

Verify a manifest + signature pair using an Ed25519 public key.

### Usage

```bash
oroc update verify (--keys=<file> | --public-key=<hex>) [options]
```

### Options

| Option | Description |
| --- | --- |
| `--manifest=<path>` | path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| `--manifest-name=<name>` | manifest filename to use when --manifest is not provided |
| `--signature=<path>` | path to the signature JSON file (default: <manifest-without-extension>.sig) |
| `--keys=<file>` | JSON file containing a public key ("publicKey" or "key" field) |
| `--public-key=<hex>` | Ed25519 public key as a hex string |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

Exits with status 0 when the signature is valid for the manifest and public key; non-zero otherwise.

Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename
(the default signature path is derived as '<manifest-without-extension>.sig', e.g. manifest.json -> manifest.sig).

### Examples

```bash
oroc update verify --keys key.json --manifest manifest.json
```

verify manifest.json against manifest.sig using the public key in key.json

```bash
oroc update verify --public-key <hex-public-key> --manifest manifest.json --signature manifest.sig
```

verify using an explicit hex-encoded public key and signature file

## oroc update validate

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fvalidate

Validate an update manifest JSON file against the expected schema shape.

### Usage

```bash
oroc update validate [--manifest=<path>] [options]
```

### Options

| Option | Description |
| --- | --- |
| `--manifest=<path>` | path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| `--manifest-name=<name>` | manifest filename to use when --manifest is not provided |
| `--strict` | enable additional consistency checks (for example, channels vs updates, artifactUrl shape) |
| `--json` | print a machine-readable JSON result object (for CI) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

This command parses the manifest and performs lightweight structural validation aligned with
'schemas/update-manifest.schema.json' (required fields, types, and key relationships).

It does not attempt full JSON Schema validation, but is suitable for fast local checks and CI.

When '--strict' is provided, additional consistency rules are enforced.

When '--json' is provided, stdout is reserved for the result object only.

### Examples

```bash
oroc update validate --manifest manifest.json
```

run basic structural checks against manifest.json

```bash
oroc update validate --manifest manifest.json --strict
```

enable stricter consistency rules in addition to structural checks

```bash
oroc update validate --manifest manifest.json --json
```

print validation status as JSON for CI or agents

## oroc update bundle

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fbundle

Build a tar archive containing the contents of a directory for use as an update artifact.

### Usage

```bash
oroc update bundle [--input=<dir>] [--output=<bundle.tar>] [options]
```

### Options

| Option | Description |
| --- | --- |
| `--input=<dir>` | directory whose contents will be archived (default: project directory) |
| `--output=<bundle.tar>` | path to the tar archive to write (default: <build_name>-<version>.tar) |
| `--manifest=<path>` | optional manifest path to update with a new target for this bundle |
| `--manifest-name=<name>` | manifest filename to use when --manifest is not provided |
| `--channel=<name>` | update channel to associate with this bundle (default: update_channel or "stable") |
| `--update-id=<id>` | update id to associate with this bundle (default: <channel>-<version>) |
| `--platform=<id>` | platform identifier for the bundle target (default: "source") |
| `--arch=<id>` | architecture identifier for the bundle target (default: "any") |
| `--artifact-url=<url-or-path>` | artifactUrl to record in the manifest target (default: bundle filename) |
| `--hash-algorithm=<sha256\|sha1>` | hash algorithm to use for the bundle payload (default: sha256 when libsodium is available, otherwise sha1) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

The archive is a plain tar file (no compression) built using the runtime's native tar implementation.

Directory layout and basic metadata (mode bits, mtime) are preserved.

When omitted, '--input' defaults to the project directory (app source),
and '--output' defaults to "<build_name>-<version>.tar" derived from your oro.toml metadata.

When '--manifest' or '--manifest-name' (or ORO_UPDATE_MANIFEST_FILENAME) is provided, the manifest is updated
with a new target entry describing this bundle (including length and hash).

### Examples

```bash
oroc update bundle
```

bundle the current project source into <build_name>-<version>.tar

```bash
oroc update bundle --manifest manifest.json
```

bundle the project and record the artifact in manifest.json

```bash
oroc update bundle --input dist --output app-1.2.3.tar --manifest manifest.json --channel beta
```

bundle a custom directory and attach it as a beta update in the manifest

## oroc update extract

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fextract

Extract an update tar archive into a destination directory.

### Usage

```bash
oroc update extract --bundle=<bundle.tar> --dest=<dir> [options]
```

### Options

| Option | Description |
| --- | --- |
| `--bundle=<bundle.tar>` | path to the tar archive to extract |
| `--dest=<dir>` | destination directory to extract files into (created if missing) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

The extractor rejects absolute paths and any paths containing ".." or ':' to avoid directory traversal.

Special tar entries (symlinks, devices, etc.) are ignored; regular files and directories are restored.

### Examples

```bash
oroc update extract --bundle app-1.0.0.tar --dest ./update-staging
```

extract the contents of app-1.0.0.tar into ./update-staging

## oroc build

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fbuild

Build Oro application.

Provide a project directory, HTML file, or JavaScript module. When no
oro.toml is found, oroc infers a minimal configuration automatically.

### Usage

```bash
oroc build [options] [<project-or-source>]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<platform>` | platform target to build application for (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| `--config=<path>` | use an explicit oro.toml/oro.ini file |
| `--device=<serial>` | Android device to install and run on |
| `--copy=<source:dest>` | extra copy mapping (like [build] copy; can be repeated) |
| `--port=<port>` | load "index.html" from a specific port (if host is not specified, defaults to localhost) |
| `--host=<host>` | load "index.html" from a specific host (if port is not specified, defaults to 80) |
| `-t, --test=<path>` | enable test mode and import a test file relative to resource files |
| `-H, --headless` | build application to run in headless mode (without frame or window) |
| `-P, --prod` | build for production (disables debugging info, inspector, etc.) |
| `-D, --debug` | enable debug mode |
| `-E, --env=<name[=value]>` | add environment variables |
| `-o, --only-build` | only run the build step |
| `-p, --package` | package the app for distribution |
| `-q, --quiet` | hint for less log output |
| `-r, --run` | run after building |
| `-V, --verbose` | enable verbose output |
| `-w, --watch` | watch for changes to rerun build step |
| `--allow-exec` | allow native extension commands (git/build scripts) |
| `--sanitizers` | enable developer sanitizers (ASan/UBSan) for desktop core builds |
| `--cache` | enable all supported build result caches (default: off) |
| `--no-cache` | disable supported build result caches for this build |
| `--compiler-cache` | enable ccache/sccache for native object compilation |
| `--no-compiler-cache` | disable the native compiler cache |
| `--android-cache` | enable all supported caches for Android builds |
| `--no-android-cache` | disable supported caches for Android builds |
| `--android-build-cache` | enable Gradle's task output cache |
| `--no-android-build-cache` | disable Gradle's task output cache |
| `--android-configuration-cache` | enable Gradle's configuration cache |
| `--no-android-configuration-cache` | disable Gradle's configuration cache |
| `--tls-keylog=<path>` | write NSS TLS key log lines to <path> (OpenSSL provider) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `--editor-project=<dir>` | export an Android editor project |
| `--remote-debugging-port=<port>` | set the remote debugging port |
| `-h, --help` | show help |

### Linux options

| Option | Description |
| --- | --- |
| `-f, --package-format=<format>` | package a Linux application in a specified format for distribution:<br>- deb (default)<br>- rpm<br>- zip<br>- aur (generate AUR PKGBUILD only) |
| `--sign` | sign Linux packages with GPG (writes .asc next to the artifact) |
| `--sign-key=<id>` | optional GPG key ID or fingerprint to use with '--sign' (letters/digits/@._- only) |

### macOS options

| Option | Description |
| --- | --- |
| `-c, --codesign` | code sign application with 'codesign' |
| `-n, --notarize` | notarize application with 'notarytool' |
| `-f, --package-format=<format>` | package a macOS application in a specified format for distribution:<br>- zip (default)<br>- pkg |

### iOS options

| Option | Description |
| --- | --- |
| `-c, --codesign` | code sign application during xcodebuild (requires '[ios] provisioning_profile' in your config file) |

### Windows options

| Option | Description |
| --- | --- |
| `-f, --package-format=<format>` | package a Windows application in a specified format for distribution:<br>- appx (default) |

### Environment

| Variable | Description |
| --- | --- |
| `ORO_ALLOW_EXEC` | allow native extension commands during builds |
| `ORO_ENABLE_SANITIZERS` | enable ASan/UBSan on desktop builds |

### Notes

Relative paths are resolved from the project root or from the directory containing the explicit
`--config` file when one is supplied.
`--prod` controls packaging/debug defaults; `--package` controls whether a distributable artifact
is produced in addition to the build output directory.

Use `--copy` for one-off extra bundle mappings without modifying `oro.toml`.

Cache flags override oro.toml; specific cache flags override broader cache flags.

Set [build] cache=true for a project default, [android] cache=true for Android only,
or [android] configuration_cache=true for only Gradle configuration caching.

Use compiler_cache in [build] or a platform section to control native object caching.

Compiler caching uses ccache/sccache from PATH and compiles directly if neither is installed.

Download caches and existing incremental build behavior retain their toolchain defaults.

Android run targets use --device, then ANDROID_SERIAL, then the first ready device
listed by `adb devices`. The android-emulator target selects the first ready emulator.

NO_ANDROID and NO_IOS do not control this application build command. They are independent,
presence-based exclusions for runtime source bootstrap (`bin/install.sh` / `npm run relink`).

### Common errors

Android builds: run `oroc setup --platform=android` and accept SDK licenses.
macOS/iOS signing: set `[ios] provisioning_profile`/specifier or `[mac.productbuild] identity` in your config file.

Standard Android builds run NDK and Gradle automatically. Native extension commands may require `--allow-exec` or `ORO_ALLOW_EXEC=1`.

### Linux dependencies

deb packaging requires `dpkg` and `fakeroot` (e.g., `sudo apt-get install dpkg-dev fakeroot`).
rpm packaging requires `rpmbuild` (e.g., `sudo dnf install rpm-build`).

### macOS dependencies

Xcode and Command Line Tools are required (`xcode-select --install`).
For Gradle/JDK: `brew install gradle openjdk` (or use SDKMAN).

### Windows dependencies

Windows 10/11 SDK and Visual Studio Build Tools are recommended.
Ensure `signtool.exe` is available (set SIGNTOOL or add SDK bin to PATH).

### Examples

```bash
oroc build .
```

build the current project for the host platform

```bash
oroc build -r --debug .
```

rebuild and immediately run a debug build

```bash
oroc build --platform=ios --prod --package .
```

produce a signed/package-ready iOS build using your config

```bash
oroc build --copy assets:assets .
```

include an extra assets directory in the bundle for this build only

```bash
oroc build --platform=android --cache --no-android-configuration-cache .
```

enable supported caches except Gradle configuration caching

## oroc run

Web docs: https://oro.computer/runtime/docs/?p=cli%2Frun

Run application.

Provide a project directory, HTML file, or JavaScript module. When no
oro.toml is found, oroc infers a minimal configuration automatically.

### Usage

```bash
oroc run [options] [<project-or-source>]
```

### Options

| Option | Description |
| --- | --- |
| `-H, --headless` | run application in headless mode (without frame or window) |
| `--platform=<platform>` | platform target to run application on (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| `--config=<path>` | use an explicit oro.toml/oro.ini file |
| `--device=<serial>` | Android device to install and run on |
| `--port=<port>` | load "index.html" from a specific port (if host is not specified, defaults to localhost) |
| `--host=<host>` | load "index.html" from a specific host (if port is not specified, defaults to 80) |
| `-P, --prod` | build for production (disables debugging info, inspector, etc.) |
| `-t, --test=<path>` | enable test mode and import a test file relative to resource files |
| `-D, --debug` | enable debug mode |
| `-q, --quiet` | hint for less log output |
| `-E, --env=<name[=value]>` | add environment variables |
| `-V, --verbose` | enable verbose output |
| `--allow-exec` | allow native extension commands (git/build scripts) |
| `--tls-keylog=<path>` | write NSS TLS key log lines to <path> (OpenSSL provider) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `--remote-debugging-port=<port>` | set the remote debugging port |
| `--sanitizers` | enable desktop ASan/UBSan sanitizers |
| `-h, --help` | show help |

### Environment

| Variable | Description |
| --- | --- |
| `ORO_DEBUG` | enable debug mode (like -D) |
| `ORO_VERBOSE` | enable verbose logs (like -V) |

### Notes

Android targets use --device, then ANDROID_SERIAL, then the first ready device
listed by `adb devices`. The android-emulator target selects the first ready emulator.

### Common errors

For CI/Linux headless, install `xvfb-run` or set a custom headless runner in your config.

Use `--test=path` to run tests bundled with your app.

### Examples

```bash
oroc run .
```

build and launch the current project on the host platform

```bash
oroc run --platform=android-emulator .
```

build and launch the current project on the Android emulator target

```bash
oroc run --headless --test=tests/smoke.js .
```

execute a bundled test entrypoint in headless mode

## oroc list-devices

Web docs: https://oro.computer/runtime/docs/?p=cli%2Flist-devices

Get the list of connected devices.

### Usage

```bash
oroc list-devices [options] --platform=<platform>
oroc devices [options] --platform=<platform>
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<platform>` | platform target to list devices for:<br>- android<br>- ios |
| `-f, --format=<format>` | print as 'text' or 'json' |
| `--json` | shorthand for '--format=json' |
| `--ecid` | show device ECID (ios only) |
| `--udid` | show device UDID (ios only) |
| `--only` | only show ECID or UDID of the first device (ios only) |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

Android output mirrors the device identifiers returned by `adb devices`.
iOS output can include the human-readable simulator/device name plus ECID or UDID for scripting.

JSON output emits an array of device objects with stable id fields for automation.

### Examples

```bash
oroc list-devices --platform=android
```

list attached Android devices visible to adb

```bash
oroc list-devices --platform=ios --udid
```

print iOS devices with UDID values you can pass to --device

```bash
oroc list-devices --platform=ios --json
```

print connected iOS devices as JSON for scripts or agents

## oroc env

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fenv

Print environment variables relevant to the Oro CLI and build configuration.

### Usage

```bash
oroc env [options]
```

### Options

| Option | Description |
| --- | --- |
| `-f, --format=<format>` | print as 'text' or 'json' |
| `--json` | shorthand for '--format=json' |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

- Prints a curated set of CLI, runtime, source-bootstrap, toolchain, and platform variables (for example, ORO_DEBUG, ORO_VERBOSE, ORO_HOME, NO_ANDROID, NO_IOS, JAVA_HOME, ANDROID_HOME, APPLE_ID, SIGNTOOL).
- NO_ANDROID and NO_IOS are independent presence flags for runtime source bootstrap. A non-empty
NO_ANDROID disables only Android work; a non-empty NO_IOS disables only iOS/iOS Simulator work
on macOS. Values such as 0 and false still disable the named target. They do not select the
application target for `oroc build --platform`.
- Merges [env] / env_* entries from the active configuration and from local .ororc files when present.
- Filters out unset variables; text output prints each line as KEY=VALUE.
- JSON output emits a flat object keyed by environment variable name.

### Examples

```bash
oroc env
```

print the effective environment inputs visible to Oro tooling

```bash
oroc env --json
```

print the effective environment inputs as JSON

## oroc mcp

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fmcp

Run a Model Context Protocol (MCP) server for agent tooling.

By default this subcommand speaks JSON-RPC over stdio (stdout is reserved for MCP messages).
Use --http to run an HTTP/SSE transport.

### Usage

```bash
oroc mcp [options] [<workspace-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--stdio` | use stdio transport (default) |
| `--http` | use HTTP/SSE transport |
| `--host=<host>` | HTTP bind host (default: 127.0.0.1) |
| `--port=<port>` | HTTP bind port (default: 0 for ephemeral) |
| `--endpoint=<path>` | HTTP endpoint path (default: /mcp; normalizes mcp,/mcp,/mcp/) |
| `--token=<token>` | require this bearer token (default: auto-generated in HTTP mode) |
| `--no-auth` | disable token auth (loopback only) |
| `--workspace=<path>` | explicit workspace root (default: current directory or <workspace-dir>) |
| `--config=<path>` | workspace-relative oro.toml/oro.ini path to use as the active project config |
| `--read-workspace-only` | restrict filesystem reads to the workspace root (default; compatibility flag) |
| `--allow-read-outside-workspace` | explicitly allow reading arbitrary files outside the workspace root |
| `--replace-sse-stream` | allow a new SSE connection to replace an existing one for the same session id |
| `-h, --help` | show help |

### Notes

Stdio mode disables JSON logs and suppresses INFO output so stdout remains valid MCP JSON-RPC.

HTTP mode supports MCP 2026-07-28 requests and MCP 2025-11-25 and 2025-06-18 sessions.

MCP 2026-07-28 clients call server/discover, send per-request metadata and routing headers, and use a long-lived
subscriptions/listen POST for notifications; they do not initialize or send a session header. MCP 2025 clients
initialize first and then include Mcp-Session-Id on subsequent requests. HTTP bearer authentication is enabled
by default.

The server publishes descriptive MCP tool metadata, including standard titles, safety annotations, extension
data under _meta, and structured tool results to help human and AI clients choose the right operation without
shell parsing.

Use the MCP `search_docs` tool for topic-driven discovery across README, source-build environment, MCP,
API, config, and manpage resources before falling back to manual resource walking.

When --config is omitted, the active config defaults to oro.toml and falls back to oro.ini when that is the
only standard project config present in the workspace.
resources/list includes the workspace root and, when present, the active config file, project-local docs under
conventional paths, and installed runtime-doc:/ references for shipped API, CLI, config, MCP, README,
BUILD_ENVIRONMENT.md, llms.txt, and man1/man3/man7 docs when available.

Use runtime-doc:/BUILD_ENVIRONMENT.md for the independent, presence-based NO_ANDROID and NO_IOS
source-bootstrap controls; neither variable selects an application build target.

Prefer runtime-doc:/ resources for runtime behavior and API contracts, and workspace:/ resources for the current
app's config and local docs.

### Examples

```bash
oroc mcp --stdio .
```

start an MCP server over stdio for the current workspace

```bash
oroc mcp --http --host=127.0.0.1 --port=8080 .
```

expose the MCP server over local HTTP/SSE

## oroc init

Web docs: https://oro.computer/runtime/docs/?p=cli%2Finit

Create a new project. If the path is not provided, the new project will be created in the current directory.

### Usage

```bash
oroc init [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `-C, --config` | only create the config file |
| `-n, --name=<name>` | project name |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

By default this command creates a starter `oro.toml`, `src/index.html`, `src/index.js`,
`src/serviceworker.js`, and `.gitignore`.

Use `--config` when you only want the config scaffold without the example app files.

### Examples

```bash
oroc init my-app
```

create a new starter project in ./my-app

```bash
oroc init --config
```

write only the default oro.toml into the current directory

## oroc install-app

Web docs: https://oro.computer/runtime/docs/?p=cli%2Finstall-app

Install the app to the device or host target.

### Usage

```bash
oroc install-app [options] [<project-dir>]
oroc install [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `-D, --debug` | enable debug output |
| `--device=<identifier>` | identifier (ecid, ID) of the device to install to Android defaults to ANDROID_SERIAL or the first ready device |
| `--platform=<platform>` | platform to install application to device (defaults to host):<br>- android<br>- ios |
| `-P, --prod` | install production application |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### macOS options

| Option | Description |
| --- | --- |
| `--target=<target>` | installation target for macOS application (defaults to '/') the application is installed into '$target/Applications' |

### Notes

Android installs use the first ready device listed by `adb devices` when neither
--device nor ANDROID_SERIAL is set. Offline and unauthorized devices are skipped.

The android-emulator target selects the first ready emulator.

### Common errors

Android: list devices with `adb devices` or pass `--device`.
iOS/macOS: list devices with `oroc list-devices --platform=ios` and pass `--device`.

### Examples

```bash
oroc install-app --platform=android --device emulator-5554
```

install the current Android build onto a specific emulator

```bash
oroc install-app --platform=ios --device <udid>
```

install the current iOS build onto a specific simulator or device

## oroc print-build-dir

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fprint-build-dir

Print the build directory path

### Usage

```bash
oroc print-build-dir [options] [<project-dir>]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<platform>` | platform to print build directory for (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| `-P, --prod` | indicate production build directory |
| `--root` | print the root build directory |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

This is useful for scripts that need to locate packaged artifacts, intermediate bundles, or
platform-specific staging directories without reimplementing Oro's path logic.

### Examples

```bash
oroc print-build-dir .
```

print the host-platform build directory for the current project

```bash
oroc print-build-dir --platform=ios --prod .
```

print the production iOS build directory

## oroc setup

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fsetup

Setup build tools for host or target platform.

### Usage

```bash
oroc setup [options] [--platform=<platform>] [-y|--yes]
```

### Options

| Option | Description |
| --- | --- |
| `--platform=<platform>` | platform target to run setup for (defaults to host):<br>- android<br>- ios<br>- linux<br>- windows |
| `-q, --quiet` | hint for less log output |
| `-y, --yes` | answer yes to any prompts |
| `-D, --debug` | enable debug output |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Common errors

Without `--platform`, setup defaults to the host. Verify with `oroc env`.

### Examples

```bash
oroc setup --platform=android
```

install or validate Android SDK/NDK requirements

```bash
oroc setup --platform=ios
```

install or validate host tools needed for iOS/macOS builds

## oroc config

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fconfig

Inspect configuration values.

### Usage

```bash
oroc config [options] [<key-or-path>]
```

### Options

| Option | Description |
| --- | --- |
| `--config=<path>` | use an explicit oro.toml/oro.ini file |
| `--list` | list known configuration keys with current and default values |
| `--key=<name>` | print the current value for a specific key |
| `--describe=<name>` | print help and metadata for a specific key |
| `-f, --format=<format>` | print the full configuration as 'toml', 'ini', or 'json' |
| `--json` | shorthand for '--format=json' |
| `--strict` | treat unknown or unset keys as errors (non-zero exit) |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

Keys may be provided in flattened form (e.g., "filesystem_sandbox_enabled") or as TOML-style paths (e.g., "filesystem.sandbox_enabled").

A bare argument after 'config' is treated as a key query (for example, 'oroc config filesystem.sandbox_enabled').

Unknown keys are printed when present in the active configuration but are marked as undocumented.
`--format` prints the merged effective configuration after applying oro.toml, local .ororc overrides,
and CLI-selected config sources.

Use '--log-file=<path>' instead of the global 'oroc --json ...' flag when you need structured config output on stdout and logs at the same time.

### Examples

```bash
oroc config --list
```

list all known configuration keys with effective values

```bash
oroc config meta.version
```

print the effective value of meta.version

```bash
oroc config --describe webview.root
```

print the description, default, and source for webview.root

```bash
oroc config "ios.*"
```

glob-match a group of related keys

```bash
oroc config --format json
```

print the merged effective configuration as JSON

## oroc versions

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fversions

Print Oro CLI/runtime and dependency versions.

### Usage

```bash
oroc versions [options] [<dependency-name>]
```

### Options

| Option | Description |
| --- | --- |
| `-f, --format=<format>` | print as 'text' or 'json' |
| `--json` | shorthand for '--format=json' |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

With no dependency name, prints the full version map for the CLI, runtime, and linked libraries.

Passing a dependency name filters to a single entry (for example: oro, oroc, uv, sqlite, iroh).

Use '--json' or '--format=json' when you need stable machine-readable output on stdout.

Use '--log-file=<path>' if you also need logs; the global 'oroc --json ...' form is reserved for JSON log streaming.

### Examples

```bash
oroc versions
```

print all known dependency versions as text

```bash
oroc versions --json
```

print all known dependency versions as JSON

```bash
oroc versions uv
```

print only the libuv version

## oroc version

Web docs: https://oro.computer/runtime/docs/?p=cli%2Fversion

Inspect or bump the project version defined in your configuration file.

### Usage

```bash
oroc version [options]
oroc version <new-version | release> [options]
```

### Options

| Option | Description |
| --- | --- |
| `-C, --config=<path>` | explicit oro.toml/oro.ini to update |
| `--preid=<id>` | pre-release identifier for pre* / prerelease bumps (default: "rc") |
| `-V, --verbose` | enable verbose output |
| `--log-file=<path>` | mirror logs to a JSON file |
| `-h, --help` | show help |

### Notes

- With no arguments, 'version' prints the current semantic version from the [meta] section.
- With a <new-version> argument, it sets the version to that exact SemVer 2.0.0 value.
- With a release type, it bumps the version using SemVer rules:
major, minor, patch,
premajor, preminor, prepatch, prerelease
For pre* / prerelease bumps, '--preid' controls the pre-release tag (e.g., 'beta').
- The command updates only your app configuration file; it does not call git or create tags.
- oro.toml is the preferred config file, oro.ini is fully supported when oro.toml is absent.

### Examples

```bash
oroc version
```

print the current project version

```bash
oroc version minor
```

bump version to the next minor

```bash
oroc version prepatch --preid beta
```

bump to the next patch and start a 'beta' pre-release

```bash
oroc version 1.2.3
```

set the version explicitly to 1.2.3
