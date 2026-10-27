//
// CLI help uses an 80-column layout with descriptions at column 39.
// gManualText supplements are included in generated docs and topic search only.
// Matching section and option labels replace brief descriptions in the manuals.
//
constexpr auto gHelpText = R"TEXT(
{{cli_name}} v{{cli_version}}

Build, run, and manage Oro applications.

Usage:
  {{cli_name}} <command> [options]
  {{cli_name}} help [query...]

Commands:
  build                               build project
  config                              inspect configuration values and docs
  env                                 print relevant environment variables
  help                                search commands and documentation
  init                                create a project
  install-app                         install app to the device
  install                             alias for install-app
  language-server                     run a language server over stdio
  list-devices                        get the list of connected devices
  devices                             alias for list-devices
  mcp                                 run an MCP server for agent tooling
  print-build-dir                     print build path to stdout
  push                                manage local UnifiedPush development
  run                                 run application
  setup                               install build dependencies
  tooling                             export editor compilation models
  update                              update tooling (keys, manifests, bundles)
  version                             inspect or bump project version
  versions                            print runtime and dependency versions

General options:
  -h, --help                          show help
  --prefix                            print install path
  -v, --version                       print program version
  -q, --quiet                         hint for less log output
  -V, --verbose                       enable verbose output (can be global)
  -D, --debug                         enable debug output (can be global)
  --no-color                          disable colored log output
  --json                              enable structured JSON logs on stdout
  --log-file=<path>                   mirror logs to a JSON file

Documentation:
  man oroc
)TEXT";

constexpr auto gManualText = R"TEXT(
Commands:
  help                                search commands, options, and platform notes
  init                                create a new project (in the current directory)
  language-server                     run Oro Language Server over stdio
  tooling                             export and prepare editor compilation models
  push                                run a local UnifiedPush development server

Environment:
  ORO_DEBUG                           enable debug mode (like -D)
  ORO_VERBOSE                         enable verbose logs (like -V)
  ORO_LOG_NO_COLOR                    disable colored log output
  ORO_LOG_JSON                        enable structured JSON logs on stdout
  ORO_LOG_FILE                        mirror logs to a JSON file
  ORO_ALLOW_EXEC                      allow native extension commands during builds
  ORO_ENABLE_SANITIZERS               enable ASan/UBSan on desktop builds
  NO_ANDROID                          source bootstrap: disable only Android work when non-empty
  NO_IOS                              source bootstrap: disable only iOS/iOS Simulator work when non-empty

Notes:
  Run '{{cli_name}} <command> --help' for usage and options; see the command's man page for examples and platform guidance.

  Use '{{cli_name}} help <query>' to search commands, options, examples, and workflow notes by topic.
  '{{cli_name}} <subcommand> --json' emits structured command results when that subcommand supports it.

  The global '{{cli_name}} --json <subcommand>' form is reserved for JSON log streaming.

  Use '--log-file=<path>' when you need machine-readable stdout plus a separate log stream.

  NO_ANDROID and NO_IOS are independent presence flags for building the runtime source tree;
  they do not select an application target for '{{cli_name}} build --platform'. Values such as
  0 and false are non-empty and still disable the named target.

Examples:
  {{cli_name}} init my-app
                                      create a starter Oro project in ./my-app
  {{cli_name}} run .
                                      run the app rooted at the current directory
  {{cli_name}} build --platform=ios --prod .
                                      build a production iOS artifact from the current project
  {{cli_name}} config --describe meta.version
                                      inspect documentation for a single configuration key
  {{cli_name}} help ios signing
                                      search for commands and notes related to iOS signing and provisioning

Update tooling:
  {{cli_name}} update keygen          generate an Ed25519 keypair for signing update manifests
  {{cli_name}} update init            scaffold a manifest JSON file
  {{cli_name}} update server          run an update server over HTTP/TCP/UDP
  {{cli_name}} update info            query update servers or static manifests over HTTP/TCP/UDP
  {{cli_name}} update sign            sign a manifest JSON file and emit a sidecar signature
  {{cli_name}} update verify          verify a manifest JSON file + signature using a public key
  {{cli_name}} update bundle          build a tar archive from a directory suitable as an update artifact
  {{cli_name}} update extract         extract an update bundle tar archive into a directory
  {{cli_name}} update validate        validate a manifest.json file against the update manifest schema shape
)TEXT";

constexpr auto gHelpTextLanguageServer = R"TEXT(
Run Oro Language Server over stdio.

Usage:
  {{cli_name}} language-server --language <name> [options] [<project-dir>]

Options:
  --language=<name>                   language adapter (required)
  --variant=<name>                    Android variant (default devDebug)
  --server=<path>                     override the configured language server
                                      executable
  --prepare                           prepare native dependencies before launch
                                      (Kotlin prepares SDK classes automatically)
  --startup-timeout=<ms>              preparation and startup deadline (300000)
  --refresh                           regenerate the selected editor model
  --offline                           use installed dependencies only
  --platform=<name>                   native target platform
  --arch=<name>                       native target architecture
  --prod                              release compiler flags for native models
  --config=<path>                     use an explicit oro.toml or oro.ini
  -h, --help                          show help

Documentation:
  man oroc-language-server
)TEXT";

constexpr auto gManualTextLanguageServer = R"TEXT(
Oro Language Server: editor-independent build models and language adapters.

Options:
  --language=<name>                   required: kotlin, typescript, cpp, c, objc, objcpp, swift, or configured language
  --config=<path>                     explicit oro.toml or oro.ini

Notes:
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
)TEXT";

constexpr auto gHelpTextTooling = R"TEXT(
Export and prepare editor compilation models.

Usage:
  {{cli_name}} tooling <command> [options]

Commands:
  kotlin                              export or prepare a Kotlin model
  cpp                                 export C-family compiler commands
  typescript                          export web sources and Oro SDK typings
  swift                               export commands and prepare Swift modules
  doctor                              inspect editor setup and model freshness

Options:
  -h, --help                          show help

Documentation:
  man oroc-tooling
)TEXT";

constexpr auto gManualTextTooling = R"TEXT(
Notes:
  Use '{{cli_name}} tooling <command> --help' for model export and preparation options.

  Tooling models are JSON. Doctor defaults to readable text and also accepts --json.
)TEXT";

constexpr auto gHelpTextToolingKotlin = R"TEXT(
Export or prepare the Kotlin editor compilation model.

Usage:
  {{cli_name}} tooling kotlin [options] [<project-dir>]

Options:
  --prepare                           compile resources and JVM classes for
                                      analysis
  --variant=<name>                    Android variant (default devDebug)
  --refresh                           regenerate the Android editor project
  --offline                           resolve dependencies without network
                                      access
  --config=<path>                     use an explicit oro.toml or oro.ini
  --classpath                         print only the resolved editor classpath
  -h, --help                          show help

Documentation:
  man oroc-tooling-kotlin
)TEXT";

constexpr auto gManualTextToolingKotlin = R"TEXT(
Export and prepare Oro editor compilation models.

Options:
  --config=<path>                     explicit oro.toml or oro.ini

Notes:
  Prints the versioned JSON model by default. Uses Oro configuration and Gradle task providers.

  Preparation does not assemble, sign, install, or compile native libraries.

  Gradle, a compatible JDK, Android SDK packages and dependency artifacts must be installed.
)TEXT";

constexpr auto gHelpTextToolingCpp = R"TEXT(
Export C, C++, Objective-C and Objective-C++ compiler commands.

Usage:
  {{cli_name}} tooling cpp [options] [<project-dir>]

Options:
  --platform=<name>                   desktop, linux, mac, win, android, ios or
                                      ios-simulator
  --arch=<name>                       target architecture (host by default;
                                      arm64 for Android/iOS)
  --prod                              select release compiler flags
  --prepare                           build explicitly configured CMake
                                      preparation targets
  --refresh                           allocate a fresh editor generation
  --offline                           disable CMake FetchContent downloads and
                                      updates
  --config=<path>                     use an explicit oro.toml or oro.ini
  -h, --help                          show help

Documentation:
  man oroc-tooling-cpp
)TEXT";

constexpr auto gManualTextToolingCpp = R"TEXT(
Notes:
  Uses Oro extension declarations, an existing compilation database, or a configured CMake project.

  Generated commands target original source files and live under build.output/.oro-tooling.

  Apple SDK discovery requires macOS and Xcode; Android requires the selected NDK.

  Custom scripts and generated dependency headers require a build-system compilation database or CMake provider.

  See docs/LANGUAGE_SERVER.md for configuration and editor examples.
)TEXT";

constexpr auto gHelpTextToolingTypescript = R"TEXT(
Export web compiler context and selected Oro SDK declarations.

Usage:
  {{cli_name}} tooling typescript [options] [<project-dir>]

Options:
  --prepare                           materialize the editor config and
                                      TypeScript plugin
  --refresh                           allocate a fresh editor generation
  --offline                           use installed packages; disable automatic
                                      typings acquisition in LSP
  --config=<path>                     use an explicit oro.toml or oro.ini
  -h, --help                          show help

Documentation:
  man oroc-tooling-typescript
)TEXT";

constexpr auto gManualTextToolingTypescript = R"TEXT(
Notes:
  Uses the original tsconfig.json/jsconfig.json, including references, or explicit source declarations.

  Without a declared scope, tsserver infers projects from opened documents and their imports.

  Does not rewrite application configs, emit JavaScript, or install packages.

  The default TypeScript LSP adapter adds Oro declarations through an Oro-owned tsserver plugin.

  See docs/LANGUAGE_SERVER.md for configuration and editor examples.
)TEXT";

constexpr auto gHelpTextToolingSwift = R"TEXT(
Export Swift compiler commands and prepare analysis modules.

Usage:
  {{cli_name}} tooling swift [options] [<project-dir>]

Options:
  --platform=<name>                   desktop, mac, ios, ios-simulator, linux or
                                      win
  --arch=<name>                       target architecture
  --prod                              select release compiler flags
  --prepare                           emit the declared Swift module without
                                      linking or signing
  --refresh                           allocate a fresh editor generation
  --offline                           use installed toolchains and dependencies
                                      only
  --config=<path>                     use an explicit oro.toml or oro.ini
  -h, --help                          show help

Documentation:
  man oroc-tooling-swift
)TEXT";

constexpr auto gManualTextToolingSwift = R"TEXT(
Notes:
  Declare original sources and module settings under tooling.swift, or import an existing compilation database.

  Apple targets require macOS and Xcode. External build systems own their dependency preparation.

  Use language_server.swift.adapter="stdio" for native SwiftPM discovery.

  See docs/LANGUAGE_SERVER.md for configuration and editor examples.
)TEXT";

constexpr auto gHelpTextToolingDoctor = R"TEXT(
Inspect editor setup without preparing dependencies.

Usage:
  {{cli_name}} tooling doctor [options] [<project-dir>]

Options:
  --language=<name>                   inspect one language family; otherwise
                                      inspect declared languages
  --platform=<name>                   select the native target
  --arch=<name>                       select target architecture
  --variant=<name>                    select Android variant (default devDebug)
  --prod                              inspect release model
  --json                              emit the structured report instead of
                                      readable text
  --config=<path>                     use an explicit oro.toml or oro.ini
  -h, --help                          show help

Documentation:
  man oroc-tooling-doctor
)TEXT";

constexpr auto gManualTextToolingDoctor = R"TEXT(
Notes:
  Checks project root, selected SDK, server executables, required tools and cached model freshness.

  Does not install servers, generate models, run Gradle tasks or invoke preparation targets.

  Returns a nonzero status when a required check fails; stale or absent models are warnings.

  See docs/LANGUAGE_SERVER.md for configuration and editor examples.
)TEXT";

constexpr auto gHelpTextPush = R"TEXT(
Local UnifiedPush development tooling.

Usage:
  {{cli_name}} push <command> [options]

Commands:
  server                              run a local development server
  distributor                         build or install Oro Push Dev

Options:
  -h, --help                          show help

Documentation:
  man oroc-push
)TEXT";

constexpr auto gManualTextPush = R"TEXT(
Commands:
  server                              run a local ntfy server for an Android UnifiedPush distributor
  distributor                         build or install the Oro Kotlin development distributor APK

Notes:
  Run '{{cli_name}} push server --help' for configuration and HTTPS requirements.
)TEXT";

constexpr auto gHelpTextPushServer = R"TEXT(
Run a local UnifiedPush development server backed by ntfy.

Usage:
  {{cli_name}} push server [options]

Options:
  --host=<ip>                         listen address (default: 127.0.0.1)
  --port=<port>                       listen port (default: 8787)
  --base-url=<origin>                 server URL reachable by the distributor
  --data-dir=<path>                   writable cache, tools and message storage
                                      directory
  --cert-file=<path>                  HTTPS certificate (requires --key-file)
  --key-file=<path>                   HTTPS private key (requires --cert-file)
  --ntfy-bin=<path>                   use a local ntfy server executable
  --offline                           require the verified cached ntfy release
                                      or --ntfy-bin
  -h, --help                          show help

Documentation:
  man oroc-push-server
)TEXT";

constexpr auto gManualTextPushServer = R"TEXT(
Options:
  --base-url=<origin>                 URL reachable by the distributor and application server
  --cert-file=<path>                  PEM certificate for direct HTTPS (requires --key-file)
  --key-file=<path>                   PEM private key for direct HTTPS (requires --cert-file)
  --ntfy-bin=<path>                   use an explicitly supplied ntfy executable without download

Notes:
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

Examples:
  {{cli_name}} push server
                                      start at http://127.0.0.1:8787
  {{cli_name}} push server --offline
                                      restart from the verified cached release
  {{cli_name}} push server --host=0.0.0.0 --base-url=https://push.dev.example:8787 --cert-file=./cert.pem --key-file=./key.pem
                                      serve HTTPS on port 8787 for an Android distributor
  {{cli_name}} push server --base-url=https://push.dev.example
                                      run loopback HTTP behind a local HTTPS reverse proxy
)TEXT";

constexpr auto gHelpTextPushDistributor = R"TEXT(
Run the Linux development distributor, or build the Oro Push Dev Android distributor.

Usage:
  {{cli_name}} push distributor [options]

Options:
  --platform=<android|linux>          Android APK build (default), or Linux service
  --allow-loopback-http               Linux only: allow literal loopback HTTP
  --android-home=<path>               Android SDK directory
  --gradle-home=<path>                Gradle 9 installation directory
  --java-home=<path>                  JDK installation directory
  --data-dir=<path>                   writable build state and persistent
                                      development signing key
  --output=<path>                     APK destination (default: data
                                      directory/oro-push-dev.apk)
  --server=<origin>                   prefill the distributor HTTPS server
  --ca-cert=<path>                    embed a development PEM CA, trusted only
                                      by this APK
  --offline                           build with cached Gradle/Android
                                      dependencies only
  --install                           install APK and open its settings on the
                                      selected device
  --device=<serial>                   explicit adb serial for installation (or
                                      ANDROID_SERIAL)
  -h, --help                          show help

Documentation:
  man oroc-push-distributor
)TEXT";

constexpr auto gManualTextPushDistributor = R"TEXT(
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

Options:
  --android-home=<path>               Android SDK (defaults to ANDROID_HOME or ANDROID_SDK_ROOT)
  --gradle-home=<path>                Gradle 9 installation (defaults to GRADLE_HOME)
  --java-home=<path>                  JDK installation (defaults to JAVA_HOME)
  --server=<origin>                   prefill an HTTPS server in the app; user taps Save and start

Notes:
  Requires Node.js 22+, JDK 17+, Gradle 9, an installed Android platform 34 or newer, and build tools.

  Run '{{cli_name}} setup --platform=android' to provision the normal Android toolchain first.

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

