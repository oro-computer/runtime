# Command Line interface
These commands are available from the command line interface (CLI).

## oroc
Web docs: https://oro.computer/runtime/docs/?p=cli%2Foroc

### Usage
```bash
oroc [SUBCOMMAND] [options] [<project-dir>]
oroc [SUBCOMMAND] -h
oroc help [query...]
```

### subcommands
| Option | Description |
| --- | --- |
| help | search commands, options, and platform notes |
| build | build project |
| list-devices | get the list of connected devices |
| init | create a new project (in the current directory) |
| install-app | install app to the device |
| print-build-dir | print build path to stdout |
| run | run application |
| config | inspect configuration values and docs |
| env | print relevant environment variables |
| mcp | run an MCP server for agent tooling |
| setup | install build dependencies |
| version | inspect or bump project version |
| versions | print Oro CLI/runtime and dependency versions |
| update | update tooling (keys, manifests, bundles) |

### environment
ORO_DEBUG                            enable debug mode (like -D)
ORO_VERBOSE                          enable verbose logs (like -V)
ORO_LOG_NO_COLOR                     disable colored log output
ORO_LOG_JSON                         enable structured JSON logs on stdout
ORO_LOG_FILE                         mirror logs to a JSON file
ORO_ALLOW_EXEC                       allow external exec during builds
ORO_ENABLE_SANITIZERS                enable ASan/UBSan on desktop builds
NO_ANDROID                           source bootstrap: disable only Android work when non-empty
NO_IOS                               source bootstrap: disable only iOS/iOS Simulator work when non-empty

### general options
| Option | Description |
| --- | --- |
| -h, --help | print help message |
| --prefix | print install path |
| -v, --version | print program version |
| -q, --quiet | hint for less log output |
| -V, --verbose | enable verbose output (can be global) |
| -D, --debug | enable debug output (can be global) |
| --no-color | disable colored log output |
| --json | enable structured JSON logs on stdout |
| --log-file=<path> | mirror logs to a JSON file |

### notes
Run 'oroc <subcommand> --help' for subcommand-specific flags, examples, and platform notes.
Use 'oroc help <query>' to search commands, options, examples, and workflow notes by topic.
'oroc <subcommand> --json' emits structured command results when that subcommand supports it.
The global 'oroc --json <subcommand>' form is reserved for JSON log streaming.
Use '--log-file=<path>' when you need machine-readable stdout plus a separate log stream.
NO_ANDROID and NO_IOS are independent presence flags for building the runtime source tree;
they do not select an application target for 'oroc build --platform'. Values such as
0 and false are non-empty and still disable the named target.

### examples
oroc init my-app
create a starter Oro project in ./my-app
oroc run .
run the app rooted at the current directory
oroc build --platform=ios --prod .
build a production iOS artifact from the current project
oroc config --describe meta.version
inspect documentation for a single configuration key
oroc help ios signing
search for commands and notes related to iOS signing and provisioning

### update tooling
oroc update keygen           generate an Ed25519 keypair for signing update manifests
oroc update init             scaffold a manifest JSON file
oroc update server           run an update server over HTTP/TCP/UDP
oroc update info             query update servers or static manifests over HTTP/TCP/UDP
oroc update sign             sign a manifest JSON file and emit a sidecar signature
oroc update verify           verify a manifest JSON file + signature using a public key
oroc update bundle           build a tar archive from a directory suitable as an update artifact
oroc update extract          extract an update bundle tar archive into a directory
oroc update validate         validate a manifest.json file against the update manifest schema shape

## oroc help
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fhelp

Discover commands, options, and workflow entry points from the CLI help index.

### Usage
```bash
oroc help [query...]
oroc help [query...] [--json]
```

### options
| Option | Description |
| --- | --- |
| --json | emit structured search results for automation and editor tooling |
| --log-file=<path> | mirror logs to a JSON file |

### notes
With no query, this subcommand prints the top-level CLI help.
Exact command matches print the full subcommand page (for example, 'oroc help build' or 'oroc help update validate').
Free-form queries search command names, descriptions, options, notes, and examples, then rank the closest matches.
Use this for discovery topics such as 'oroc help tls', 'oroc help ios signing', 'oroc help json logs', or 'oroc help agent tooling'.
'oroc help --json <query>' returns a machine-readable result list with ranked matches and exact-help payloads.

