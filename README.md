# Oro Runtime

**Build desktop and mobile apps with the web technologies you know.**

[![CI](https://github.com/oro-computer/runtime/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/oro-computer/runtime/actions/workflows/ci.yml)
[![Release builds](https://github.com/oro-computer/runtime/actions/workflows/release-artifacts.yml/badge.svg)](https://github.com/oro-computer/runtime/actions/workflows/release-artifacts.yml)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE.txt)

Oro brings HTML, CSS, and JavaScript to native applications on **Linux, macOS, Windows,
Android, and iOS**. Your interface runs in the platform's WebView, backed by a C++ runtime
that exposes native capabilities through JavaScript modules. The `oroc` CLI takes you from
your first window to building, packaging, and updating your app.

[Website](https://oro.computer/runtime) · [API reference](api/README.md) · [Examples](examples) · [Contributing](CONTRIBUTING.md)

## Why Oro?

- **Web interfaces, native capabilities.** Work with windows, files, networking, secure
  storage, and workers through `oro:*` imports. Extend the runtime with native code when
  your application needs more.
- **Data and compute on the device.** Build with SQLite, peer-to-peer connections through
  Iroh, and local model inference. Keep application logic close to the user's data.
- **A shared toolchain across platforms.** Create projects, inspect configuration, build
  apps, and prepare releases with one CLI and an explicit `oro.toml` configuration.
- **Discoverable by people and agents.** Generated TypeScript declarations, searchable
  CLI help, installed manuals, and an MCP server make the runtime accessible from your
  editor, terminal, and automation tools.

## Get started

> **Release status:** the first runtime release is in preparation. The npm packages currently
> contain bootstrap reservations, not installable runtime binaries. Use a [source build](#build-from-source)
> until `v0.1.0` is available.

Once `v0.1.0` is published, install the CLI with **Node.js 22 or newer**:

```sh
npm install --global @oro-computer/runtime@0.1.0
oroc --version
```

With `oroc` installed, create and launch your first app:

```sh
oroc init my-app
cd my-app
oroc build --run .
```

Edit your app's HTML, CSS, and JavaScript, and configure its build in `oro.toml`.
Use `oroc help <query>` to find your next step—for example, `oroc help updates` or
`oroc help ios signing`. Native builds require the toolchain and SDKs for the target platform.

## Native APIs, ordinary JavaScript

Inside an Oro app, import native functionality as ES modules. For example, query a SQLite
database directly from JavaScript:

```js
import { open } from 'oro:sqlite'

const db = open(':memory:')
db.exec('CREATE TABLE notes (body TEXT)')
db.exec('INSERT INTO notes (body) VALUES (?)', {
  params: ['Hello from Oro'],
})

const { rows } = db.exec('SELECT body FROM notes')
console.log(rows[0].body)
db.close()
```

Explore [`oro:application`](api/application.js), [`oro:fs/promises`](api/fs/promises.js),
[`oro:secure-storage`](api/secure-storage.js), and the [full API reference](api/README.md).
The runtime ships TypeScript declarations for editor completion and type checking.

## Platforms

The npm launcher selects the package for your development machine automatically.

| Development host | Architectures | Additional SDK artifacts |
| ---------------- | ------------- | ------------------------ |
| Linux            | x64, arm64    | Android on x64           |
| macOS            | x64, arm64    | iOS device and Simulator |
| Windows          | x64           | Desktop only             |

Target mobile builds with `oroc build --platform=android .` or
`oroc build --platform=ios .` using the appropriate host and SDKs. Platform APIs and device
support vary; see [current capabilities and limitations](docs/LIMITATIONS.md).

## Automation and agents

Connect an MCP client to your app workspace to search runtime documentation, inspect
configuration, and invoke build and run tools:

```sh
oroc mcp --stdio
```

The CLI also supports structured JSON output on applicable subcommands and separate log
files for automation. See the [MCP guide](docs/MCP.md) for client setup, HTTP transport,
and embedded application servers.

## Build from source

Source development requires Node.js 22+, pnpm 11, Python 3, a C++20 compiler, and the native
SDKs for your targets. From the repository root, install the JavaScript dependencies:

```sh
corepack enable
pnpm install --frozen-lockfile
```

For a desktop-only runtime, use Bash on Linux or macOS:

```sh
NO_ANDROID=1 NO_IOS=1 ./bin/install.sh
```

Or PowerShell on Windows:

```powershell
$env:NO_ANDROID = "1"
$env:NO_IOS = "1"
.\bin\install.ps1 -yesdeps
```

`NO_ANDROID` and `NO_IOS` are independent presence switches: any non-empty value—including
`0` or `false`—disables only the named mobile target family. Leave a variable unset to include
that target. Read the [source-build guide](docs/BUILD_ENVIRONMENT.md) for platform details and
[Contributing](CONTRIBUTING.md) for local linking, debug builds, and validation.

## Explore further

| Resource                                     | What you will find                                            |
| -------------------------------------------- | ------------------------------------------------------------- |
| [JavaScript APIs](api/README.md)             | Modules, methods, examples, and types                         |
| [CLI reference](api/CLI.md)                  | Build, run, package, update, and inspection commands          |
| [Configuration](api/CONFIG.md)               | `oro.toml` settings and their behavior                        |
| [Examples](examples)                         | Applications and focused integrations                         |
| [Architecture](docs/RUNTIME_ARCHITECTURE.md) | How the WebView, IPC bridge, and native services fit together |
| [Changelog](CHANGELOG.md)                    | Release notes and compatibility changes                       |

Questions or a reproducible bug? Start with [Support](SUPPORT.md).
Contributions are welcome; see [Contributing](CONTRIBUTING.md) and the
[Code of Conduct](CODE_OF_CONDUCT.md). Report vulnerabilities through [Security](SECURITY.md).

## License

Oro Runtime is licensed under [Apache 2.0](LICENSE.txt).
See [NOTICE](NOTICE) and [third-party notices](THIRD_PARTY_NOTICES.md) for attribution.