Examples:
  {{cli_name}} push distributor --platform=linux --server=http://127.0.0.1:8787 --allow-loopback-http
                                      run alongside a local 'push server'
  {{cli_name}} push distributor --offline
                                      build from the cached Android/Kotlin toolchain
  {{cli_name}} push distributor --server=https://push.dev.example:8787 --ca-cert=./dev-ca.pem --install --device=emulator-5554
                                      build, install, then review and start Oro Push Dev
)TEXT";

constexpr auto gHelpTextHelp = R"TEXT(
Find commands, options, and documentation.

Usage:
  {{cli_name}} help [query...]
  {{cli_name}} help [query...] [--json]

Options:
  --json                              print structured JSON results
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-help
)TEXT";

constexpr auto gManualTextHelp = R"TEXT(
Discover commands, options, and workflow entry points from the CLI help index.

Options:
  --json                              emit structured search results for automation and editor tooling

Notes:
  With no query, this subcommand prints the top-level CLI help.

  Exact command matches print concise command help (for example, '{{cli_name}} help build' or '{{cli_name}} help update validate').

  Free-form queries search command names, descriptions, options, notes, and examples, then rank the closest matches.

  Use this for discovery topics such as '{{cli_name}} help tls', '{{cli_name}} help ios signing', '{{cli_name}} help json logs', or '{{cli_name}} help agent tooling'.
  '{{cli_name}} help --json <query>' returns a machine-readable result list with ranked matches and exact-help payloads.

Examples:
  {{cli_name}} help build
                                      print usage and options for the build command
  {{cli_name}} help update validate
                                      print usage and options for the nested update validate command
  {{cli_name}} help ios signing
                                      search for commands and notes related to iOS signing and provisioning
  {{cli_name}} help json --json
                                      return ranked JSON results for the query "json"
)TEXT";

constexpr auto gHelpTextUpdate = R"TEXT(
Update tooling for manifests, signatures, and bundles.

Usage:
  {{cli_name}} update <subcommand> [options]

Commands:
  keygen                              generate an Ed25519 keypair for update
                                      manifests
  init                                create a project
  server                              serve updates over HTTP, TCP, or UDP
  info                                query a server or static manifest
  sign                                sign an update manifest
  verify                              verify an update manifest signature
  bundle                              create an update archive
  extract                             extract an update archive
  validate                            validate an update manifest

Options:
  -h, --help                          show help

Documentation:
  man oroc-update
)TEXT";

constexpr auto gManualTextUpdate = R"TEXT(
Commands:
  init                                scaffold a manifest JSON file
  server                              run an update server over HTTP/TCP/UDP
  info                                query update servers or static manifests over HTTP/TCP/UDP
  sign                                sign a manifest JSON file and emit a sidecar signature
  verify                              verify a manifest JSON file + signature using a public key
  bundle                              build a tar archive from a directory suitable as an update artifact
  extract                             extract an update bundle tar archive into a directory
  validate                            validate a manifest.json file against the update manifest schema shape

Notes:
  All subcommands support '--log-file=<path>' to mirror logs to a JSON file.

  Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' on relevant subcommands
  to override the default manifest.json filename (and derived sidecar names).

Examples:
  {{cli_name}} update init
                                      scaffold a default manifest.json in the current project
  {{cli_name}} update bundle --manifest manifest.json
                                      build a source bundle and record it in the manifest
  {{cli_name}} update keygen > key.json
                                      generate a signing keypair JSON file
  {{cli_name}} update sign --keys key.json --manifest manifest.json
                                      sign the manifest and write manifest.sig
  {{cli_name}} update verify --keys key.json --manifest manifest.json
                                      verify manifest.json against manifest.sig with the public key
  {{cli_name}} update server --root ./updates
                                      run an HTTP update server serving manifests and bundles
  {{cli_name}} update info --http --app-id com.example.app --follow-manifest
                                      query an update server and fetch the referenced manifest
)TEXT";

constexpr auto gHelpTextUpdateInit = R"TEXT(
Scaffold a minimal update manifest JSON file.

Usage:
  {{cli_name}} update init [options]

Options:
  --config=<path>                     use an explicit oro.toml or oro.ini
  --manifest-name=<name>              override the manifest filename
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-init
)TEXT";

constexpr auto gManualTextUpdateInit = R"TEXT(
Options:
  --config=<path>                     use an explicit oro.toml/oro.ini file when deriving defaults
  --manifest-name=<name>              filename for the manifest JSON (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME)

Notes:
  The generated manifest includes these defaults.
    - schemaVersion = 1
    - appId derived from your oro.toml ([meta] bundle_identifier, falling back to "com.example.app")
    - generatedAt = current UTC timestamp
    - channels = [update_channel or "stable"]
    - updates = a single entry for the current version/channel (with an empty targets array)
  Edit the generated file to add real targets, artifact metadata, or additional updates.

Examples:
  {{cli_name}} update init
                                      create ./manifest.json using oro.toml metadata
  {{cli_name}} update init --manifest-name app-updates.json
                                      create ./app-updates.json instead of manifest.json
)TEXT";

constexpr auto gHelpTextUpdateServer = R"TEXT(
Run an update server that speaks the Oro Application Update Protocol.

Usage:
  {{cli_name}} update server [options]

Options:
  --root=<dir>                        directory containing manifest trees and
                                      artifacts to serve
  --host=<host>                       interface to bind (default: 0.0.0.0)
  --port=<port>                       TCP/UDP port to bind (default: 8080)
  --manifest-name=<name>              override the manifest filename
  --tcp                               run in TCP mode (binary OUP
                                      CHECK/RESPONSE)
  --udp                               run in UDP mode (binary OUP
                                      CHECK/RESPONSE)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-server
)TEXT";

constexpr auto gManualTextUpdateServer = R"TEXT(
Options:
  --manifest-name=<name>              manifest filename to look up under each appId (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME)

Notes:
  Default mode is HTTP. The server exposes these endpoints.
    - GET /health           — readiness metadata
    - POST /check           — accepts a CHECK JSON payload with "appId" and responds with a RESPONSE JSON
                               whose manifestUrl points at "/<appId>/<manifest-name>" when present
    - GET /<path>           — serves files rooted under --root, including "<appId>/<manifest-name>"
                               and "<appId>/<manifest-name>.sig"
  HTTP mode is designed to be run behind a load balancer or reverse proxy in production.

  TCP and UDP modes implement the same CHECK/RESPONSE selection semantics using the binary OUP framing.

Examples:
  {{cli_name}} update server --root ./updates
                                      serve manifests and bundles over HTTP on port 8080
  {{cli_name}} update server --root ./updates --tcp --port 9090
                                      run a TCP OUP server on port 9090
  {{cli_name}} update server --root ./updates --udp --port 9090
                                      run a UDP OUP server on port 9090
)TEXT";

constexpr auto gHelpTextUpdateInfo = R"TEXT(
Query update servers or static manifests over HTTP/TCP/UDP.

Usage:
  {{cli_name}} update info [--transport=<http|tcp|udp>] [options]

Options:
  --config=<path>                     use an explicit oro.toml or oro.ini
  --transport=<http|tcp|udp>          transport to use (default: http)
  --http                              shorthand for --transport=http
  --tcp                               shorthand for --transport=tcp
  --udp                               shorthand for --transport=udp
  --follow-manifest                   fetch and validate the returned manifest
  --timeout-ms=<ms>                   TCP/UDP timeout (default: 0, no timeout)
  --manifest-url=<url>                HTTP(S) URL of a manifest.json hosted
                                      statically
  --signature-url=<url>               signature URL (default: from manifest URL)
  --keys=<file>                       public key JSON file
  --public-key=<hex>                  Ed25519 public key in hexadecimal
  --host=<host>                       host for HTTP/TCP/UDP update servers
                                      (default: 127.0.0.1)
  --port=<port>                       port for HTTP/TCP/UDP update servers
                                      (default: 8080)
  --app-id=<id>                       application ID (default: active config)
  --channel=<name>                    update channel hint (default: active
                                      config update_channel or "stable")
  --current-version=<version>         app version (default: active config)
  --runtime-version=<version>         runtime version hint for the server
  --platform=<id>                     platform hint advertised in CHECK
                                      (optional)
  --arch=<id>                         architecture hint advertised in CHECK
                                      (optional)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-info
)TEXT";

constexpr auto gManualTextUpdateInfo = R"TEXT(
Options:
  --config=<path>                     use an explicit oro.toml/oro.ini file for local CHECK request defaults
  --follow-manifest                   when contacting servers, follow 'manifestUrl' in the RESPONSE and fetch/validate that manifest over HTTP(S)
  --timeout-ms=<ms>                   optional timeout for TCP/UDP CHECK requests (0 = no timeout)
  --signature-url=<url>               optional HTTP(S) URL of the corresponding signature JSON (default: derived from --manifest-url)
  --keys=<file>                       JSON file containing a public key ("publicKey" or "key" field) for manifest verification
  --public-key=<hex>                  Ed25519 public key as a hex string for manifest verification
  --app-id=<id>                       application identifier to send in CHECK messages (default: active config meta.bundle_identifier when available)
  --current-version=<version>         current application version hint (default: active config meta.version when available)
  --runtime-version=<version>         runtime version hint advertised in CHECK (optional)

Notes:
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

Examples:
  {{cli_name}} update info --manifest-url https://cdn.example.com/app/manifest.json
                                      inspect a statically hosted manifest
  {{cli_name}} update info --manifest-url https://cdn.example.com/app/manifest.json --keys app-pubkey.json
                                      fetch and verify a statically hosted manifest + signature
  {{cli_name}} update info --http --host 127.0.0.1 --port 8080 --app-id com.example.app --follow-manifest
                                      query an HTTP update server and then fetch the referenced manifest
  {{cli_name}} update info --tcp --host 127.0.0.1 --port 9000 --app-id com.example.app --follow-manifest
                                      query a TCP update server using the binary OUP protocol
)TEXT";

constexpr auto gHelpTextUpdateKeygen = R"TEXT(
Generate an Ed25519 keypair for signing update manifests.

Usage:
  {{cli_name}} update keygen [options]

Options:
  --out=<path>                        write keypair JSON to a file instead of
                                      stdout
  --key-id=<id>                       signing key identifier (default: pk-1)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-keygen
)TEXT";

constexpr auto gManualTextUpdateKeygen = R"TEXT(
Options:
  --key-id=<id>                       optional key identifier to embed in the keypair (default: "pk-1")

Notes:
  The generated JSON includes "keyId", "publicKey", and "privateKey" fields (hex-encoded).

  Keep the private key secret; distribute only the public key with your application.

Examples:
  {{cli_name}} update keygen > key.json
                                      generate a default keypair and save it to key.json
  {{cli_name}} update keygen --key-id pk-prod --out prod-key.json
                                      generate a named keypair for production use
)TEXT";

constexpr auto gHelpTextUpdateSign = R"TEXT(
Sign an update manifest and emit a detached manifest.sig file.

Usage:
  {{cli_name}} update sign (--keys=<file> | --private-key=<hex>) [options]

Options:
  --manifest=<path>                   manifest JSON (default: manifest.json)
  --manifest-name=<name>              override the manifest filename
  --keys=<file>                       signing key JSON file
  --private-key=<hex>                 Ed25519 private key in hexadecimal
  --key-id=<id>                       signing key identifier (default: pk-1)
  --out=<path>                        signature output (default: from manifest)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-sign
)TEXT";

constexpr auto gManualTextUpdateSign = R"TEXT(
Options:
  --manifest=<path>                   path to the manifest JSON file to sign (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME)
  --manifest-name=<name>              manifest filename to use when --manifest is not provided
  --keys=<file>                       JSON file containing a signing key (with "privateKey" or "secretKey" field)
  --private-key=<hex>                 Ed25519 private key as a hex string
  --key-id=<id>                       optional key identifier to embed in manifest.sig (default: "pk-1")
  --out=<path>                        output path for manifest.sig (default: <manifest-without-extension>.sig, e.g. manifest.json -> manifest.sig)

Notes:
  The signature file is JSON containing "schemaVersion", "algorithm", "keyId", and "signature" fields.

  Clients verify manifest bytes against manifest.sig and the configured public key(s).

  Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename.

Examples:
  {{cli_name}} update sign --keys key.json --manifest manifest.json
                                      sign manifest.json using the private key in key.json
  {{cli_name}} update sign --private-key <hex-private-key> --manifest manifest.json --out manifest.sig
                                      sign a manifest using a raw hex private key
)TEXT";

constexpr auto gHelpTextUpdateVerify = R"TEXT(
Verify a manifest + signature pair using an Ed25519 public key.

Usage:
  {{cli_name}} update verify (--keys=<file> | --public-key=<hex>) [options]

Options:
  --manifest=<path>                   manifest JSON (default: manifest.json)
  --manifest-name=<name>              override the manifest filename
  --signature=<path>                  signature JSON (default: from manifest)
  --keys=<file>                       public key JSON file
  --public-key=<hex>                  Ed25519 public key in hexadecimal
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-verify
)TEXT";

constexpr auto gManualTextUpdateVerify = R"TEXT(
Options:
  --manifest=<path>                   path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME)
  --manifest-name=<name>              manifest filename to use when --manifest is not provided
  --signature=<path>                  path to the signature JSON file (default: <manifest-without-extension>.sig)
  --keys=<file>                       JSON file containing a public key ("publicKey" or "key" field)
  --public-key=<hex>                  Ed25519 public key as a hex string

Notes:
  Exits with status 0 when the signature is valid for the manifest and public key; non-zero otherwise.

  Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename
  (the default signature path is derived as '<manifest-without-extension>.sig', e.g. manifest.json -> manifest.sig).

Examples:
  {{cli_name}} update verify --keys key.json --manifest manifest.json
                                      verify manifest.json against manifest.sig using the public key in key.json
  {{cli_name}} update verify --public-key <hex-public-key> --manifest manifest.json --signature manifest.sig
                                      verify using an explicit hex-encoded public key and signature file
)TEXT";

constexpr auto gHelpTextUpdateValidate = R"TEXT(
Validate an update manifest JSON file against the expected schema shape.

Usage:
  {{cli_name}} update validate [--manifest=<path>] [options]

Options:
  --manifest=<path>                   manifest JSON (default: manifest.json)
  --manifest-name=<name>              override the manifest filename
  --strict                            enable additional consistency checks
  --json                              print a machine-readable JSON result
                                      object (for CI)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-validate
)TEXT";

constexpr auto gManualTextUpdateValidate = R"TEXT(
Options:
  --manifest=<path>                   path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME)
  --manifest-name=<name>              manifest filename to use when --manifest is not provided
  --strict                            enable additional consistency checks (for example, channels vs updates, artifactUrl shape)

Notes:
  This command parses the manifest and performs lightweight structural validation aligned with
  'schemas/update-manifest.schema.json' (required fields, types, and key relationships).

  It does not attempt full JSON Schema validation, but is suitable for fast local checks and CI.

  When '--strict' is provided, additional consistency rules are enforced.

  When '--json' is provided, stdout is reserved for the result object only.

Examples:
  {{cli_name}} update validate --manifest manifest.json
                                      run basic structural checks against manifest.json
  {{cli_name}} update validate --manifest manifest.json --strict
                                      enable stricter consistency rules in addition to structural checks
  {{cli_name}} update validate --manifest manifest.json --json
                                      print validation status as JSON for CI or agents
)TEXT";

constexpr auto gHelpTextUpdateBundle = R"TEXT(
Create a tar archive for an application update.

Usage:
  {{cli_name}} update bundle [--input=<dir>] [--output=<bundle.tar>] [options]