### examples
oroc help build
print the full help page for the build command
oroc help update validate
print the full help page for the nested update validate command
oroc help ios signing
search for commands and notes related to iOS signing and provisioning
oroc help json --json
return ranked JSON results for the query "json"

## oroc update
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate

Update tooling for manifests, signatures, and bundles.

### Usage
```bash
oroc update <subcommand> [options]
```

### subcommands
| Option | Description |
| --- | --- |
| keygen | generate an Ed25519 keypair for update manifests |
| init | scaffold a manifest JSON file |
| server | run an update server over HTTP/TCP/UDP |
| info | query update servers or static manifests over HTTP/TCP/UDP |
| sign | sign a manifest JSON file and emit a sidecar signature |
| verify | verify a manifest JSON file + signature using a public key |
| bundle | build a tar archive from a directory suitable as an update artifact |
| extract | extract an update bundle tar archive into a directory |
| validate | validate a manifest.json file against the update manifest schema shape |

### notes
All subcommands support '--log-file=<path>' to mirror logs to a JSON file.
Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' on relevant subcommands
to override the default manifest.json filename (and derived sidecar names).

### examples
oroc update init
scaffold a default manifest.json in the current project
oroc update bundle --manifest manifest.json
build a source bundle and record it in the manifest
oroc update keygen > key.json
generate a signing keypair JSON file
oroc update sign --keys key.json --manifest manifest.json
sign the manifest and write manifest.sig
oroc update verify --keys key.json --manifest manifest.json
verify manifest.json against manifest.sig with the public key
oroc update server --root ./updates
run an HTTP update server serving manifests and bundles
oroc update info --http --app-id com.example.app --follow-manifest
query an update server and fetch the referenced manifest

## oroc update-init
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Finit

Scaffold a minimal update manifest JSON file.

### Usage
```bash
oroc update init [options]
```