Options:
  --input=<dir>                       directory whose contents will be archived
                                      (default: project directory)
  --output=<bundle.tar>               path to the tar archive to write (default:
                                      <build_name>-<version>.tar)
  --manifest=<path>                   update the manifest with this bundle
  --manifest-name=<name>              override the manifest filename
  --channel=<name>                    update channel to associate with this
                                      bundle (default: update_channel or
                                      "stable")
  --update-id=<id>                    update id to associate with this bundle
                                      (default: <channel>-<version>)
  --platform=<id>                     platform identifier for the bundle target
                                      (default: "source")
  --arch=<id>                         architecture identifier for the bundle
                                      target (default: "any")
  --artifact-url=<url-or-path>        artifact URL recorded in the manifest
  --hash-algorithm=<sha256|sha1>      bundle hash algorithm
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-bundle
)TEXT";

constexpr auto gManualTextUpdateBundle = R"TEXT(
Build a tar archive containing the contents of a directory for use as an update artifact.

Options:
  --manifest=<path>                   optional manifest path to update with a new target for this bundle
  --manifest-name=<name>              manifest filename to use when --manifest is not provided
  --artifact-url=<url-or-path>        artifactUrl to record in the manifest target (default: bundle filename)
  --hash-algorithm=<sha256|sha1>      hash algorithm to use for the bundle payload (default: sha256 when libsodium is available, otherwise sha1)

Notes:
  The archive is a plain tar file (no compression) built using the runtime's native tar implementation.

  Directory layout and basic metadata (mode bits, mtime) are preserved.

  When omitted, '--input' defaults to the project directory (app source),
  and '--output' defaults to "<build_name>-<version>.tar" derived from your oro.toml metadata.

  When '--manifest' or '--manifest-name' (or ORO_UPDATE_MANIFEST_FILENAME) is provided, the manifest is updated
  with a new target entry describing this bundle (including length and hash).

Examples:
  {{cli_name}} update bundle
                                      bundle the current project source into <build_name>-<version>.tar
  {{cli_name}} update bundle --manifest manifest.json
                                      bundle the project and record the artifact in manifest.json
  {{cli_name}} update bundle --input dist --output app-1.2.3.tar --manifest manifest.json --channel beta
                                      bundle a custom directory and attach it as a beta update in the manifest
)TEXT";

constexpr auto gHelpTextUpdateExtract = R"TEXT(
Extract an update tar archive into a destination directory.

Usage:
  {{cli_name}} update extract --bundle=<bundle.tar> --dest=<dir> [options]

Options:
  --bundle=<bundle.tar>               path to the tar archive to extract
  --dest=<dir>                        destination directory to extract files
                                      into (created if missing)
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-update-extract
)TEXT";

constexpr auto gManualTextUpdateExtract = R"TEXT(
Notes:
  The extractor rejects absolute paths and any paths containing ".." or ':' to avoid directory traversal.

  Special tar entries (symlinks, devices, etc.) are ignored; regular files and directories are restored.

Examples:
  {{cli_name}} update extract --bundle app-1.0.0.tar --dest ./update-staging
                                      extract the contents of app-1.0.0.tar into ./update-staging
)TEXT";

constexpr auto gHelpTextBuild = R"TEXT(
Build an Oro application.

Usage:
  {{cli_name}} build [options] [<project-or-source>]

Options:
  --platform=<platform>               target platform (default: host)
  --config=<path>                     use an explicit oro.toml or oro.ini
  --device=<serial>                   Android device to install and run on
  --copy=<source:dest>                extra bundle mapping (repeatable)
  --port=<port>                       development server port (default: 80)
  --host=<host>                       development server host (default:
                                      localhost)
  -t, --test=<path>                   run a bundled test entry
  -H, --headless                      run without a visible window
  -P, --prod                          use production build settings
  -D, --debug                         enable debug mode
  -E, --env=<name[=value]>            add environment variables
  -o, --only-build                    only run the build step
  -p, --package                       package the app for distribution
  -q, --quiet                         hint for less log output
  -r, --run                           run after building
  -V, --verbose                       enable verbose output
  -w, --watch                         watch for changes to rerun build step
  --allow-exec                        allow native extension build commands
  --sanitizers                        enable desktop ASan/UBSan sanitizers
  --cache                             enable all supported build result caches
                                      (default: off)
  --no-cache                          disable supported build result caches for
                                      this build
  --compiler-cache                    enable ccache/sccache for native object
                                      compilation
  --no-compiler-cache                 disable the native compiler cache
  --android-cache                     enable all supported caches for Android
                                      builds
  --no-android-cache                  disable supported caches for Android
                                      builds
  --android-build-cache               enable Gradle's task output cache
  --no-android-build-cache            disable Gradle's task output cache
  --android-configuration-cache       enable Gradle's configuration cache
  --no-android-configuration-cache    disable Gradle's configuration cache
  --tls-keylog=<path>                 write NSS TLS keys (OpenSSL provider)
  --log-file=<path>                   mirror logs to a JSON file
  --editor-project=<dir>              export an Android editor project
  --remote-debugging-port=<port>      set the remote debugging port
  -h, --help                          show help

Linux options:
  -f, --package-format=<format>       package format: deb (default), rpm, zip,
                                      aur
  --sign                              sign Linux packages with GPG (writes .asc
                                      next to the artifact)
  --sign-key=<id>                     GPG key ID or fingerprint for --sign

macOS options:
  -c, --codesign                      code sign application with 'codesign'
  -n, --notarize                      notarize application with 'notarytool'
  -f, --package-format=<format>       package format: zip (default), pkg

iOS options:
  -c, --codesign                      sign with the configured iOS profile

Windows options:
  -f, --package-format=<format>       package format: appx

Documentation:
  man oroc-build
)TEXT";

constexpr auto gManualTextBuild = R"TEXT(
Build Oro application.

Provide a project directory, HTML file, or JavaScript module. When no
oro.toml is found, {{cli_name}} infers a minimal configuration automatically.

Options:
  --platform=<platform>               platform target to build application for (defaults to host):
                                      - android
                                      - android-emulator
                                      - ios
                                      - ios-simulator
  --config=<path>                     use an explicit oro.toml/oro.ini file
  --copy=<source:dest>                extra copy mapping (like [build] copy; can be repeated)
  --port=<port>                       load "index.html" from a specific port (if host is not specified, defaults to localhost)
  --host=<host>                       load "index.html" from a specific host (if port is not specified, defaults to 80)
  -t, --test=<path>                   enable test mode and import a test file relative to resource files
  -H, --headless                      build application to run in headless mode (without frame or window)
  -P, --prod                          build for production (disables debugging info, inspector, etc.)
  -o, --only-build                    only run the build step
  --allow-exec                        allow native extension commands (git/build scripts)
  --sanitizers                        enable developer sanitizers (ASan/UBSan) for desktop core builds
  --tls-keylog=<path>                 write NSS TLS key log lines to <path> (OpenSSL provider)

Environment:
  ORO_ALLOW_EXEC                      allow native extension commands during builds
  ORO_ENABLE_SANITIZERS               enable ASan/UBSan on desktop builds

Notes:
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

Common errors:
  Android builds: run `{{cli_name}} setup --platform=android` and accept SDK licenses.
  macOS/iOS signing: set `[ios] provisioning_profile`/specifier or `[mac.productbuild] identity` in your config file.

  Standard Android builds run NDK and Gradle automatically. Native extension commands may require `--allow-exec` or `ORO_ALLOW_EXEC=1`.

Linux options:
  -f, --package-format=<format>       package a Linux application in a specified format for distribution:
                                      - deb (default)
                                      - rpm
                                      - zip
                                      - aur (generate AUR PKGBUILD only)
  --sign-key=<id>                     optional GPG key ID or fingerprint to use with '--sign' (letters/digits/@._- only)

Linux dependencies:
  deb packaging requires `dpkg` and `fakeroot` (e.g., `sudo apt-get install dpkg-dev fakeroot`).
  rpm packaging requires `rpmbuild` (e.g., `sudo dnf install rpm-build`).

macOS options:
  -f, --package-format=<format>       package a macOS application in a specified format for distribution:
                                      - zip (default)
                                      - pkg

macOS dependencies:
  Xcode and Command Line Tools are required (`xcode-select --install`).
  For Gradle/JDK: `brew install gradle openjdk` (or use SDKMAN).

iOS options:
  -c, --codesign                      code sign application during xcodebuild (requires '[ios] provisioning_profile' in your config file)

Windows options:
  -f, --package-format=<format>       package a Windows application in a specified format for distribution:
                                      - appx (default)

Windows dependencies:
  Windows 10/11 SDK and Visual Studio Build Tools are recommended.
  Ensure `signtool.exe` is available (set SIGNTOOL or add SDK bin to PATH).

Examples:
  {{cli_name}} build .
                                      build the current project for the host platform
  {{cli_name}} build -r --debug .
                                      rebuild and immediately run a debug build
  {{cli_name}} build --platform=ios --prod --package .
                                      produce a signed/package-ready iOS build using your config
  {{cli_name}} build --copy assets:assets .
                                      include an extra assets directory in the bundle for this build only
  {{cli_name}} build --platform=android --cache --no-android-configuration-cache .
                                      enable supported caches except Gradle configuration caching
)TEXT";

constexpr auto gHelpTextRun = R"TEXT(
Run an Oro application.

Usage:
  {{cli_name}} run [options] [<project-or-source>]

Options:
  -H, --headless                      run without a visible window
  --platform=<platform>               target platform (default: host)
  --config=<path>                     use an explicit oro.toml or oro.ini
  --device=<serial>                   Android device to install and run on
  --port=<port>                       development server port (default: 80)
  --host=<host>                       development server host (default:
                                      localhost)
  -P, --prod                          use production build settings
  -t, --test=<path>                   run a bundled test entry
  -D, --debug                         enable debug mode
  -q, --quiet                         hint for less log output
  -E, --env=<name[=value]>            add environment variables
  -V, --verbose                       enable verbose output
  --allow-exec                        allow native extension build commands
  --tls-keylog=<path>                 write NSS TLS keys (OpenSSL provider)
  --log-file=<path>                   mirror logs to a JSON file
  --remote-debugging-port=<port>      set the remote debugging port
  --sanitizers                        enable desktop ASan/UBSan sanitizers
  -h, --help                          show help

Documentation:
  man oroc-run
)TEXT";

constexpr auto gManualTextRun = R"TEXT(
Run application.

Provide a project directory, HTML file, or JavaScript module. When no
oro.toml is found, {{cli_name}} infers a minimal configuration automatically.

Options:
  -H, --headless                      run application in headless mode (without frame or window)
  --platform=<platform>               platform target to run application on (defaults to host):
                                      - android
                                      - android-emulator
                                      - ios
                                      - ios-simulator
  --config=<path>                     use an explicit oro.toml/oro.ini file
  --port=<port>                       load "index.html" from a specific port (if host is not specified, defaults to localhost)
  --host=<host>                       load "index.html" from a specific host (if port is not specified, defaults to 80)
  -P, --prod                          build for production (disables debugging info, inspector, etc.)
  -t, --test=<path>                   enable test mode and import a test file relative to resource files
  --allow-exec                        allow native extension commands (git/build scripts)
  --tls-keylog=<path>                 write NSS TLS key log lines to <path> (OpenSSL provider)

Environment:
  ORO_DEBUG                           enable debug mode (like -D)
  ORO_VERBOSE                         enable verbose logs (like -V)

Notes:
  Android targets use --device, then ANDROID_SERIAL, then the first ready device
  listed by `adb devices`. The android-emulator target selects the first ready emulator.

Common errors:
  For CI/Linux headless, install `xvfb-run` or set a custom headless runner in your config.

  Use `--test=path` to run tests bundled with your app.

Examples:
  {{cli_name}} run .
                                      build and launch the current project on the host platform
  {{cli_name}} run --platform=android-emulator .
                                      build and launch the current project on the Android emulator target
  {{cli_name}} run --headless --test=tests/smoke.js .
                                      execute a bundled test entrypoint in headless mode
)TEXT";

constexpr auto gHelpTextListDevices = R"TEXT(
Get the list of connected devices.

Usage:
  {{cli_name}} list-devices [options] --platform=<platform>
  {{cli_name}} devices [options] --platform=<platform>

Options:
  --platform=<platform>               device platform: android or ios (required)
  -f, --format=<format>               print as 'text' or 'json'
  --json                              shorthand for '--format=json'
  --ecid                              show device ECID (ios only)
  --udid                              show device UDID (ios only)
  --only                              only show ECID or UDID of the first device
                                      (ios only)
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-list-devices
)TEXT";

constexpr auto gManualTextListDevices = R"TEXT(
Options:
  --platform=<platform>               platform target to list devices for:
                                      - android
                                      - ios

Notes:
  Android output mirrors the device identifiers returned by `adb devices`.
  iOS output can include the human-readable simulator/device name plus ECID or UDID for scripting.

  JSON output emits an array of device objects with stable id fields for automation.

Examples:
  {{cli_name}} list-devices --platform=android
                                      list attached Android devices visible to adb
  {{cli_name}} list-devices --platform=ios --udid
                                      print iOS devices with UDID values you can pass to --device
  {{cli_name}} list-devices --platform=ios --json
                                      print connected iOS devices as JSON for scripts or agents
)TEXT";

constexpr auto gHelpTextEnv = R"TEXT(
Print environment variables relevant to the Oro CLI and build configuration.

Usage:
  {{cli_name}} env [options]

Options:
  -f, --format=<format>               print as 'text' or 'json'
  --json                              shorthand for '--format=json'
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-env
)TEXT";

constexpr auto gManualTextEnv = R"TEXT(
Notes:
  - Prints a curated set of CLI, runtime, source-bootstrap, toolchain, and platform variables (for example, ORO_DEBUG, ORO_VERBOSE, ORO_HOME, NO_ANDROID, NO_IOS, JAVA_HOME, ANDROID_HOME, APPLE_ID, SIGNTOOL).
  - NO_ANDROID and NO_IOS are independent presence flags for runtime source bootstrap. A non-empty
    NO_ANDROID disables only Android work; a non-empty NO_IOS disables only iOS/iOS Simulator work
    on macOS. Values such as 0 and false still disable the named target. They do not select the
    application target for `{{cli_name}} build --platform`.
  - Merges [env] / env_* entries from the active configuration and from local .ororc files when present.
  - Filters out unset variables; text output prints each line as KEY=VALUE.
  - JSON output emits a flat object keyed by environment variable name.

Examples:
  {{cli_name}} env
                                      print the effective environment inputs visible to Oro tooling
  {{cli_name}} env --json
                                      print the effective environment inputs as JSON
)TEXT";

constexpr auto gHelpTextMcp = R"TEXT(
Run a Model Context Protocol (MCP) server for agent tooling.

Usage:
  {{cli_name}} mcp [options] [<workspace-dir>]

Options:
  --stdio                             use stdio transport (default)
  --http                              use HTTP/SSE transport
  --host=<host>                       HTTP bind host (default: 127.0.0.1)
  --port=<port>                       HTTP bind port (default: 0 for ephemeral)
  --endpoint=<path>                   HTTP endpoint path (default: /mcp)
  --token=<token>                     HTTP bearer token (default: generated)
  --no-auth                           disable token auth (loopback only)
  --workspace=<path>                  workspace root (default: current
                                      directory)
  --config=<path>                     use an explicit oro.toml or oro.ini
  --read-workspace-only               limit reads to the workspace (default)
  --allow-read-outside-workspace      allow reads outside the workspace
  --replace-sse-stream                allow replacement of a session SSE stream
  -h, --help                          show help

Documentation:
  man oroc-mcp
)TEXT";

constexpr auto gManualTextMcp = R"TEXT(
Run a Model Context Protocol (MCP) server for agent tooling.

By default this subcommand speaks JSON-RPC over stdio (stdout is reserved for MCP messages).
Use --http to run an HTTP/SSE transport.

Options:
  --endpoint=<path>                   HTTP endpoint path (default: /mcp; normalizes mcp,/mcp,/mcp/)
  --token=<token>                     require this bearer token (default: auto-generated in HTTP mode)
  --workspace=<path>                  explicit workspace root (default: current directory or <workspace-dir>)
  --config=<path>                     workspace-relative oro.toml/oro.ini path to use as the active project config
  --read-workspace-only               restrict filesystem reads to the workspace root (default; compatibility flag)
  --allow-read-outside-workspace      explicitly allow reading arbitrary files outside the workspace root
  --replace-sse-stream                allow a new SSE connection to replace an existing one for the same session id

Notes:
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

Examples:
  {{cli_name}} mcp --stdio .
                                      start an MCP server over stdio for the current workspace
  {{cli_name}} mcp --http --host=127.0.0.1 --port=8080 .
                                      expose the MCP server over local HTTP/SSE
)TEXT";

constexpr auto gHelpTextInit = R"TEXT(
Create a project in the given directory (default: current directory).

Usage:
  {{cli_name}} init [options] [<project-dir>]

Options:
  -C, --config                        only create the config file
  -n, --name=<name>                   project name
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-init
)TEXT";

constexpr auto gManualTextInit = R"TEXT(
Create a new project. If the path is not provided, the new project will be created in the current directory.

Notes:
  By default this command creates a starter `oro.toml`, `src/index.html`, `src/index.js`,
  `src/serviceworker.js`, and `.gitignore`.

  Use `--config` when you only want the config scaffold without the example app files.

Examples:
  {{cli_name}} init my-app
                                      create a new starter project in ./my-app
  {{cli_name}} init --config
                                      write only the default oro.toml into the current directory
)TEXT";

constexpr auto gHelpTextInstallApp = R"TEXT(
Install the app to the device or host target.

Usage:
  {{cli_name}} install-app [options] [<project-dir>]
  {{cli_name}} install [options] [<project-dir>]

Options:
  -D, --debug                         enable debug output
  --device=<identifier>               device identifier for installation
  --platform=<platform>               target platform (default: host)
  -P, --prod                          install production application
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

macOS options:
  --target=<target>                   macOS installation root (default: /)

Documentation:
  man oroc-install-app
)TEXT";

constexpr auto gManualTextInstallApp = R"TEXT(
Options:
  --device=<identifier>               identifier (ecid, ID) of the device to install to Android defaults to ANDROID_SERIAL or the first ready device
  --platform=<platform>               platform to install application to device (defaults to host):
                                      - android
                                      - ios

macOS options:
  --target=<target>                   installation target for macOS application (defaults to '/') the application is installed into '$target/Applications'

Notes:
  Android installs use the first ready device listed by `adb devices` when neither
  --device nor ANDROID_SERIAL is set. Offline and unauthorized devices are skipped.

  The android-emulator target selects the first ready emulator.

Common errors:
  Android: list devices with `adb devices` or pass `--device`.
  iOS/macOS: list devices with `{{cli_name}} list-devices --platform=ios` and pass `--device`.

Examples:
  {{cli_name}} install-app --platform=android --device emulator-5554
                                      install the current Android build onto a specific emulator
  {{cli_name}} install-app --platform=ios --device <udid>
                                      install the current iOS build onto a specific simulator or device
)TEXT";

constexpr auto gHelpTextPrintBuildDir = R"TEXT(
Print the build directory path.

Usage:
  {{cli_name}} print-build-dir [options] [<project-dir>]

Options:
  --platform=<platform>               target platform (default: host)
  -P, --prod                          indicate production build directory
  --root                              print the root build directory
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-print-build-dir
)TEXT";

constexpr auto gManualTextPrintBuildDir = R"TEXT(
Print the build directory path

Options:
  --platform=<platform>               platform to print build directory for (defaults to host):
                                      - android
                                      - android-emulator
                                      - ios
                                      - ios-simulator

Notes:
  This is useful for scripts that need to locate packaged artifacts, intermediate bundles, or
  platform-specific staging directories without reimplementing Oro's path logic.

Examples:
  {{cli_name}} print-build-dir .
                                      print the host-platform build directory for the current project
  {{cli_name}} print-build-dir --platform=ios --prod .
                                      print the production iOS build directory
)TEXT";

constexpr auto gHelpTextSetup = R"TEXT(
Setup build tools for host or target platform.

Usage:
  {{cli_name}} setup [options] [--platform=<platform>] [-y|--yes]

Options:
  --platform=<platform>               target platform (default: host)
  -q, --quiet                         hint for less log output
  -y, --yes                           answer yes to any prompts
  -D, --debug                         enable debug output
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-setup
)TEXT";

constexpr auto gManualTextSetup = R"TEXT(
Options:
  --platform=<platform>               platform target to run setup for (defaults to host):
                                      - android
                                      - ios
                                      - linux
                                      - windows

Common errors:
  Without `--platform`, setup defaults to the host. Verify with `{{cli_name}} env`.

Examples:
  {{cli_name}} setup --platform=android
                                      install or validate Android SDK/NDK requirements
  {{cli_name}} setup --platform=ios
                                      install or validate host tools needed for iOS/macOS builds
)TEXT";

constexpr auto gHelpTextConfig = R"TEXT(
Inspect configuration values.

Usage:
  {{cli_name}} config [options] [<key-or-path>]

Options:
  --config=<path>                     use an explicit oro.toml or oro.ini
  --list                              list known configuration keys with current
                                      and default values
  --key=<name>                        print the current value for a specific key
  --describe=<name>                   print help and metadata for a specific key
  -f, --format=<format>               print the full configuration as 'toml',
                                      'ini', or 'json'
  --json                              shorthand for '--format=json'
  --strict                            fail on unknown or unset keys
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-config
)TEXT";

constexpr auto gManualTextConfig = R"TEXT(
Options:
  --config=<path>                     use an explicit oro.toml/oro.ini file
  --strict                            treat unknown or unset keys as errors (non-zero exit)

Notes:
  Keys may be provided in flattened form (e.g., "filesystem_sandbox_enabled") or as TOML-style paths (e.g., "filesystem.sandbox_enabled").

  A bare argument after 'config' is treated as a key query (for example, '{{cli_name}} config filesystem.sandbox_enabled').

  Unknown keys are printed when present in the active configuration but are marked as undocumented.
  `--format` prints the merged effective configuration after applying oro.toml, local .ororc overrides,
  and CLI-selected config sources.

  Use '--log-file=<path>' instead of the global '{{cli_name}} --json ...' flag when you need structured config output on stdout and logs at the same time.

Examples:
  {{cli_name}} config --list
                                      list all known configuration keys with effective values
  {{cli_name}} config meta.version
                                      print the effective value of meta.version
  {{cli_name}} config --describe webview.root
                                      print the description, default, and source for webview.root
  {{cli_name}} config "ios.*"
                                      glob-match a group of related keys
  {{cli_name}} config --format json
                                      print the merged effective configuration as JSON
)TEXT";

constexpr auto gHelpTextVersions = R"TEXT(
Print Oro CLI/runtime and dependency versions.

Usage:
  {{cli_name}} versions [options] [<dependency-name>]

Options:
  -f, --format=<format>               print as 'text' or 'json'
  --json                              shorthand for '--format=json'
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-versions
)TEXT";

constexpr auto gManualTextVersions = R"TEXT(
Notes:
  With no dependency name, prints the full version map for the CLI, runtime, and linked libraries.

  Passing a dependency name filters to a single entry (for example: oro, oroc, uv, sqlite, iroh).

  Use '--json' or '--format=json' when you need stable machine-readable output on stdout.

  Use '--log-file=<path>' if you also need logs; the global '{{cli_name}} --json ...' form is reserved for JSON log streaming.

Examples:
  {{cli_name}} versions
                                      print all known dependency versions as text
  {{cli_name}} versions --json
                                      print all known dependency versions as JSON
  {{cli_name}} versions uv
                                      print only the libuv version
)TEXT";

constexpr auto gHelpTextVersion = R"TEXT(
Inspect or bump the project version defined in your configuration file.

Usage:
  {{cli_name}} version [options]
  {{cli_name}} version <new-version | release> [options]

Options:
  -C, --config=<path>                 use an explicit oro.toml or oro.ini
  --preid=<id>                        pre-release identifier (default: rc)
  -V, --verbose                       enable verbose output
  --log-file=<path>                   mirror logs to a JSON file
  -h, --help                          show help

Documentation:
  man oroc-version
)TEXT";

constexpr auto gManualTextVersion = R"TEXT(
Options:
  -C, --config=<path>                 explicit oro.toml/oro.ini to update
  --preid=<id>                        pre-release identifier for pre* / prerelease bumps (default: "rc")

Notes:
  - With no arguments, 'version' prints the current semantic version from the [meta] section.
  - With a <new-version> argument, it sets the version to that exact SemVer 2.0.0 value.
  - With a release type, it bumps the version using SemVer rules:
      major, minor, patch,
      premajor, preminor, prepatch, prerelease
    For pre* / prerelease bumps, '--preid' controls the pre-release tag (e.g., 'beta').
  - The command updates only your app configuration file; it does not call git or create tags.
  - oro.toml is the preferred config file, oro.ini is fully supported when oro.toml is absent.

Examples:
  {{cli_name}} version
                                      print the current project version
  {{cli_name}} version minor
                                      bump version to the next minor
  {{cli_name}} version prepatch --preid beta
                                      bump to the next patch and start a 'beta' pre-release
  {{cli_name}} version 1.2.3
                                      set the version explicitly to 1.2.3
)TEXT";

// Validate CSP using Google's CSP Evaluator
// https://csp-evaluator.withgoogle.com
constexpr auto gHelloWorld = R"HTML(
<!doctype html>
<html>
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
    <meta
      http-equiv="Content-Security-Policy"
      content="
        connect-src oro: https: http: blob: ipc: npm: node: wss: ws: ws://localhost:*;
         script-src oro: https: http: blob: npm: node: http://localhost:* 'unsafe-eval' 'unsafe-inline';
         worker-src oro: https: http: blob: 'unsafe-eval' 'unsafe-inline';
          frame-src oro: https: http: blob: http://localhost:*;
            img-src oro: https: http: blob: http://localhost:*;
          child-src oro: https: http: blob:;
         object-src 'none';
      "
    >
    <style type="text/css">
      html, body {
        height: 100%;
      }
      body {
        display: grid;
        justify-content: center;
        align-content: center;
        font-family: helvetica;
        overflow: hidden;
      }
    </style>
    <script src="index.js" type="module"></script>
  </head>
  <body>
    <h1>Hello, World.</h1>
  </body>
</html>
)HTML";

constexpr auto gHelloWorldScript = R"JavaScript(//
// Your JavaScript goes here!
//
import process from 'oro:process'
console.log(`Hello, ${process.platform}!`)

await navigator.serviceWorker.register('/serviceworker.js')
await navigator.serviceWorker.ready

const response = await fetch('/hello.json')

console.log(await response.json())

// Lifecycle: apps should handle background/foreground consistently across platforms.
// The runtime emits high-level events you can subscribe to:
const log = (m) => console.log(`[lifecycle] ${m}`)
globalThis.addEventListener('applicationpause', () => log('pause'))
globalThis.addEventListener('applicationresume', () => log('resume'))
globalThis.addEventListener('applicationstop', () => log('stop'))

// Note: fs.watch instances are stopped during pause. Recreate them after resume.
// See RUNTIME.md (Lifecycle Model) and examples/lifecycle/fs-watch-recreate.js for a pattern.
)JavaScript";

constexpr auto gHelloWorldServiceWorker = R"JavaScript(import process from 'oro:process'
export default async function (request) {
  const url = new URL(request.url)
  if (url.pathname === '/hello.json') {
    return Response.json({ hello: `world from service worker on ${process.platform}!` })
  }

  return null // 404
}
)JavaScript";

//
// macOS 'Info.plist' file
//
constexpr auto gMacOSInfoPList = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <!--- Metadata -->
  <key>CFBundleDisplayName</key>
  <string>{{meta_title}}</string>

  <key>CFBundleName</key>
  <string>{{meta_title}}</string>

	<key>CFBundleIconFile</key>
	<string>AppIcon</string>

	<key>CFBundleIconName</key>
	<string>AppIcon</string>

  <key>CFBundlePackageType</key>
  <string>APPL</string>

  <key>CFBundleVersion</key>
  <string>{{meta_version}}</string>

  <key>CFBundleShortVersionString</key>
  <string>{{meta_version}}</string>

  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>

  <key>CFBundleExecutable</key>
  <string>{{build_name}}</string>

  <key>CFBundleIdentifier</key>
  <string>{{meta_bundle_identifier}}</string>

  <key>LSUIElement</key>
  <{{application_agent}}/>

  <key>LSApplicationCategoryType</key>
  <string>{{mac_category}}</string>

  <key>NSHumanReadableCopyright</key>
  <string>{{meta_copyright}}</string>

  <key>NSMainNibFile</key>
  <string>MainMenu</string>

  <key>NSPrincipalClass</key>
  <string>NSApplication</string>

  <key>CFBundleURLTypes</key>
  <array>
    <dict>
      <key>CFBundleURLName</key>
      <string>{{meta_application_protocol}}</string>
      <key>CFBundleURLSchemes</key>
      <array>
        <string>{{meta_application_protocol}}</string>
      </array>
    </dict>
  </array>

  <key>NSUserActivityTypes</key>
  <array>
    <string>NSUserActivityTypeBrowsingWeb</string>
  </array>

  <!-- Application configuration -->
  <key>NSLocationDefaultAccuracyReduced</key>
  <true/>

  <key>LSMinimumSystemVersion</key>
  <string>{{mac_minimum_supported_version}}</string>

  <key>LSMultipleInstancesProhibited</key>
  <true/>

  <key>NSHighResolutionCapable</key>
  <true/>

  <key>NSRequiresAquaSystemAppearance</key>
  <false/>

  <key>NSSupportsAutomaticGraphicsSwitching</key>
  <true/>

  <key>SoftResourceLimits</key>
  <dict>
      <key>NumberOfFiles</key>
      <integer>{{meta_file_limit}}</integer>
  </dict>

  <key>WKAppBoundDomains</key>
  <array>
      <string>localhost</string>
      <string>{{meta_bundle_identifier}}</string>
  </array>


  <!-- Permission usage descriptions -->
  <key>NSAppDataUsageDescription</key>
  <string>
    {{meta_title}} would like shared app data access
  </string>

  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>
    {{meta_title}} would like to discover and connect to peers using Bluetooth
  </string>

  <key>NSCameraUsageDescription</key>
  <string>
    {{meta_title}} would like to access to your camera
  </string>

  <key>NSLocationAlwaysUsageDescription</key>
  <string>
    {{meta_title}} would like access to your location
  </string>

  <key>NSLocationWhenInUseUsageDescription</key>
  <string>
    {{meta_title}} would like access to your location when in use
  </string>

  <key>NSLocationTemporaryUsageDescriptionDictionary</key>
  <string>
    {{meta_title}} would like temporary access to your location
  </string>

  <key>NSMicrophoneUsageDescription</key>
  <string>
    {{meta_title}} would like to access to your microphone
  </string>

  <key>NSSpeechRecognitionUsageDescription</key>
  <string>
    {{meta_title}} would like to access Speech Recognition
  </string>

  <key>NSMotionUsageDescription</key>
  <string>
    {{meta_title}} would like to access to detect your device motion
  </string>


  <!-- Security configuration -->
  <key>NSAppTransportSecurity</key>
  <dict>
    <key>NSAllowsArbitraryLoads</key>
    <true/>

    <key>NSAllowsLocalNetworking</key>
    <true/>

    <key>NSExceptionDomains</key>
    <dict>
{{macos_app_transport_security_domain_exceptions}}
      <key>127.0.0.1</key>
      <dict>
        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>
        <true/>

        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>
        <false/>

        <key>NSIncludesSubdomains</key>
        <false/>

        <key>NSTemporaryExceptionMinimumTLSVersion</key>
        <string>1.0</string>

        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>
        <false/>
      </dict>

      <key>localhost</key>
      <dict>
        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>
        <true/>

        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>
        <false/>

        <key>NSIncludesSubdomains</key>
        <false/>

        <key>NSTemporaryExceptionMinimumTLSVersion</key>
        <string>1.0</string>

        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>
        <false/>
      </dict>
    </dict>
  </dict>


  <!-- Debug information -->
  <key>BuildMachineOSBuild</key>
  <string>{{__xcode_macosx_sdk_build_version}}</string>

  <key>BuildMachineOSBuild</key>
  <string>{{__xcode_macosx_sdk_build_version}}</string>

  <key>DTSDKName</key>
  <string>macosx{{__xcode_macosx_sdk_version}}</string>

  <key>DTXcode</key>
  <string>{{__xcode_version}}</string>

  <key>DTSDKBuild</key>
  <string>{{__xcode_macosx_sdk_version}}</string>

  <key>DTXcodeBuild</key>
  <string>{{__xcode_build_version}}</string>

  <!-- User given plist data -->
{{mac_info_plist_data}}
  </dict>