### options
| Option | Description |
| --- | --- |
| --config=<path> | use an explicit oro.toml/oro.ini file when deriving defaults |
| --manifest-name=<name> | filename for the manifest JSON (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
The generated manifest includes these defaults.
- schemaVersion = 1
- appId derived from your oro.toml ([meta] bundle_identifier, falling back to "com.example.app")
- generatedAt = current UTC timestamp
- channels = [update_channel or "stable"]
- updates = a single entry for the current version/channel (with an empty targets array)
Edit the generated file to add real targets, artifact metadata, or additional updates.

### examples
oroc update init
create ./manifest.json using oro.toml metadata
oroc update init --manifest-name app-updates.json
create ./app-updates.json instead of manifest.json

## oroc update-server
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fserver

Run an update server that speaks the Oro Application Update Protocol.

### Usage
```bash
oroc update server [options]
```

### options
| Option | Description |
| --- | --- |
| --root=<dir> | directory containing manifest trees and artifacts to serve |
| --host=<host> | interface to bind (default: 0.0.0.0) |
| --port=<port> | TCP/UDP port to bind (default: 8080) |
| --manifest-name=<name> | manifest filename to look up under each appId (default: manifest.json or ORO_UPDATE_MANIFEST_FILENAME) |
| --tcp | run in TCP mode (binary OUP CHECK/RESPONSE) |
| --udp | run in UDP mode (binary OUP CHECK/RESPONSE) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
Default mode is HTTP. The server exposes these endpoints.
- GET /health           — readiness metadata
- POST /check           — accepts a CHECK JSON payload with "appId" and responds with a RESPONSE JSON
whose manifestUrl points at "/<appId>/<manifest-name>" when present
- GET /<path>           — serves files rooted under --root, including "<appId>/<manifest-name>"
and "<appId>/<manifest-name>.sig"
HTTP mode is designed to be run behind a load balancer or reverse proxy in production.
TCP and UDP modes implement the same CHECK/RESPONSE selection semantics using the binary OUP framing.

### examples
oroc update server --root ./updates
serve manifests and bundles over HTTP on port 8080
oroc update server --root ./updates --tcp --port 9090
run a TCP OUP server on port 9090
oroc update server --root ./updates --udp --port 9090
run a UDP OUP server on port 9090

## oroc update-info
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Finfo

Query update servers or static manifests over HTTP/TCP/UDP.

### Usage
```bash
oroc update info [--transport=<http|tcp|udp>] [options]
```

### options
| Option | Description |
| --- | --- |
| --config=<path> | use an explicit oro.toml/oro.ini file for local CHECK request defaults |
| --transport=<http|tcp|udp> | transport to use (default: http) |
| --http | shorthand for --transport=http |
| --tcp | shorthand for --transport=tcp |
| --udp | shorthand for --transport=udp |
| --follow-manifest | when contacting servers, follow 'manifestUrl' in the RESPONSE and fetch/validate that manifest over HTTP(S) |
| --timeout-ms=<ms> | optional timeout for TCP/UDP CHECK requests (0 = no timeout) |
| --manifest-url=<url> | HTTP(S) URL of a manifest.json hosted statically |
| --signature-url=<url> | optional HTTP(S) URL of the corresponding signature JSON (default: derived from --manifest-url) |
| --keys=<file> | JSON file containing a public key ("publicKey" or "key" field) for manifest verification |
| --public-key=<hex> | Ed25519 public key as a hex string for manifest verification |
| --host=<host> | host for HTTP/TCP/UDP update servers (default: 127.0.0.1) |
| --port=<port> | port for HTTP/TCP/UDP update servers (default: 8080) |
| --app-id=<id> | application identifier to send in CHECK messages (default: active config meta.bundle_identifier when available) |
| --channel=<name> | update channel hint (default: active config update_channel or "stable") |
| --current-version=<version> | current application version hint (default: active config meta.version when available) |
| --runtime-version=<version> | runtime version hint advertised in CHECK (optional) |
| --platform=<id> | platform hint advertised in CHECK (optional) |
| --arch=<id> | architecture hint advertised in CHECK (optional) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
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

### examples
oroc update info --manifest-url https://cdn.example.com/app/manifest.json
inspect a statically hosted manifest
oroc update info --manifest-url https://cdn.example.com/app/manifest.json --keys app-pubkey.json
fetch and verify a statically hosted manifest + signature
oroc update info --http --host 127.0.0.1 --port 8080 --app-id com.example.app --follow-manifest
query an HTTP update server and then fetch the referenced manifest
oroc update info --tcp --host 127.0.0.1 --port 9000 --app-id com.example.app --follow-manifest
query a TCP update server using the binary OUP protocol

## oroc update-keygen
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fkeygen

Generate an Ed25519 keypair for signing update manifests.

### Usage
```bash
oroc update keygen [options]
```

### options
| Option | Description |
| --- | --- |
| --out=<path> | write keypair JSON to a file instead of stdout |
| --key-id=<id> | optional key identifier to embed in the keypair (default: "pk-1") |
| --log-file=<path> | mirror logs to a JSON file |

### notes
The generated JSON includes "keyId", "publicKey", and "privateKey" fields (hex-encoded).
Keep the private key secret; distribute only the public key with your application.

### examples
oroc update keygen > key.json
generate a default keypair and save it to key.json
oroc update keygen --key-id pk-prod --out prod-key.json
generate a named keypair for production use

## oroc update-sign
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fsign

Sign an update manifest and emit a detached manifest.sig file.

### Usage
```bash
oroc update sign [--manifest=<path>] (--keys=<file> | --private-key=<hex>) [options]
```

### options
| Option | Description |
| --- | --- |
| --manifest=<path> | path to the manifest JSON file to sign (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| --manifest-name=<name> | manifest filename to use when --manifest is not provided |
| --keys=<file> | JSON file containing a signing key (with "privateKey" or "secretKey" field) |
| --private-key=<hex> | Ed25519 private key as a hex string |
| --key-id=<id> | optional key identifier to embed in manifest.sig (default: "pk-1") |
| --out=<path> | output path for manifest.sig (default: <manifest-without-extension>.sig, e.g. manifest.json -> manifest.sig) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
The signature file is JSON containing "schemaVersion", "algorithm", "keyId", and "signature" fields.
Clients verify manifest bytes against manifest.sig and the configured public key(s).
Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename.

### examples
oroc update sign --keys key.json --manifest manifest.json
sign manifest.json using the private key in key.json
oroc update sign --private-key <hex-private-key> --manifest manifest.json --out manifest.sig
sign a manifest using a raw hex private key

## oroc update-verify
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fverify

Verify a manifest + signature pair using an Ed25519 public key.

### Usage
```bash
oroc update verify [--manifest=<path>] [--signature=<path>] (--keys=<file> | --public-key=<hex>) [options]
```

### options
| Option | Description |
| --- | --- |
| --manifest=<path> | path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| --manifest-name=<name> | manifest filename to use when --manifest is not provided |
| --signature=<path> | path to the manifest.sig JSON file (default: <manifest>.sig) |
| --keys=<file> | JSON file containing a public key ("publicKey" or "key" field) |
| --public-key=<hex> | Ed25519 public key as a hex string |
| --log-file=<path> | mirror logs to a JSON file |

### notes
Exits with status 0 when the signature is valid for the manifest and public key; non-zero otherwise.
Advanced: set ORO_UPDATE_MANIFEST_FILENAME or pass '--manifest-name' to change the default manifest filename
(the default signature path is derived as '<manifest-without-extension>.sig', e.g. manifest.json -> manifest.sig).

### examples
oroc update verify --keys key.json --manifest manifest.json
verify manifest.json against manifest.sig using the public key in key.json
oroc update verify --public-key <hex-public-key> --manifest manifest.json --signature manifest.sig
verify using an explicit hex-encoded public key and signature file

## oroc update-validate
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fvalidate

Validate an update manifest JSON file against the expected schema shape.

### Usage
```bash
oroc update validate [--manifest=<path>] [options]
```

### options
| Option | Description |
| --- | --- |
| --manifest=<path> | path to the manifest JSON file (default: manifest-name or ORO_UPDATE_MANIFEST_FILENAME) |
| --manifest-name=<name> | manifest filename to use when --manifest is not provided |
| --strict | enable additional consistency checks (for example, channels vs updates, artifactUrl shape) |
| --json | print a machine-readable JSON result object (for CI) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
This command parses the manifest and performs lightweight structural validation aligned with
'schemas/update-manifest.schema.json' (required fields, types, and key relationships).
It does not attempt full JSON Schema validation, but is suitable for fast local checks and CI.
When '--strict' is provided, additional consistency rules are enforced.
When '--json' is provided, stdout is reserved for the result object only.

### examples
oroc update validate --manifest manifest.json
run basic structural checks against manifest.json
oroc update validate --manifest manifest.json --strict
enable stricter consistency rules in addition to structural checks
oroc update validate --manifest manifest.json --json
print validation status as JSON for CI or agents

## oroc update-bundle
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fbundle

Build a tar archive containing the contents of a directory for use as an update artifact.

### Usage
```bash
oroc update bundle [--input=<dir>] [--output=<bundle.tar>] [options]
```

### options
| Option | Description |
| --- | --- |
| --input=<dir> | directory whose contents will be archived (default: project directory) |
| --output=<bundle.tar> | path to the tar archive to write (default: <build_name>-<version>.tar) |
| --manifest=<path> | optional manifest path to update with a new target for this bundle |
| --manifest-name=<name> | manifest filename to use when --manifest is not provided |
| --channel=<name> | update channel to associate with this bundle (default: update_channel or "stable") |
| --update-id=<id> | update id to associate with this bundle (default: <channel>-<version>) |
| --platform=<id> | platform identifier for the bundle target (default: "source") |
| --arch=<id> | architecture identifier for the bundle target (default: "any") |
| --artifact-url=<url-or-path> | artifactUrl to record in the manifest target (default: bundle filename) |
| --hash-algorithm=<sha256|sha1> | hash algorithm to use for the bundle payload (default: sha256 when libsodium is available, otherwise sha1) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
The archive is a plain tar file (no compression) built using the runtime's native tar implementation.
Directory layout and basic metadata (mode bits, mtime) are preserved.
When omitted, '--input' defaults to the project directory (app source),
and '--output' defaults to "<build_name>-<version>.tar" derived from your oro.toml metadata.
When '--manifest' or '--manifest-name' (or ORO_UPDATE_MANIFEST_FILENAME) is provided, the manifest is updated
with a new target entry describing this bundle (including length and hash).

### examples
oroc update bundle
bundle the current project source into <build_name>-<version>.tar
oroc update bundle --manifest manifest.json
bundle the project and record the artifact in manifest.json
oroc update bundle --input dist --output app-1.2.3.tar --manifest manifest.json --channel beta
bundle a custom directory and attach it as a beta update in the manifest

## oroc update-extract
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fupdate%2Fextract

Extract a tar archive produced by update-bundle into a destination directory.

### Usage
```bash
oroc update extract --bundle=<bundle.tar> --dest=<dir> [options]
```

### options
| Option | Description |
| --- | --- |
| --bundle=<bundle.tar> | path to the tar archive to extract |
| --dest=<dir> | destination directory to extract files into (created if missing) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
The extractor rejects absolute paths and any paths containing ".." or ':' to avoid directory traversal.
Special tar entries (symlinks, devices, etc.) are ignored; regular files and directories are restored.

### examples
oroc update extract --bundle app-1.0.0.tar --dest ./update-staging
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

### options
| Option | Description |
| --- | --- |
| --platform=<platform> | platform target to build application for (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| --config=<path> | use an explicit oro.toml/oro.ini file |
| --copy=<source:dest> | extra copy mapping (like [build] copy; can be repeated) |
| --port=<port> | load "index.html" from a specific port (if host is not specified, defaults to localhost) |
| --host=<host> | load "index.html" from a specific host (if port is not specified, defaults to 80) |
| --test[=path] | indicate test mode, optionally importing a test file relative to resource files |
| --headless | build application to run in headless mode (without frame or window) |
| --prod | build for production (disables debugging info, inspector, etc.) |
| -D, --debug | enable debug mode |
| -E, --env | add environment variables |
| -o, --only-build | only run build step, |
| -p, --package | package the app for distribution |
| -q, --quiet | hint for less log output |
| -r, --run | run after building |
| -V, --verbose | enable verbose output |
| -w, --watch | watch for changes to rerun build step |
| --allow-exec | allow external command execution (gates ndk-build/gradle/git/build scripts) |
| --sanitizers | enable developer sanitizers (ASan/UBSan) for desktop core builds |
| --tls-keylog=<path> | write NSS TLS key log lines to <path> (OpenSSL provider) |
| --log-file=<path> | mirror logs to a JSON file |

### environment
ORO_ALLOW_EXEC                       allow external exec during builds
ORO_ENABLE_SANITIZERS                enable ASan/UBSan on desktop builds

### notes
Relative paths are resolved from the project root or from the directory containing the explicit
`--config` file when one is supplied.
`--prod` controls packaging/debug defaults; `--package` controls whether a distributable artifact
is produced in addition to the build output directory.
Use `--copy` for one-off extra bundle mappings without modifying `oro.toml`.
NO_ANDROID and NO_IOS do not control this application build command. They are independent,
presence-based exclusions for runtime source bootstrap (`bin/install.sh` / `npm run relink`).

### common errors
Android builds: run `oroc setup --platform=android` and accept SDK licenses.
macOS/iOS signing: set `[ios] provisioning_profile`/specifier or `[mac.productbuild] identity` in your config file.
If you see "external command execution is disabled", pass `--allow-exec` or set `ORO_ALLOW_EXEC=1`.

### Linux options
| Option | Description |
| --- | --- |
| -f, --package-format=<format> | package a Linux application in a specified format for distribution:<br>- deb (default)<br>- rpm<br>- zip<br>- aur (generate AUR PKGBUILD only) |
| --sign | sign Linux packages with GPG (writes .asc next to the artifact) |
| --sign-key=<id> | optional GPG key ID or fingerprint to use with '--sign' (letters/digits/@._- only) |

### dependencies
Windows 10/11 SDK and Visual Studio Build Tools are recommended.
Ensure `signtool.exe` is available (set SIGNTOOL or add SDK bin to PATH).

### macOS options
| Option | Description |
| --- | --- |
| -c, --codesign | code sign application with 'codesign' |
| -n, --notarize | notarize application with 'notarytool' |
| -f, --package-format=<format> | package a macOS application in a specified format for distribution:<br>- zip (default)<br>- pkg |

### iOS options
| Option | Description |
| --- | --- |
| -c, --codesign | code sign application during xcoddbuild<br>(requires '[ios] provisioning_profile' in your config file) |

### Windows options
| Option | Description |
| --- | --- |
| -f, --package-format=<format> | package a Windows application in a specified format for distribution:<br>- appx (default) |

### examples
oroc build .
build the current project for the host platform
oroc build -r --debug .
rebuild and immediately run a debug build
oroc build --platform=ios --prod --package .
produce a signed/package-ready iOS build using your config
oroc build --copy assets:assets .
include an extra assets directory in the bundle for this build only

## oroc run
Web docs: https://oro.computer/runtime/docs/?p=cli%2Frun

Run application.
Provide a project directory, HTML file, or JavaScript module. When no
oro.toml is found, oroc infers a minimal configuration automatically.

### Usage
```bash
oroc run [options] [<project-or-source>]
```

### options
| Option | Description |
| --- | --- |
| --headless | run application in headless mode (without frame or window) |
| --platform=<platform> | platform target to run application on (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| --config=<path> | use an explicit oro.toml/oro.ini file |
| --port=<port> | load "index.html" from a specific port (if host is not specified, defaults to localhost) |
| --host=<host> | load "index.html" from a specific host (if port is not specified, defaults to 80) |
| --prod | build for production (disables debugging info, inspector, etc.) |
| --test[=path] | indicate test mode, optionally importing a test file relative to resource files |
| -D, --debug | enable debug mode |
| -q, --quiet | hint for less log output |
| -E, --env | add environment variables |
| -V, --verbose | enable verbose output |
| --allow-exec | allow external command execution (gates ndk-build/gradle/git/build scripts) |
| --tls-keylog=<path> | write NSS TLS key log lines to <path> (OpenSSL provider) |
| --log-file=<path> | mirror logs to a JSON file |

### environment
ORO_DEBUG                            enable debug mode (like -D)
ORO_VERBOSE                          enable verbose logs (like -V)

### common errors
For CI/Linux headless, install `xvfb-run` or set a custom headless runner in your config.
Use `--test=path` to run tests bundled with your app.

### examples
oroc run .
build and launch the current project on the host platform
oroc run --platform=android-emulator .
build and launch the current project on the Android emulator target
oroc run --headless --test=tests/smoke.js .
execute a bundled test entrypoint in headless mode

## oroc list-devices
Web docs: https://oro.computer/runtime/docs/?p=cli%2Flist-devices

Get the list of connected devices.

### Usage
```bash
oroc list-devices [options] --platform=<platform>
```

### options
| Option | Description |
| --- | --- |
| --platform=<platform> | platform target to list devices for:<br>- android<br>- ios |
| -f, --format=<format> | print as 'text' or 'json' |
| --json | shorthand for '--format=json' |
| --ecid | show device ECID (ios only) |
| --udid | show device UDID (ios only) |
| --only | only show ECID or UDID of the first device (ios only) |
| -V, --verbose | enable verbose output |
| --log-file=<path> | mirror logs to a JSON file |

### notes
Android output mirrors the device identifiers returned by `adb devices`.
iOS output can include the human-readable simulator/device name plus ECID or UDID for scripting.
JSON output emits an array of device objects with stable id fields for automation.

### examples
oroc list-devices --platform=android
list attached Android devices visible to adb
oroc list-devices --platform=ios --udid
print iOS devices with UDID values you can pass to --device
oroc list-devices --platform=ios --json
print connected iOS devices as JSON for scripts or agents

## oroc env
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fenv

Print environment variables relevant to the Oro CLI and build configuration.

### Usage
```bash
oroc env [options]
```

### options
| Option | Description |
| --- | --- |
| -f, --format=<format> | print as 'text' or 'json' |
| --json | shorthand for '--format=json' |
| -V, --verbose | enable verbose output |
| --log-file=<path> | mirror logs to a JSON file |

### notes
- Prints a curated set of CLI, runtime, source-bootstrap, toolchain, and platform variables (for example, ORO_DEBUG, ORO_VERBOSE, ORO_HOME, NO_ANDROID, NO_IOS, JAVA_HOME, ANDROID_HOME, APPLE_ID, SIGNTOOL).
- NO_ANDROID and NO_IOS are independent presence flags for runtime source bootstrap. A non-empty
NO_ANDROID disables only Android work; a non-empty NO_IOS disables only iOS/iOS Simulator work
on macOS. Values such as 0 and false still disable the named target. They do not select the
application target for `oroc build --platform`.
- Merges [env] / env_* entries from the active configuration and from local .ororc files when present.
- Filters out unset variables; text output prints each line as KEY=VALUE.
- JSON output emits a flat object keyed by environment variable name.

### examples
oroc env
print the effective environment inputs visible to Oro tooling
oroc env --json
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

### options
| Option | Description |
| --- | --- |
| --stdio | use stdio transport (default) |
| --http | use HTTP/SSE transport |
| --host=<host> | HTTP bind host (default: 127.0.0.1) |
| --port=<port> | HTTP bind port (default: 0 for ephemeral) |
| --endpoint=<path> | HTTP endpoint path (default: /mcp; normalizes mcp,/mcp,/mcp/) |
| --token=<token> | require this bearer token (default: auto-generated in HTTP mode) |
| --no-auth | disable token auth (loopback only) |
| --workspace=<path> | explicit workspace root (default: current directory or <workspace-dir>) |
| --config=<path> | workspace-relative oro.toml/oro.ini path to use as the active project config |
| --read-workspace-only | restrict filesystem reads to the workspace root (default; compatibility flag) |
| --allow-read-outside-workspace | explicitly allow reading arbitrary files outside the workspace root |
| --replace-sse-stream | allow a new SSE connection to replace an existing one for the same session id |

### notes
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

### examples
oroc mcp --stdio .
start an MCP server over stdio for the current workspace
oroc mcp --http --host=127.0.0.1 --port=8080 .
expose the MCP server over local HTTP/SSE

## oroc init
Web docs: https://oro.computer/runtime/docs/?p=cli%2Finit

Create a new project. If the path is not provided, the new project will be created in the current directory.

### Usage
```bash
oroc init [<project-dir>]
```

### options
| Option | Description |
| --- | --- |
| -C, --config | only create the config file |
| -n, --name | project name |

### notes
By default this command creates a starter `oro.toml`, `src/index.html`, `src/index.js`,
`src/serviceworker.js`, and `.gitignore`.
Use `--config` when you only want the config scaffold without the example app files.

### examples
oroc init my-app
create a new starter project in ./my-app
oroc init --config
write only the default oro.toml into the current directory

## oroc install-app
Web docs: https://oro.computer/runtime/docs/?p=cli%2Finstall-app

Install the app to the device or host target.

### Usage
```bash
oroc install-app [--platform=<platform>] [--device=<identifier>] [options]
```

### options
| Option | Description |
| --- | --- |
| -D, --debug | enable debug output |
| --device[=identifier] | identifier (ecid, ID) of the device to install to<br>if not specified, tries to run on the current device |
| --platform=<platform> | platform to install application to device (defaults to host)::<br>- android<br>- ios |
| --prod | install production application |
| -V, --verbose | enable verbose output |

### macOS options
| Option | Description |
| --- | --- |
| --target=<target> | installation target for macOS application (defaults to '/')<br>the application is installed into '$target/Applications' |

### common errors
Android: list devices with `adb devices` or pass `--device`.
iOS/macOS: list devices with `oroc list-devices --platform=ios` and pass `--device`.

### examples
oroc install-app --platform=android --device emulator-5554
install the current Android build onto a specific emulator
oroc install-app --platform=ios --device <udid>
install the current iOS build onto a specific simulator or device

## oroc print-build-dir
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fprint-build-dir

Print the build directory path

### Usage
```bash
oroc print-build-dir [--platform=<platform>] [--prod] [--root] [<project-dir>]
```

### options
| Option | Description |
| --- | --- |
| --platform | platform to print build directory for (defaults to host):<br>- android<br>- android-emulator<br>- ios<br>- ios-simulator |
| --prod | indicate production build directory |
| --root | print the root build directory |

### notes
This is useful for scripts that need to locate packaged artifacts, intermediate bundles, or
platform-specific staging directories without reimplementing Oro's path logic.

### examples
oroc print-build-dir .
print the host-platform build directory for the current project
oroc print-build-dir --platform=ios --prod .
print the production iOS build directory

## oroc setup
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fsetup

Setup build tools for host or target platform.

### Usage
```bash
oroc setup [options] [--platform=<platform>] [-y|--yes]
```

### options
| Option | Description |
| --- | --- |
| --platform=<platform> | platform target to run setup for (defaults to host):<br>- android<br>- ios<br>- linux<br>- windows |
| -q, --quiet | hint for less log output |
| -y, --yes | answer yes to any prompts |

### common errors
Without `--platform`, setup defaults to the host. Verify with `oroc env`.

### examples
oroc setup --platform=android
install or validate Android SDK/NDK requirements
oroc setup --platform=ios
install or validate host tools needed for iOS/macOS builds

## oroc config
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fconfig

Inspect configuration values.

### Usage
```bash
oroc config [options] [<key-or-path>]
```

### options
| Option | Description |
| --- | --- |
| --config=<path> | use an explicit oro.toml/oro.ini file |
| --list | list known configuration keys with current and default values |
| --key=<name> | print the current value for a specific key |
| --describe=<name> | print help and metadata for a specific key |
| -f, --format=<format> | print the full configuration as 'toml', 'ini', or 'json' |
| --json | shorthand for '--format=json' |
| --strict | treat unknown or unset keys as errors (non-zero exit) |
| --log-file=<path> | mirror logs to a JSON file |

### notes
Keys may be provided in flattened form (e.g., "filesystem_sandbox_enabled") or as TOML-style paths (e.g., "filesystem.sandbox_enabled").
A bare argument after 'config' is treated as a key query (for example, 'oroc config filesystem.sandbox_enabled').
Unknown keys are printed when present in the active configuration but are marked as undocumented.
`--format` prints the merged effective configuration after applying oro.toml, local .ororc overrides,
and CLI-selected config sources.
Use '--log-file=<path>' instead of the global 'oroc --json ...' flag when you need structured config output on stdout and logs at the same time.

### examples
oroc config --list
list all known configuration keys with effective values
oroc config meta.version
print the effective value of meta.version
oroc config --describe webview.root
print the description, default, and source for webview.root
oroc config "ios.*"
glob-match a group of related keys
oroc config --format json
print the merged effective configuration as JSON

## oroc versions
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fversions

Print Oro CLI/runtime and dependency versions.

### Usage
```bash
oroc versions [options] [<dependency-name>]
```

### options
| Option | Description |
| --- | --- |
| -f, --format=<format> | print as 'text' or 'json' |
| --json | shorthand for '--format=json' |
| -V, --verbose | enable verbose output |
| --log-file=<path> | mirror logs to a JSON file |

### notes
With no dependency name, prints the full version map for the CLI, runtime, and linked libraries.
Passing a dependency name filters to a single entry (for example: oro, oroc, uv, sqlite, iroh).
Use '--json' or '--format=json' when you need stable machine-readable output on stdout.
Use '--log-file=<path>' if you also need logs; the global 'oroc --json ...' form is reserved for JSON log streaming.

### examples
oroc versions
print all known dependency versions as text
oroc versions --json
print all known dependency versions as JSON
oroc versions uv
print only the libuv version

## oroc version
Web docs: https://oro.computer/runtime/docs/?p=cli%2Fversion

Inspect or bump the project version defined in your configuration file.

### Usage
```bash
oroc version [options]
oroc version <new-version | release> [options]
```

### options
| Option | Description |
| --- | --- |
| --config=<path> | explicit oro.toml/oro.ini to update |
| --preid=<id> | pre-release identifier for pre* / prerelease bumps (default: "rc") |
| --verbose, -V | enable verbose output |
| --log-file=<path> | mirror logs to a JSON file |

### notes
- With no arguments, 'version' prints the current semantic version from the [meta] section.
- With a <new-version> argument, it sets the version to that exact SemVer 2.0.0 value.
- With a release type, it bumps the version using SemVer rules:
major, minor, patch,
premajor, preminor, prepatch, prerelease
For pre* / prerelease bumps, '--preid' controls the pre-release tag (e.g., 'beta').
- The command updates only your app configuration file; it does not call git or create tags.
- oro.toml is the preferred config file, oro.ini is fully supported when oro.toml is absent.

### examples
oroc version
print the current project version
oroc version minor
bump version to the next minor
oroc version prepatch --preid beta
bump to the next patch and start a 'beta' pre-release
oroc version 1.2.3
set the version explicitly to 1.2.3