</plist>
)XML";

// Credits
constexpr auto gCredits = R"HTML(
  <p style="font-family: -apple-system; font-size: small; color: FieldText;">
    Built with {{cli_name}} v{{cli_version}}
  </p>
)HTML";

constexpr auto DEFAULT_ANDROID_APPLICATION_NAME = ".App";
constexpr auto DEFAULT_ANDROID_MAIN_ACTIVITY_NAME = ".MainActivity";

//
// Android Manifest
//
constexpr auto gAndroidManifest = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<manifest
  xmlns:android="http://schemas.android.com/apk/res/android"
>
  <uses-permission android:name="android.permission.INTERNET" />
  <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
  {{android_manifest_xml_permissions}}
  {{android_push_queries}}

  <uses-feature
    android:name="android.hardware.usb.host"
    android:required="false"
  />
  <uses-feature
    android:name="android.hardware.usb.accessory"
    android:required="false"
  />

  <application
    android:name="{{android_application}}"
    android:allowBackup="false"
    android:fullBackupContent="false"
    android:dataExtractionRules="@xml/runtime_data_extraction_rules"
    android:label="{{meta_title}}"
    android:theme="{{android_application_theme}}"
    android:supportsRtl="true"
    {{android_application_icon_config}}
    {{android_allow_cleartext}}
  >
    {{android_usb_notification_icon_metadata}}
    {{android_foreground_notification_icon_metadata}}
    <meta-data
      android:name="android.webkit.WebView.MetricsOptOut"
      android:value="true"
    />
    <activity
      android:name="{{android_main_activity}}"
      android:exported="true"
      android:configChanges="orientation|keyboardHidden|keyboard|screenSize|locale|layoutDirection|fontScale|screenLayout|density|uiMode"
      android:launchMode="singleTop"
      android:enableOnBackInvokedCallback="true"
      android:hardwareAccelerated="true"
    >
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>

      <intent-filter>
        <action android:name="android.intent.action.VIEW" />
        <category android:name="android.intent.category.DEFAULT" />
        <category android:name="android.intent.category.LAUNCHER" />
        <category android:name="android.intent.category.BROWSABLE" />
        <data android:scheme="{{meta_application_protocol}}" />
      </intent-filter>

      {{android_activity_intent_filters}}
      {{android_launcher_metadata}}
    </activity>

    <service
      android:name=".UsbService"
      android:exported="false"
      android:foregroundServiceType="connectedDevice"
      android:stopWithTask="false"
    />
    {{android_background_service}}
    {{android_migration_components}}
    {{android_push_components}}
    <receiver
      android:name="oro.runtime.app.NotificationDismissReceiver"
      android:exported="false"
    />
    <provider
      android:name="oro.runtime.window.ShareFileProvider"
      android:authorities="${applicationId}.oro.share"
      android:exported="false"
      android:grantUriPermissions="true"
    >
      <meta-data android:name="android.support.FILE_PROVIDER_PATHS"
        android:resource="@xml/runtime_share_paths" />
    </provider>
  </application>
</manifest>
)XML";

constexpr auto gAndroidSharePaths = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<paths xmlns:android="http://schemas.android.com/apk/res/android">
  <cache-path name="shares" path="oro-share/" />
</paths>
)XML";

constexpr auto gAndroidDataExtractionRules = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<data-extraction-rules>
  <cloud-backup>
    <exclude domain="root" path="." />
    <exclude domain="file" path="." />
    <exclude domain="database" path="." />
    <exclude domain="sharedpref" path="." />
    <exclude domain="external" path="." />
    <exclude domain="device_root" path="." />
    <exclude domain="device_file" path="." />
    <exclude domain="device_database" path="." />
    <exclude domain="device_sharedpref" path="." />
  </cloud-backup>
  <device-transfer>
    <exclude domain="root" path="." />
    <exclude domain="file" path="." />
    <exclude domain="database" path="." />
    <exclude domain="sharedpref" path="." />
    <exclude domain="external" path="." />
    <exclude domain="device_root" path="." />
    <exclude domain="device_file" path="." />
    <exclude domain="device_database" path="." />
    <exclude domain="device_sharedpref" path="." />
  </device-transfer>
</data-extraction-rules>
)XML";

//
// Linux config
//
constexpr auto gDesktopManifest = R"INI(
[Desktop Entry]
Encoding=UTF-8
Version=v{{meta_version}}
Name={{meta_title}}
Terminal=false
Type=Application
Exec={{linux_executable_path}} %U
Icon={{linux_icon_path}}
StartupWMClass={{build_name}}
StartupNotify={{linux_desktop_startup_notify}}
Comment={{meta_description}}
Categories={{linux_categories}};
MimeType=x-scheme-handler/{{meta_application_protocol}}
)INI";

constexpr auto gDebianManifest = R"DEB(Package: {{build_name}}
Version: {{meta_version}}
Architecture: {{arch}}
Maintainer: {{meta_maintainer}}
Description: {{meta_title}}
 {{meta_description}}
)DEB";

constexpr auto gRpmSpec = R"RPM(
Name: {{build_name}}
Version: {{meta_version}}
Release: {{linux_rpm_release}}
Summary: {{meta_title}}
License: {{meta_license}}
URL: {{meta_url}}
BuildArch: {{linux_rpm_arch}}
Requires: {{linux_rpm_requires}}

%description
{{meta_description}}

%install
mkdir -p %{buildroot}

%files
/opt/{{build_name}}
/usr/local/bin/{{build_name}}
/usr/share/applications/{{build_name}}.desktop
/usr/share/icons/hicolor/256x256/apps/{{build_name}}.png
)RPM";

constexpr auto gAurPKGBuild = R"PKG(
pkgname={{linux_aur_pkgname}}
pkgver={{meta_version}}
pkgrel={{linux_aur_pkgrel}}
pkgdesc="{{meta_title}}"
arch=({{linux_aur_arch}})
url="{{meta_url}}"
license=('custom')
depends=({{linux_aur_depends}})
source=()
sha256sums=()

package() {
  echo "TODO: implement Arch Linux packaging for Oro app '{{build_name}}' in PKGBUILD" >&2
}
)PKG";

//
// Windows config
//
constexpr auto gWindowsAppManifest = R"XML(<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap3="http://schemas.microsoft.com/appx/manifest/uap/windows10/3"
  xmlns:desktop="http://schemas.microsoft.com/appx/manifest/desktop/windows10"
  xmlns:desktop2="http://schemas.microsoft.com/appx/manifest/desktop/windows10/2"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap3 rescap"
>
  <Identity Name="{{meta_bundle_identifier}}"
    ProcessorArchitecture="neutral"
    Publisher="{{win_publisher}}"
    Version="{{win_version}}"
    ResourceId="{{build_name}}"
  />

  <Properties>
    <DisplayName>{{meta_title}}</DisplayName>
    <Description>{{meta_description}}</Description>
    <Logo>{{win_logo}}</Logo>
    <PublisherDisplayName>{{meta_maintainer}}</PublisherDisplayName>
  </Properties>

  <Resources>
    <Resource Language="{{meta_lang}}"/>
  </Resources>

  <Dependencies>
    <TargetDeviceFamily
      Name="Windows.Desktop"
      MinVersion="10.0.17763.0"
      MaxVersionTested="10.0.19041.1"
    />
  </Dependencies>

  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
  <Applications>
    <Application
      Id="{{build_name}}"
      EntryPoint="Windows.FullTrustApplication"
      Executable="{{win_exe}}"
    >
      <uap:VisualElements DisplayName="{{meta_title}}"
        Square150x150Logo="{{win_logo}}"
        Square44x44Logo="{{win_logo}}"
        Description="{{meta_description}}"
        BackgroundColor="#20123A"
      >
        <uap:DefaultTile Wide310x150Logo="{{win_logo}}" />
      </uap:VisualElements>
      <Extensions>
        <uap3:Extension
          Category="windows.appExecutionAlias"
          EntryPoint="Windows.FullTrustApplication"
          Executable="{{win_exe}}"
        >
          <uap3:AppExecutionAlias>
            <desktop:ExecutionAlias Alias="{{win_exe}}" />
          </uap3:AppExecutionAlias>
        </uap3:Extension>
      </Extensions>
    </Application>

  </Applications>
  <Extensions>
    <desktop2:Extension Category="windows.firewallRules">
      <desktop2:FirewallRules Executable="{{win_exe}}">
        <desktop2:Rule
          Direction="in"
          IPProtocol="TCP"
          LocalPortMin="1"
          LocalPortMax="65535"
          RemotePortMin="1"
          RemotePortMax="65535"
          Profile="all" />
        <desktop2:Rule
          Direction="out"
          IPProtocol="TCP"
          LocalPortMin="1"
          LocalPortMax="65535"
          RemotePortMin="1"
          RemotePortMax="65535"
          Profile="all" />

        <desktop2:Rule
          Direction="in"
          IPProtocol="UDP"
          LocalPortMin="1"
          LocalPortMax="65535"
          RemotePortMin="1"
          RemotePortMax="65535"
          Profile="all" />
        <desktop2:Rule
          Direction="out"
          IPProtocol="UDP"
          LocalPortMin="1"
          LocalPortMax="65535"
          RemotePortMin="1"
          RemotePortMax="65535"
          Profile="all" />
      </desktop2:FirewallRules>
    </desktop2:Extension>
  </Extensions>
</Package>
)XML";

//
// Template generated by XCode.
//
constexpr auto gXCodeProject = R"ASCII(// !$*UTF8*$!
{
  archiveVersion = 1;
  classes = {
  };
  objectVersion = 55;
  objects = {

/* Begin PBXBuildFile section */
    {{__ios_native_extensions_build_context_sections}}
		034B592125768A7B005D0134 /* lib/default.metallib in Resources */ = {isa = PBXBuildFile; fileRef = 034B592025768A7B005D0134 /* lib/default.metallib */; };
		171C1C2B2AC38A70005F587F /* CoreLocation.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 171C1C2A2AC38A70005F587F /* CoreLocation.framework */; };
		171C1C2E2AC38A70005F587F /* CoreGraphics.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 171C1C2F2AC38A70005F587F /* CoreGraphics.framework */; };
		171C1C2C2AC38A70005F587F /* Security.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 171C1C2D2AC38A70005F587F /* Security.framework */; };
		179989D22A867B260041EDC1 /* UniformTypeIdentifiers.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 179989D12A867B260041EDC1 /* UniformTypeIdentifiers.framework */; };
		17A7F8F229358D220051D146 /* init.cc in Sources */ = {isa = PBXBuildFile; fileRef = 17A7F8EE29358D180051D146 /* init.cc */; };
		17A7F8F529358D430051D146 /* liboro-runtime.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F8F329358D430051D146 /* liboro-runtime.a */; };
		17A7F8F629358D430051D146 /* libuv.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F8F429358D430051D146 /* libuv.a */; };
		17A7F8FA29358D430051D146 /* libsodium.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F8FA29358D430051D14A /* libsodium.a */; };
		17A7F8F629358D4A0051D146 /* libllama.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F8F429358D430051D147 /* libllama.a */; };
		17A7F90029358D4A0051D148 /* libwhisper.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F90029358D430051D149 /* libwhisper.a */; };
		17E5B4C42D40623300C40EB2 /* libggml-base.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17E5B4BF2D40623200C40EB2 /* libggml-base.a */; };
		17E5B4C52D40623300C40EB2 /* libggml-cpu.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17E5B4C02D40623300C40EB2 /* libggml-cpu.a */; };
		17E5B4C72D40623300C40EB2 /* libggml-metal.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17E5B4C22D40623300C40EB2 /* libggml-metal.a */; };
		17E5B4C82D40623300C40EB2 /* libggml.a in Frameworks */ = {isa = PBXBuildFile; fileRef = 17E5B4C32D40623300C40EB2 /* libggml.a */; };
		17A7F8F729358D4D0051D146 /* main.o in Frameworks */ = {isa = PBXBuildFile; fileRef = 17A7F8F129358D180051D146 /* main.o */; };
		17C230BA28E9398700301440 /* Foundation.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 17C230B928E9398700301440 /* Foundation.framework */; };
		290F7EBF2768C49000486988 /* UIKit.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 294A3C792763E9C6007B5B9A /* UIKit.framework */; };
		290F7F87276BC2B000486988 /* lib in Resources */ = {isa = PBXBuildFile; fileRef = 290F7F86276BC2B000486988 /* lib */; };
		29124C5D2761336B001832A0 /* LaunchScreen.storyboard in Resources */ = {isa = PBXBuildFile; fileRef = 29124C5B2761336B001832A0 /* LaunchScreen.storyboard */; };
		294A3C852764EAB7007B5B9A /* ui in Resources */ = {isa = PBXBuildFile; fileRef = 294A3C842764EAB7007B5B9A /* ui */; };
		294A3CA02768C429007B5B9A /* WebKit.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 294A3C7B2763EA7F007B5B9A /* WebKit.framework */; };
		2996EDB22770BC1F00C672A0 /* Accelerate.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672B1 /* Accelerate.framework */; };
		2996EDB22770BC1F00C672A1 /* Metal.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672A1 /* Metal.framework */; };
		2996EDB22770BC1F00C672A2 /* Network.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672A2 /* Network.framework */; };
		2996EDB22770BC1F00C672A3 /* CoreBluetooth.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672A3 /* CoreBluetooth.framework */; };
		2996EDB22770BC1F00C672A4 /* UserNotifications.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672A4 /* UserNotifications.framework */; };
		2996EDB22770BC1F00C672B0 /* QuartzCore.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = 2996EDB12770BC1F00C672B0 /* QuartzCore.framework */; };
		2996EDB22770BC1F00C672A5 /* Assets.xcassets in Resources */ = {isa = PBXBuildFile; fileRef = 29124C5E2761336B001832A1 /* Assets.xcassets */; };
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
    {{__ios_native_extensions_build_context_refs}}
		171C1C2A2AC38A70005F587F /* CoreLocation.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = CoreLocation.framework; path = System/Library/Frameworks/CoreLocation.framework; sourceTree = SDKROOT; };
		171C1C2F2AC38A70005F587F /* CoreGraphics.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = CoreGraphics.framework; path = System/Library/Frameworks/CoreGraphics.framework; sourceTree = SDKROOT; };
		171C1C2D2AC38A70005F587F /* Security.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Security.framework; path = System/Library/Frameworks/Security.framework; sourceTree = SDKROOT; };
		179989D12A867B260041EDC1 /* UniformTypeIdentifiers.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = UniformTypeIdentifiers.framework; path = System/Library/Frameworks/UniformTypeIdentifiers.framework; sourceTree = SDKROOT; };
		17A7F8EE29358D180051D146 /* init.cc */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.objcpp; path = init.cc; sourceTree = "<group>"; };
		17A7F8F129358D180051D146 /* main.o */ = {isa = PBXFileReference; lastKnownFileType = "compiled.mach-o.objfile"; path = main.o; sourceTree = "<group>"; };
		17A7F8F329358D430051D146 /* liboro-runtime.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = "liboro-runtime.a"; path = "lib/liboro-runtime.a"; sourceTree = "<group>"; };
		17A7F8F429358D430051D146 /* libuv.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = libuv.a; path = lib/libuv.a; sourceTree = "<group>"; };
		17A7F8FA29358D430051D14A /* libsodium.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = libsodium.a; path = lib/libsodium.a; sourceTree = "<group>"; };
		17A7F8F429358D430051D147 /* libllama.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = libllama.a; path = lib/libllama.a; sourceTree = "<group>"; };
		17A7F90029358D430051D149 /* libwhisper.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = libwhisper.a; path = lib/libwhisper.a; sourceTree = "<group>"; };
		17C230B928E9398700301440 /* Foundation.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Foundation.framework; path = System/Library/Frameworks/Foundation.framework; sourceTree = SDKROOT; };
		17E5B4BF2D40623200C40EB2 /* libggml-base.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = "libggml-base.a"; path = "lib/libggml-base.a"; sourceTree = "<group>"; };
		17E5B4C02D40623300C40EB2 /* libggml-cpu.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = "libggml-cpu.a"; path = "lib/libggml-cpu.a"; sourceTree = "<group>"; };
		17E5B4C22D40623300C40EB2 /* libggml-metal.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = "libggml-metal.a"; path = "lib/libggml-metal.a"; sourceTree = "<group>"; };
		17E5B4C32D40623300C40EB2 /* libggml.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = libggml.a; path = lib/libggml.a; sourceTree = "<group>"; };
		17E73FEE28FCD3360087604F /* libuv-ios.a */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = "libuv-ios.a"; path = "lib/libuv-ios.a"; sourceTree = "<group>"; };
		290F7F86276BC2B000486988 /* lib */ = {isa = PBXFileReference; lastKnownFileType = folder; path = lib; sourceTree = "<group>"; };
		29124C4A27613369001832A0 /* {{build_name}}.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = "{{build_name}}.app"; sourceTree = BUILT_PRODUCTS_DIR; };
		29124C5C2761336B001832A0 /* Base */ = {isa = PBXFileReference; lastKnownFileType = file.storyboard; name = Base; path = Base.lproj/LaunchScreen.storyboard; sourceTree = "<group>"; };
		29124C5E2761336B001832A0 /* Info.plist */ = {isa = PBXFileReference; lastKnownFileType = text.plist.xml; path = Info.plist; sourceTree = "<group>"; };
		034B592025768A7B005D0134 /* lib/default.metallib */ = {isa = PBXFileReference; lastKnownFileType = "archive.metal-library"; path = lib/default.metallib; sourceTree = "<group>"; };
		29124C5E2761336B001832A1 /* Assets.xcassets */ = {isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = Assets.xcassets; sourceTree = "<group>"; };
		294A3C792763E9C6007B5B9A /* UIKit.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = UIKit.framework; path = System/Library/Frameworks/UIKit.framework; sourceTree = SDKROOT; };
		294A3C7B2763EA7F007B5B9A /* WebKit.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = WebKit.framework; path = System/Library/Frameworks/WebKit.framework; sourceTree = SDKROOT; };
		294A3C842764EAB7007B5B9A /* ui */ = {isa = PBXFileReference; lastKnownFileType = folder; path = ui; sourceTree = "<group>"; };
		294A3C9027677424007B5B9A /* oro.entitlements */ = {isa = PBXFileReference; lastKnownFileType = text.plist.entitlements; path = oro.entitlements; sourceTree = "<group>"; };
    2996EDB12770BC1F00C672B1 /* Accelerate.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Accelerate.framework; path = System/Library/Frameworks/Accelerate.framework; sourceTree = SDKROOT; };
    2996EDB12770BC1F00C672A1 /* Metal.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Metal.framework; path = System/Library/Frameworks/Metal.framework; sourceTree = SDKROOT; };
		2996EDB12770BC1F00C672A2 /* Network.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Network.framework; path = System/Library/Frameworks/Network.framework; sourceTree = SDKROOT; };
		2996EDB12770BC1F00C672A3 /* CoreBluetooth.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = CoreBluetooth.framework; path = System/Library/Frameworks/CoreBluetooth.framework; sourceTree = SDKROOT; };
		2996EDB12770BC1F00C672A4 /* UserNotifications.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = UserNotifications.framework; path = System/Library/Frameworks/UserNotifications.framework; sourceTree = SDKROOT; };
		2996EDB12770BC1F00C672B0 /* QuartzCore.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = Quartzcore.framework; path = System/Library/Frameworks/Quartzcore.framework; sourceTree = SDKROOT; };
/* End PBXFileReference section */

/* Begin PBXFrameworksBuildPhase section */
		29124C4727613369001832A0 /* Frameworks */ = {
			isa = PBXFrameworksBuildPhase;
			buildActionMask = 2147483647;
			files = (
        {{__ios_native_extensions_build_ids}}
				17E5B4C42D40623300C40EB2 /* libggml-base.a in Frameworks */,
				17E5B4C52D40623300C40EB2 /* libggml-cpu.a in Frameworks */,
				17E5B4C72D40623300C40EB2 /* libggml-metal.a in Frameworks */,
				17E5B4C82D40623300C40EB2 /* libggml.a in Frameworks */,
				171C1C2B2AC38A70005F587F /* CoreLocation.framework in Frameworks */,
				171C1C2E2AC38A70005F587F /* CoreGraphics.framework in Frameworks */,
				171C1C2C2AC38A70005F587F /* Security.framework in Frameworks */,
				179989D22A867B260041EDC1 /* UniformTypeIdentifiers.framework in Frameworks */,
				17A7F8F529358D430051D146 /* liboro-runtime.a in Frameworks */,
				17A7F8F629358D430051D146 /* libuv.a in Frameworks */,
				17A7F8FA29358D430051D146 /* libsodium.a in Frameworks */,
        17A7F8F629358D4A0051D146 /* libllama.a in Frameworks */,
        17A7F90029358D4A0051D148 /* libwhisper.a in Frameworks */,
				17A7F8F729358D4D0051D146 /* main.o in Frameworks */,
				17C230BA28E9398700301440 /* Foundation.framework in Frameworks */,
				2996EDB22770BC1F00C672A0 /* Accelerate.framework in Frameworks */,
				2996EDB22770BC1F00C672A1 /* Metal.framework in Frameworks */,
				2996EDB22770BC1F00C672A2 /* Network.framework in Frameworks */,
				2996EDB22770BC1F00C672A3 /* CoreBluetooth.framework in Frameworks */,
				2996EDB22770BC1F00C672A4 /* UserNotifications.framework in Frameworks */,
        2996EDB22770BC1F00C672B0 /* QuartzCore.framework in Frameworks */,
				294A3CA02768C429007B5B9A /* WebKit.framework in Frameworks */,
				290F7EBF2768C49000486988 /* UIKit.framework in Frameworks */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXFrameworksBuildPhase section */

/* Begin PBXGroup section */
		1790CE4C2AD78CCF00AA7E5B /* core */ = {
			isa = PBXGroup;
			children = (
			);
			path = core;
			sourceTree = "<group>";
		};
		17A7F8EF29358D180051D146 /* objects */ = {
			isa = PBXGroup;
			children = (
				17A7F8F029358D180051D146 /* ios */,
			);
			path = objects;
			sourceTree = "<group>";
		};
		17A7F8F029358D180051D146 /* ios */ = {
			isa = PBXGroup;
			children = (
				17A7F8F129358D180051D146 /* main.o */,
			);
			path = ios;
			sourceTree = "<group>";
		};
		29124C4127613369001832A0 = {
			isa = PBXGroup;
			children = (
				1790CE512AD792B600AA7E5B /* core */,
				17A7F8EE29358D180051D146 /* init.cc */,
				17A7F8EF29358D180051D146 /* objects */,
				290F7F86276BC2B000486988 /* lib */,
				294A3C9027677424007B5B9A /* oro.entitlements */,
				294A3C842764EAB7007B5B9A /* ui */,
				29124C5B2761336B001832A0 /* LaunchScreen.storyboard */,
				29124C5E2761336B001832A0 /* Info.plist */,
				29124C5E2761336B001832A1 /* Assets.xcassets */,
				29124C4B27613369001832A0 /* Products */,
				294A3C782763E9C6007B5B9A /* Frameworks */,
			);
			sourceTree = "<group>";
		};
		29124C4B27613369001832A0 /* Products */ = {
			isa = PBXGroup;
			children = (
				29124C4A27613369001832A0 /* {{build_name}}.app */,
			);
			name = Products;
			sourceTree = "<group>";
		};
		294A3C782763E9C6007B5B9A /* Frameworks */ = {
			isa = PBXGroup;
			children = (
        {{__ios_native_extensions_build_refs}}
				17E5B4C32D40623300C40EB2 /* libggml.a */,
				17E5B4BF2D40623200C40EB2 /* libggml-base.a */,
				17E5B4C02D40623300C40EB2 /* libggml-cpu.a */,
				17E5B4C22D40623300C40EB2 /* libggml-metal.a */,
				171C1C2A2AC38A70005F587F /* CoreLocation.framework */,
				171C1C2F2AC38A70005F587F /* CoreGraphics.framework */,
				171C1C2D2AC38A70005F587F /* Security.framework */,
				179989D12A867B260041EDC1 /* UniformTypeIdentifiers.framework */,
				17A7F8F329358D430051D146 /* liboro-runtime.a */,
				17A7F8F429358D430051D146 /* libuv.a */,
				17A7F8FA29358D430051D14A /* libsodium.a */,
				17A7F8F429358D430051D147 /* libllama.a */,
        17A7F90029358D430051D149 /* libwhisper.a */,
				17E73FEE28FCD3360087604F /* libuv-ios.a */,
				17C230B928E9398700301440 /* Foundation.framework */,
        2996EDB12770BC1F00C672A1 /* Metal.framework */,
        2996EDB12770BC1F00C672B1 /* Accelerate.framework */,
				2996EDB12770BC1F00C672A2 /* Network.framework */,
				2996EDB12770BC1F00C672A3 /* CoreBluetooth.framework */,
				2996EDB12770BC1F00C672A4 /* UserNotifications.framework */,
				2996EDB12770BC1F00C672B0 /* QuartzCore.framework */,
				294A3C7B2763EA7F007B5B9A /* WebKit.framework */,
				294A3C792763E9C6007B5B9A /* UIKit.framework */,
			);
			name = Frameworks;
			sourceTree = "<group>";
		};
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		29124C4927613369001832A0 /* {{build_name}} */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = 29124C792761336B001832A0 /* Build configuration list for PBXNativeTarget {{build_name}} */;
			buildPhases = (
				29124C4627613369001832A0 /* Sources */,
				29124C4727613369001832A0 /* Frameworks */,
				29124C4827613369001832A0 /* Resources */,
			);
			buildRules = (
			);
			dependencies = (
			);
			name = "{{build_name}}";
			productName = "{{meta_title}}";
			productReference = 29124C4A27613369001832A0 /* {{build_name}}.app */;
			productType = "com.apple.product-type.application";
		};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		29124C4227613369001832A0 /* Project object */ = {
			isa = PBXProject;
			attributes = {
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 1340;
				TargetAttributes = {
					29124C4927613369001832A0 = {
						CreatedOnToolsVersion = 13.1;
					};
				};
			};
			buildConfigurationList = 29124C4527613369001832A0 /* Build configuration list for PBXProject "{{build_name}}" */;
			compatibilityVersion = "Xcode 13.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (
				en,
				Base,
			);
			mainGroup = 29124C4127613369001832A0;
			productRefGroup = 29124C4B27613369001832A0 /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				29124C4927613369001832A0 /* {{build_name}} */,
			);
		};
/* End PBXProject section */

/* Begin PBXResourcesBuildPhase section */
		29124C4827613369001832A0 /* Resources */ = {
			isa = PBXResourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
				29124C5D2761336B001832A0 /* LaunchScreen.storyboard in Resources */,
				294A3C852764EAB7007B5B9A /* ui in Resources */,
				034B592125768A7B005D0134 /* default.metallib in Resources */,
				2996EDB22770BC1F00C672A5 /* Assets.xcassets in Resources */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXResourcesBuildPhase section */

/* Begin PBXSourcesBuildPhase section */
		29124C4627613369001832A0 /* Sources */ = {
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
				17A7F8F229358D220051D146 /* init.cc in Sources */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXSourcesBuildPhase section */

/* Begin PBXVariantGroup section */
		29124C5B2761336B001832A0 /* LaunchScreen.storyboard */ = {
			isa = PBXVariantGroup;
			children = (
				29124C5C2761336B001832A0 /* Base */,
			);
			name = LaunchScreen.storyboard;
			sourceTree = "<group>";
		};
/* End PBXVariantGroup section */

/* Begin XCBuildConfiguration section */
    29124C772761336B001832A0 /* Debug */ = {
      isa = XCBuildConfiguration;
      buildSettings = {
        ALWAYS_SEARCH_USER_PATHS = NO;
        CLANG_ANALYZER_NONNULL = NO;
        CLANG_ANALYZER_NUMBER_OBJECT_CONVERSION = YES_AGGRESSIVE;
        CLANG_CXX_LANGUAGE_STANDARD = "c++20";
        CLANG_CXX_LIBRARY = "libc++";
        CLANG_ENABLE_MODULES = YES;
        CLANG_ENABLE_OBJC_ARC = YES;
        CLANG_ENABLE_OBJC_WEAK = YES;
        CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;
        CLANG_WARN_BOOL_CONVERSION = YES;
        CLANG_WARN_COMMA = YES;
        CLANG_WARN_CONSTANT_CONVERSION = YES;
        CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;
        CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;
        CLANG_WARN_DOCUMENTATION_COMMENTS = YES;
        CLANG_WARN_EMPTY_BODY = YES;
        CLANG_WARN_ENUM_CONVERSION = YES;
        CLANG_WARN_INFINITE_RECURSION = YES;
        CLANG_WARN_INT_CONVERSION = YES;
        CLANG_WARN_NON_LITERAL_NULL_CONVERSION = NO;
        CLANG_WARN_NULLABLE_TO_NONNULL_CONVERSION = NO;
        CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;
        CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;
        CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;
        CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;
        CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;
        CLANG_WARN_STRICT_PROTOTYPES = YES;
        CLANG_WARN_SUSPICIOUS_MOVE = YES;
        CLANG_WARN_UNGUARDED_AVAILABILITY = YES_AGGRESSIVE;
        CLANG_WARN_UNREACHABLE_CODE = YES;
        CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;
        COPY_PHASE_STRIP = NO;
        DEBUG_INFORMATION_FORMAT = dwarf;
        ENABLE_STRICT_OBJC_MSGSEND = YES;
        ENABLE_TESTABILITY = YES;
        GCC_C_LANGUAGE_STANDARD = gnu11;
        GCC_DYNAMIC_NO_PIC = NO;
        GCC_NO_COMMON_BLOCKS = YES;
        GCC_OPTIMIZATION_LEVEL = 0;
        GCC_PREPROCESSOR_DEFINITIONS = (
          "DEBUG=1",
          "ORO_RUNTIME_VERSION={{ORO_RUNTIME_VERSION}}",
          "ORO_RUNTIME_VERSION_HASH={{ORO_RUNTIME_VERSION_HASH}}",
          "ORO_RUNTIME_PLATFORM_SANDBOXED={{ORO_RUNTIME_PLATFORM_SANDBOXED}}",
          "$(inherited)",
        );
        GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
        GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
        GCC_WARN_UNDECLARED_SELECTOR = YES;
        GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
        GCC_WARN_UNUSED_FUNCTION = YES;
        GCC_WARN_UNUSED_VARIABLE = YES;
        IPHONEOS_DEPLOYMENT_TARGET = {{ios_deployment_target}};
        MTL_ENABLE_DEBUG_INFO = INCLUDE_SOURCE;
        MTL_FAST_MATH = YES;
        ONLY_ACTIVE_ARCH = YES;
        SDKROOT = {{ios_sdkroot}};
        SUPPORTED_PLATFORMS = "iphonesimulator iphoneos ipados ipadsimulator";
      };
      name = Debug;
    };
    29124C782761336B001832A0 /* Release */ = {
      isa = XCBuildConfiguration;
      buildSettings = {
        ALWAYS_SEARCH_USER_PATHS = NO;
        CLANG_ANALYZER_NONNULL = NO;
        CLANG_ANALYZER_NUMBER_OBJECT_CONVERSION = YES_AGGRESSIVE;
        CLANG_CXX_LANGUAGE_STANDARD = "c++20";
        CLANG_CXX_LIBRARY = "libc++";
        CLANG_ENABLE_MODULES = YES;
        CLANG_ENABLE_OBJC_ARC = YES;
        CLANG_ENABLE_OBJC_WEAK = YES;
        CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;
        CLANG_WARN_BOOL_CONVERSION = YES;
        CLANG_WARN_COMMA = YES;
        CLANG_WARN_CONSTANT_CONVERSION = YES;
        CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;
        CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;
        CLANG_WARN_DOCUMENTATION_COMMENTS = YES;
        CLANG_WARN_EMPTY_BODY = YES;
        CLANG_WARN_ENUM_CONVERSION = YES;
        CLANG_WARN_INFINITE_RECURSION = YES;
        CLANG_WARN_INT_CONVERSION = YES;
        CLANG_WARN_NON_LITERAL_NULL_CONVERSION = NO;
        CLANG_WARN_NULLABLE_TO_NONNULL_CONVERSION = NO;
        CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;
        CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;
        CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;
        CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;
        CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;
        CLANG_WARN_STRICT_PROTOTYPES = YES;
        CLANG_WARN_SUSPICIOUS_MOVE = YES;
        CLANG_WARN_UNGUARDED_AVAILABILITY = YES_AGGRESSIVE;
        CLANG_WARN_UNREACHABLE_CODE = YES;
        CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;
        COPY_PHASE_STRIP = NO;
        DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym";
        ENABLE_NS_ASSERTIONS = NO;
        ENABLE_STRICT_OBJC_MSGSEND = YES;
        GCC_C_LANGUAGE_STANDARD = gnu11;
        GCC_NO_COMMON_BLOCKS = YES;
        GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
        GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
        GCC_WARN_UNDECLARED_SELECTOR = YES;
        GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
        GCC_WARN_UNUSED_FUNCTION = YES;
        GCC_WARN_UNUSED_VARIABLE = YES;
        GCC_PREPROCESSOR_DEFINITIONS = (
          "ORO_RUNTIME_VERSION={{ORO_RUNTIME_VERSION}}",
          "ORO_RUNTIME_VERSION_HASH={{ORO_RUNTIME_VERSION_HASH}}",
          "$(inherited)",
        );
        IPHONEOS_DEPLOYMENT_TARGET = {{ios_deployment_target}};
        MTL_ENABLE_DEBUG_INFO = NO;
        MTL_FAST_MATH = YES;
        SDKROOT = {{ios_sdkroot}};
        SUPPORTED_PLATFORMS = "iphonesimulator iphoneos ipados ipadsimulator";
        VALIDATE_PRODUCT = YES;
      };
      name = Release;
    };
    29124C7A2761336B001832A0 /* Debug */ = {
      isa = XCBuildConfiguration;
      buildSettings = {
        ARCHS = "$(ARCHS_STANDARD)";
        ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
        ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME = AccentColor;
        CODE_SIGN_ENTITLEMENTS = "$(PROJECT_DIR)/oro.entitlements";
        CODE_SIGN_IDENTITY = "{{ios_codesign_identity}}";
        CODE_SIGN_STYLE = Manual;
        CURRENT_PROJECT_VERSION = {{ios_project_version}};
        DEVELOPMENT_TEAM = "{{apple_team_identifier}}";
        ENABLE_BITCODE = NO;
        GENERATE_INFOPLIST_FILE = YES;
        HEADER_SEARCH_PATHS = "$(PROJECT_DIR)/include";
        INFOPLIST_FILE = Info.plist;
        INFOPLIST_KEY_NSHumanReadableCopyright = "{{meta_copyright}}";
        INFOPLIST_KEY_UILaunchStoryboardName = LaunchScreen;
        INFOPLIST_KEY_UIRequiresFullScreen = YES;
        INFOPLIST_KEY_UIStatusBarHidden = YES;
        INFOPLIST_KEY_UISupportedInterfaceOrientations_iPad = "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight";
        INFOPLIST_KEY_UISupportedInterfaceOrientations_iPhone = "UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight";
        LD_RUNPATH_SEARCH_PATHS = (
          "$(inherited)",
          "@executable_path/Frameworks",
          "@executable_path/../Frameworks",
        );
        LIBRARY_SEARCH_PATHS = "$(PROJECT_DIR)/lib";
        MARKETING_VERSION = {{meta_version}};
        ONLY_ACTIVE_ARCH = YES;
        OTHER_CFLAGS = (
          "-DHOST=\\\"{{host}}\\\"",
          "-DPORT={{port}}",
        );
        PRODUCT_BUNDLE_IDENTIFIER = "{{meta_bundle_identifier}}";
        PRODUCT_NAME = "$(TARGET_NAME)";
        PROVISIONING_PROFILE_SPECIFIER = "{{ios_provisioning_specifier}}";
        SWIFT_EMIT_LOC_STRINGS = YES;
        TARGETED_DEVICE_FAMILY = "1,2";
        WARNING_CFLAGS = (
          "$(inherited)",
          "-Wno-nullability-completeness",
        );
      };
      name = Debug;
    };
    29124C7B2761336B001832A0 /* Release */ = {
      isa = XCBuildConfiguration;
      buildSettings = {
        ARCHS = "$(ARCHS_STANDARD)";
        ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
        ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME = AccentColor;
        CODE_SIGN_ENTITLEMENTS = "$(PROJECT_DIR)/oro.entitlements";
        CODE_SIGN_IDENTITY = "iPhone Distribution";
        CODE_SIGN_STYLE = Manual;
        CURRENT_PROJECT_VERSION = {{ios_project_version}};
        DEVELOPMENT_TEAM = "{{apple_team_identifier}}";
        ENABLE_BITCODE = NO;
        GENERATE_INFOPLIST_FILE = YES;
        HEADER_SEARCH_PATHS = "$(PROJECT_DIR)/include";
        INFOPLIST_FILE = Info.plist;
        INFOPLIST_KEY_NSHumanReadableCopyright = "{{meta_copyright}}";
        INFOPLIST_KEY_UILaunchStoryboardName = LaunchScreen;
        INFOPLIST_KEY_UIRequiresFullScreen = YES;
        INFOPLIST_KEY_UIStatusBarHidden = YES;
        INFOPLIST_KEY_UISupportedInterfaceOrientations_iPad = "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight";
        INFOPLIST_KEY_UISupportedInterfaceOrientations_iPhone = "UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight";
        LD_RUNPATH_SEARCH_PATHS = (
          "$(inherited)",
          "@executable_path/Frameworks",
          "@executable_path/../Frameworks",
        );
        LIBRARY_SEARCH_PATHS = "$(PROJECT_DIR)/lib";
        MARKETING_VERSION = {{meta_version}};
        ONLY_ACTIVE_ARCH = YES;
        PRODUCT_BUNDLE_IDENTIFIER = "{{meta_bundle_identifier}}";
        PRODUCT_NAME = "$(TARGET_NAME)";
        PROVISIONING_PROFILE_SPECIFIER = "{{ios_provisioning_specifier}}";
        SWIFT_EMIT_LOC_STRINGS = YES;
        TARGETED_DEVICE_FAMILY = "1,2";
        WARNING_CFLAGS = (
          "$(inherited)",
          "-Wno-nullability-completeness",
        );
      };
      name = Release;
    };
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
    29124C4527613369001832A0 /* Build configuration list for PBXProject "{{build_name}}" */ = {
      isa = XCConfigurationList;
      buildConfigurations = (
        29124C772761336B001832A0 /* Debug */,
        29124C782761336B001832A0 /* Release */,
      );
      defaultConfigurationIsVisible = 0;
      defaultConfigurationName = Release;
    };
    29124C792761336B001832A0 /* Build configuration list for PBXNativeTarget "{{build_name}}" */ = {
      isa = XCConfigurationList;
      buildConfigurations = (
        29124C7A2761336B001832A0 /* Debug */,
        29124C7B2761336B001832A0 /* Release */,
      );
      defaultConfigurationIsVisible = 0;
      defaultConfigurationName = Release;
    };
/* End XCConfigurationList section */
  };
  rootObject = 29124C4227613369001832A0 /* Project object */;

})ASCII";

//
// 'exportOptions.plist' for iOS
//
constexpr auto gXCodeExportOptionsForIOS = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key>
  <string>{{ios_distribution_method}}</string>
  <key>teamID</key>
  <string>{{apple_team_identifier}}</string>
  <key>uploadBitcode</key>
  <true/>
  <key>compileBitcode</key>
  <true/>
  <key>uploadSymbols</key>
  <true/>
  <key>signingStyle</key>
  <string>manual</string>
  <key>signingCertificate</key>
  <string>{{ios_codesign_identity}}</string>
  <key>provisioningProfiles</key>
  <dict>
    <key>{{meta_bundle_identifier}}</key>
    <string>{{ios_provisioning_profile}}</string>
  </dict>
</dict>
</plist>)XML";

//
// 'exportOptions.plist' for macOS
//
constexpr auto gXCodeExportOptionsForMacOS = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key>
  <string>{{mac_distribution_method}}</string>
  <key>teamID</key>
  <string>{{apple_team_identifier}}</string>
  <key>uploadBitcode</key>
  <true/>
  <key>compileBitcode</key>
  <true/>
  <key>uploadSymbols</key>
  <true/>
  <key>signingStyle</key>
  <string>manual</string>
  <key>signingCertificate</key>
  <string>{{mac_codesign_identity}}</string>
  <key>provisioningProfiles</key>
  <dict>
    <key>{{meta_bundle_identifier}}</key>
    <string>{{mac_provisioning_profile}}</string>
  </dict>
</dict>
</plist>)XML";

//
// iOS 'Info.plist' file
//
constexpr auto gIOSInfoPList = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <!--- Metadata -->
  <key>CFBundleIdentifier</key>
  <string>{{meta_bundle_identifier}}</string>

  <key>CFBundleDisplayName</key>
  <string>{{meta_title}}</string>

  <key>CFBundleName</key>
  <string>{{build_name}}</string>

	<key>CFBundleIconFile</key>
	<string>AppIcon</string>

	<key>CFBundleIconName</key>
	<string>AppIcon</string>

  <key>compileBitcode</key>
  <{{meta_compile_bitcode}}/>

  <key>uploadBitcode</key>
  <{{meta_upload_bitcode}}/>

  <key>uploadSymbols</key>
  <{{meta_upload_symbols}}/>

  <key>CFBundleURLTypes</key>
  <array>
    <dict>
      <key>CFBundleURLName</key>
      <string>{{meta_application_protocol}}</string>
      <key>CFBundleURLSchemes</key>
      <array>
        <string>{{meta_application_protocol}}</string>
      </array>
    </dict>
  </array>

  <key>LSApplicationCategoryType</key>
  <string>{{ios_category}}</string>

  <!-- Application configuration -->
  <key>LSApplicationQueriesSchemes</key>
  <array>
    <string>{{ios_protocol}}</string>
  </array>

  <key>NSHighResolutionCapable</key>
  <true/>

  <key>NSLocationDefaultAccuracyReduced</key>
  <false/>

  <key>NSRequiresAquaSystemAppearance</key>
  <false/>

  <key>NSSupportsAutomaticGraphicsSwitching</key>
  <true/>

  <key>ITSAppUsesNonExemptEncryption</key>
  <{{ios_nonexempt_encryption}}/>

  <!-- Permission usage descriptions -->
  <key>NSAppDataUsageDescription</key>
  <string>
    {{meta_title}} would like shared app data access
  </string>

  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>
    {{meta_title}} would like to discover and connect to peers using Bluetooth
  </string>

  <key>NSCameraUsageDescription</key>
  <string>
    {{meta_title}} would like to access to your camera
  </string>

  <key>NSLocalNetworkUsageDescription</key>
  <string>
    {{meta_title}} would like to discover and connect to peers using your local network
  </string>

  <key>NSLocationAlwaysUsageDescription</key>
  <string>
    {{meta_title}} would like access to your location
  </string>

  <key>NSLocationWhenInUseUsageDescription</key>
  <string>
    {{meta_title}} would like access to your location when in use
  </string>

  <key>NSLocationAlwaysAndWhenInUseUsageDescription</key>
  <string>
    {{meta_title}} would like access to your location
  </string>

  <key>NSLocationTemporaryUsageDescriptionDictionary</key>
  <string>
    {{meta_title}} would like temporary access to your location
  </string>

  <key>NSMicrophoneUsageDescription</key>
  <string>
    {{meta_title}} would like to access to your microphone
  </string>

  <key>NSSpeechRecognitionUsageDescription</key>
  <string>
    {{meta_title}} would like to access Speech Recognition
  </string>

  <key>NSMotionUsageDescription</key>
  <string>
    {{meta_title}} would like to access to detect your device motion
  </string>


  <!-- Security configuration -->
  <key>NSAppTransportSecurity</key>
  <dict>
    <key>NSAllowsArbitraryLoads</key>
    <true/>

    <key>NSAllowsLocalNetworking</key>
    <true/>

    <key>NSExceptionDomains</key>
    <dict>
{{ios_app_transport_security_domain_exceptions}}
      <key>127.0.0.1</key>
      <dict>
        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>
        <true/>

        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>
        <false/>

        <key>NSIncludesSubdomains</key>
        <false/>

        <key>NSTemporaryExceptionMinimumTLSVersion</key>
        <string>1.0</string>

        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>
        <false/>
      </dict>

      <key>localhost</key>
      <dict>
        <key>NSTemporaryExceptionAllowsInsecureHTTPLoads</key>
        <true/>

        <key>NSTemporaryExceptionRequiresForwardSecrecy</key>
        <false/>

        <key>NSIncludesSubdomains</key>
        <false/>

        <key>NSTemporaryExceptionMinimumTLSVersion</key>
        <string>1.0</string>

        <key>NSTemporaryExceptionAllowsInsecureHTTPSLoads</key>
        <false/>
      </dict>
    </dict>
  </dict>


  <key>UIApplicationSupportsIndirectInputEvents</key>
  <true/>

  <!-- User given plist data -->
{{ios_info_plist_data}}
</dict>
</plist>)XML";

constexpr auto gXcodeEntitlements = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <!-- Generated entitlements given plist data -->
{{configured_entitlements}}
</dict>
</plist>)XML";

//
// Android top level `build.gradle` in Groovy Syntax
//
constexpr auto gGradleBuild = R"GROOVY(
buildscript {
  repositories {
    google()
    mavenCentral()
  }

  dependencies {
    classpath 'com.android.tools.build:gradle:9.3.2'
  }
}

allprojects {
  repositories {
    google()
    mavenCentral()
  }
}

task clean (type: Delete) {
  delete rootProject.buildDir
}
)GROOVY";

//
// Android source `build.gradle` in Groovy Syntax
//
constexpr auto gGradleBuildForSource = R"GROOVY(
apply plugin: 'com.android.application'

android {
  compileSdk 37
  ndkVersion = "29.0.14206865"
  flavorDimensions "default"
  namespace = '{{android_bundle_identifier}}'

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  buildFeatures {
    buildConfig = true
  }

  defaultConfig {
    applicationId "{{android_bundle_identifier}}"
    minSdk 26
    targetSdk 37
    versionCode {{meta_revision}}
    versionName "{{meta_version}}"

    ndk {
      abiFilters {{android_ndk_abi_filters}}
    }

{{android_default_config_external_native_build}}
  }

  signingConfigs {
    release {
{{android_signingconfig_release}}
      v1SigningEnabled = true
      v2SigningEnabled = true
    }
  }

  aaptOptions {
    {{android_aapt}}
    // Matches the CLI asset-manifest inventory policy.
    ignoreAssetsPattern = '!.svn:!.git:!.ds_store:!*.scc:.*:<dir>_*:!CVS:!thumbs.db:!picasa.ini:!*~'
    noCompress {{android_aapt_no_compress}}
  }

{{android_external_native_build}}

  productFlavors {
    dev {
      buildConfigField "String", "URL", "\"{{TODO}}\""
    }

    live {
      buildConfigField "String", "URL", "\"{{TODO}}\""
    }
  }

  buildTypes {
    release {
      minifyEnabled true
      proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
      //productFlavors.dev
      productFlavors.live
      {{android_buildtypes_release_config}}
    }

    debug {
      minifyEnabled false
      proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
      productFlavors.dev
      //productFlavors.live
    }
  }
}

dependencies {
  implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3'
  implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3'
  implementation 'androidx.fragment:fragment-ktx:1.7.1'
  implementation 'androidx.lifecycle:lifecycle-process:2.7.0'
  implementation 'androidx.appcompat:appcompat:1.6.1'
  implementation 'androidx.core:core-ktx:1.13.0'
  implementation('androidx.webkit:webkit:1.14.0') { version { strictly '1.14.0' } }
  {{android_credentials_dependencies}}
  {{android_extension_dependencies}}
}
)GROOVY";

//
// Android top level `settings.gradle` in Groovy Syntax
//
constexpr auto gGradleSettings = R"GROOVY(
rootProject.name = "{{build_name}}"
include ':app'
{{android_extension_modules}}
)GROOVY";

//
// Android top level `gradle.properties`
//
constexpr auto gGradleProperties = R"GRADLE(
org.gradle.jvmargs=-Xmx2048m
org.gradle.parallel=true
org.gradle.caching={{android_gradle_build_cache}}
org.gradle.configuration-cache={{android_gradle_configuration_cache}}
org.gradle.configuration-cache.problems=fail

android.useAndroidX=true
kotlin.code.style=official
)GRADLE";

//
// Android cpp `Android.mk` file
//
constexpr auto gAndroidMakefile = R"MAKE(
LOCAL_PATH := $(call my-dir)

## libuv.a
include $(CLEAR_VARS)
LOCAL_MODULE := libuv

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libuv.a
include $(PREBUILT_STATIC_LIBRARY)

## libggml.a
include $(CLEAR_VARS)
LOCAL_MODULE := libggml

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libggml.a
LOCAL_STATIC_LIBRARIES := libggml-cpu libggml-base
include $(PREBUILT_STATIC_LIBRARY)

## libggml-base.a
include $(CLEAR_VARS)
LOCAL_MODULE := libggml-base

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libggml-base.a
include $(PREBUILT_STATIC_LIBRARY)

## libggml-cpu.a
include $(CLEAR_VARS)
LOCAL_MODULE := libggml-cpu

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libggml-cpu.a
LOCAL_STATIC_LIBRARIES := libggml-base
include $(PREBUILT_STATIC_LIBRARY)

## libllama.a
include $(CLEAR_VARS)
LOCAL_MODULE := libllama

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libllama.a
LOCAL_STATIC_LIBRARIES := libggml
include $(PREBUILT_STATIC_LIBRARY)

## libusb-1.0.a
include $(CLEAR_VARS)
LOCAL_MODULE := libusb

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libusb-1.0.a
include $(PREBUILT_STATIC_LIBRARY)

## libsodium.a
include $(CLEAR_VARS)
LOCAL_MODULE := libsodium

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libsodium.a
include $(PREBUILT_STATIC_LIBRARY)

## libwhisper.a
include $(CLEAR_VARS)
LOCAL_MODULE := libwhisper

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/libwhisper.a
LOCAL_STATIC_LIBRARIES := libggml
include $(PREBUILT_STATIC_LIBRARY)

## liboro-runtime.a
include $(CLEAR_VARS)
LOCAL_MODULE := liboro-runtime-static

LOCAL_SRC_FILES = ../libs/$(TARGET_ARCH_ABI)/liboro-runtime.a
LOCAL_STATIC_LIBRARIES := libuv libllama libwhisper libggml libusb libsodium
LOCAL_EXPORT_LDLIBS := -lz
include $(PREBUILT_STATIC_LIBRARY)

## Injected extensions
{{__android_native_extensions_context}}

## liboro-runtime.so
include $(CLEAR_VARS)
LOCAL_MODULE := oro-runtime

LOCAL_CFLAGS +=                                                                \
  -std=c++2a                                                                   \
  -g                                                                           \
  -I$(LOCAL_PATH)/include                                                      \
  -I$(LOCAL_PATH)                                                              \
  -pthreads                                                                    \
  -fexceptions                                                                 \
  -fPIC                                                                        \
  -frtti                                                                       \
  -fsigned-char                                                                \
  -O0

LOCAL_CFLAGS += {{cflags}}

LOCAL_LDLIBS := -landroid -llog -lz
LOCAL_SRC_FILES =                                                              \
  init.cc {{android_native_callback_sources}}

{{android_native_callback_includes}}

LOCAL_STATIC_LIBRARIES :=                                                      \
  libllama                                                                     \
  libwhisper                                                                   \
  libggml                                                                      \
  libggml-cpu                                                                  \
  libggml-base                                                                 \
  libusb                                                                       \
  libsodium                                                                    \

LOCAL_WHOLE_STATIC_LIBRARIES :=                                                \
  liboro-runtime-static                                                        \
  libuv

include $(BUILD_SHARED_LIBRARY)

## Custom userspace Android NDK
{{android_native_make_context}}
)MAKE";

//
// Android cpp `Application.mk` file
//
constexpr auto gAndroidApplicationMakefile = R"MAKE(
## Keep generated native debug paths stable across project checkout locations.
APP_CFLAGS += -ffile-prefix-map="$(abspath .)=."

APP_ABI := {{android_native_abis}}
APP_STL := c++_static
)MAKE";

//
// Android `proguard-rules.pro` file
//
constexpr auto gProGuardRules = R"PGR(
# JNI resolves runtime classes, fields, and callbacks by their declared names.
# These references are invisible to the Java/Kotlin shrinker.
-keep class oro.runtime.** { *; }
-keepclassmembers class * {
  @android.webkit.JavascriptInterface <methods>;
}

# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile
)PGR";

//
// Android `layout/web_view.xml`
//
constexpr auto gAndroidLayoutWebView = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<WebView
  xmlns:android="http://schemas.android.com/apk/res/android"
  xmlns:app="http://schemas.android.com/apk/res-auto"
  android:id="@+id/webview"
  android:layout_width="match_parent"
  android:layout_height="match_parent"
/>
)XML";

//
// Android `layout/window_container_view.xml`
//
constexpr auto gAndroidLayoutWindowContainerView = R"XML(
<?xml version="1.0" encoding="utf-8"?>
<FrameLayout
  xmlns:android="http://schemas.android.com/apk/res/android"
  android:layout_width="match_parent"
  android:layout_height="match_parent"
>
  <androidx.fragment.app.FragmentContainerView
    xmlns:android="http://schemas.android.com/apk/res/android"
    android:id="@+id/window"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
  />
</FrameLayout>
)XML";

constexpr auto gAndroidValuesStrings = R"XML(
<resources>
  <string name="app_name">{{meta_title}}</string>
  <string name="runtime_startup_loading">Loading…</string>
  <string name="runtime_startup_failed">Unable to open the app. Reload to try again. Unsaved input may be lost. Check any pending action before submitting it again.</string>
  <string name="runtime_package_mismatch">App files do not match this runtime. Install a matching app update, then reopen the app.</string>
  <string name="runtime_webview_update_required">This app requires a newer Android System WebView. Update WebView or your browser provider, then reopen the app.</string>
  <string name="runtime_fullscreen_exit">Exit fullscreen</string>
  <string name="runtime_push_distributor">Choose push distributor</string>
  <string name="runtime_reload">Reload app</string>
{{android_shortcut_strings}}
</resources>
)XML";

//
// XCode Build Config
//
constexpr auto gXCodeScheme = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Scheme
   LastUpgradeVersion = "1310"
   version = "1.3">
   <BuildAction
      parallelizeBuildables = "YES"
      buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry
            buildForTesting = "YES"
            buildForRunning = "YES"
            buildForProfiling = "YES"
            buildForArchiving = "YES"
            buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "29124C4927613369001832A0"
               BuildableName = "{{build_name}}.app"
               BlueprintName = "{{build_name}}"
               ReferencedContainer = "container:{{build_name}}.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction
      buildConfiguration = "Debug"
      selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"
      selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"
      shouldUseLaunchSchemeArgsEnv = "YES">
      <Testables>
      </Testables>
   </TestAction>
   <LaunchAction
      buildConfiguration = "Debug"
      selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"
      selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"
      launchStyle = "0"
      useCustomWorkingDirectory = "NO"
      ignoresPersistentStateOnLaunch = "NO"
      debugDocumentVersioning = "YES"
      debugServiceExtension = "internal"
      allowLocationSimulation = "YES">
      <BuildableProductRunnable
         runnableDebuggingMode = "0">
         <BuildableReference
            BuildableIdentifier = "primary"
            BlueprintIdentifier = "29124C4927613369001832A0"
            BuildableName = "{{build_name}}.app"
            BlueprintName = "{{build_name}}"
            ReferencedContainer = "container:{{build_name}}.xcodeproj">
         </BuildableReference>
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction
      buildConfiguration = "Release"
      shouldUseLaunchSchemeArgsEnv = "YES"
      savedToolIdentifier = ""
      useCustomWorkingDirectory = "NO"
      debugDocumentVersioning = "YES">
      <BuildableProductRunnable
         runnableDebuggingMode = "0">
         <BuildableReference
            BuildableIdentifier = "primary"
            BlueprintIdentifier = "29124C4927613369001832A0"
            BuildableName = "{{build_name}}.app"
            BlueprintName = "{{build_name}}"
            ReferencedContainer = "container:{{build_name}}.xcodeproj">
         </BuildableReference>
      </BuildableProductRunnable>
   </ProfileAction>
   <AnalyzeAction
      buildConfiguration = "Debug">
   </AnalyzeAction>
   <ArchiveAction
      buildConfiguration = "Release"
      customArchiveName = "{{build_name}}"
      revealArchiveInOrganizer = "YES">
   </ArchiveAction>
</Scheme>)XML";

constexpr auto gStoryboardLaunchScreen = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<document type="com.apple.InterfaceBuilder3.CocoaTouch.Storyboard.XIB" version="3.0" toolsVersion="13122.16" targetRuntime="iOS.CocoaTouch" propertyAccessControl="none" useAutolayout="YES" launchScreen="YES" useTraitCollections="YES" useSafeAreas="YES" colorMatched="YES" initialViewController="01J-lp-oVM">
  <dependencies>
    <plugIn identifier="com.apple.InterfaceBuilder.IBCocoaTouchPlugin" version="13104.12"/>
    <capability name="Safe area layout guides" minToolsVersion="9.0"/>
    <capability name="documents saved in the Xcode 8 format" minToolsVersion="8.0"/>
  </dependencies>
  <scenes>
    <!--View Controller-->
    <scene sceneID="EHf-IW-A2E">
      <objects>
        <viewController id="01J-lp-oVM" sceneMemberID="viewController">
          <view key="view" contentMode="scaleToFill" id="Ze5-6b-2t3">
            <rect key="frame" x="0.0" y="0.0" width="375" height="667"/>
            <autoresizingMask key="autoresizingMask" widthSizable="YES" heightSizable="YES"/>
            <color key="backgroundColor" xcode11CocoaTouchSystemColor="systemBackgroundColor" cocoaTouchSystemColor="whiteColor"/>
            <viewLayoutGuide key="safeArea" id="6Tk-OE-BBY"/>
          </view>
        </viewController>
        <placeholder placeholderIdentifier="IBFirstResponder" id="iYj-Kq-Ea1" userLabel="First Responder" sceneMemberID="firstResponder"/>
      </objects>
      <point key="canvasLocation" x="53" y="375"/>
    </scene>
  </scenes>
</document>
)XML";

constexpr auto gDefaultConfig = R"INI(
##
# Oro Runtime ☆ v{{cli_version}}
#
# Minimal oro.toml for a new app.
# Most settings have safe defaults - you can add more sections later as your project grows.
# Use `.ororc` next to this file for developer-local overrides and signing secrets.
# This file is the recommended editor project-root marker for `oroc language-server`.
# Optional [language_server.<language>] command/args override programs installed on PATH.
##

# Build-time bundling, output, and environment forwarding.
[build]
# Required: CLI build name (also used as binary name)
# Must contain only letters, numbers, dashes, or underscores
name = "{{project_name}}"
# Resource directory copied into the app bundle and served by the `oro:` scheme.
# Most apps keep HTML, JS, CSS, icons, and service workers under `src`.
copy = "src"
# Build output directory (relative to the project root). Safe to delete and regenerate.
output = "build"
# Opt in to supported build result caches. Platform and cache-specific settings can override this.
cache = false
# Environment variables forwarded from your shell into the runtime at launch.
# Use `.ororc` or `[env]`/`env_*` overrides when values differ per machine.
env = ["USER", "TMPDIR", "PWD"]

# Package metadata embedded in desktop/mobile artifacts and update manifests.
[meta]
# Required: reverse-DNS bundle identifier (used by app stores).
bundle_identifier = "com.{{project_name}}"
# Human-readable app name for metadata.
title = "{{project_name}}"
# Required: semantic version (major.minor.patch).
version = "1.0.0"
# Optional: SPDX or custom license identifier for packaging metadata.
license = "custom"
# Optional: project homepage URL for packaging metadata.
url = ""

# WebView bootstrap and resource-loading defaults.
[webview]
# URL path inside the packaged resources that becomes the initial app root.
root = "/"
# Automatically reload the webview when bundled resources change during local development.
# Packaged production builds ignore this.
watch = true

# Main application window defaults.
[window]
# Initial window width as a percentage of the available screen.
width = "50%"
# Initial window height as a percentage of the available screen.
height = "50%"
)INI";

constexpr auto gDefaultGitignore = R"GITIGNORE(
logs
# Logs
*.log
*.dat
npm-debug.log*
yarn-debug.log*
yarn-error.log*
lerna-debug.log*
.pnpm-debug.log*
.ororc
.oro.env
.*.env

# Ignore all files in the node_modules folder
node_modules/

# Default output directory
build/

# Provisioning profile
*.mobileprovision

# extension build artifacts
*.o

)GITIGNORE";
