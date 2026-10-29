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

## Project status

**Oro Runtime v0.2.1 is available** on [npm](https://www.npmjs.com/package/@oro-computer/runtime)
and as [release archives](https://github.com/oro-computer/runtime/releases/tag/v0.2.1).
The project is in active development in the `0.x` series: incompatible public API
changes require a minor release, while compatible fixes use a patch release.
See the [changelog](CHANGELOG.md) when upgrading.

Recent work includes:

- **Native platform services.** Backend discovery through `oro:platform`,
  authentication-bound keys, password-provider selection, foreground and deferred
  native work, UnifiedPush, and launcher integration on Android and Linux.
- **Android declarative workflows.** Bundled HTTPS read, checkpoint, and notification
  workflows without application native sources. These workflows and historical
  protected-import executors currently have Android backends only; they do not run
  arbitrary background JavaScript.
- **Developer tooling and distribution.** Project-aware language-server integration,
  a VS Code client, read-only SDK support, and expanded package installation and
  consumer-build checks. Version 0.2.1 hardens offline npm package verification and
  Android editor qualification.

Feature availability varies by platform. Use `oro:platform` and each feature's
support/status probes before offering native operations. The
[native platform inventory](docs/NATIVE_PLATFORM_PARITY.md) distinguishes implemented,
missing, and restricted backends; [runtime limitations](docs/LIMITATIONS.md) cover
TLS, USB/HID/Bluetooth, AI, and device-validation gaps.

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

Install the CLI with **Node.js 22 or newer**:

```sh
npm install --global @oro-computer/runtime
oroc --version
```

Installing from npm selects the matching native package for your host. For a specific
release, append its version, for example `@oro-computer/runtime@0.2.1`.

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

Published packages cover the following development hosts and bundled SDK targets:

| Development host | Architectures | Additional SDK artifacts |
| ---------------- | ------------- | ------------------------ |
| Linux            | x64, arm64    | Android on x64           |
| macOS            | x64, arm64    | iOS device and Simulator |
| Windows          | x64           | Desktop only             |

Target mobile builds with `oroc build --platform=android .` or
`oroc build --platform=ios .` using the appropriate host and SDKs. Platform APIs and device
support vary; see [current capabilities and limitations](docs/LIMITATIONS.md).

CI covers desktop hosts, Android Emulator, and iOS Simulator flows. Simulator and
emulator coverage does not establish physical-device or hardware-service support.

## Editors, automation, and agents

Oro provides a [language-server wrapper](docs/LANGUAGE_SERVER.md) for JavaScript/
TypeScript, Android Kotlin, C/C++/Objective-C, and Swift, plus a
[VS Code client](editors/vscode). Use the directory containing `oro.toml` as the
editor's project root and install the language-server backends you need separately.
Windows C-family language-server integration remains unavailable; JavaScript/
TypeScript editing and native compilation-model export are supported there.

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

| Resource                                                         | What you will find                                            |
| ---------------------------------------------------------------- | ------------------------------------------------------------- |
| [JavaScript APIs](api/README.md)                                 | Modules, methods, examples, and types                         |
| [CLI reference](api/CLI.md)                                      | Build, run, package, update, and inspection commands          |
| [Configuration](api/CONFIG.md)                                   | `oro.toml` settings and their behavior                        |
| [Examples](examples)                                             | Applications and focused integrations                         |
| [Architecture](docs/RUNTIME_ARCHITECTURE.md)                     | How the WebView, IPC bridge, and native services fit together |
| [Native platform support](docs/NATIVE_PLATFORM_PARITY.md)        | Backend availability, runtime probes, and qualification scope |
| [Declarative workflows](docs/DECLARATIVE_WORKFLOWS.md)           | Android background reads, checkpoints, and notifications      |
| [Android native integration](docs/ANDROID_NATIVE_INTEGRATION.md) | Back handling, periodic work, maintenance and encrypted push  |
| [Linux native integration](docs/LINUX_NATIVE_INTEGRATION.md)     | Native execution, scheduling, push, and desktop integration   |
| [Language servers](docs/LANGUAGE_SERVER.md)                      | Editor setup, compilation models, and backend requirements    |
| [Changelog](CHANGELOG.md)                                        | Release notes and compatibility changes                       |

Questions or a reproducible bug? Start with [Support](SUPPORT.md).
Contributions are welcome; see [Contributing](CONTRIBUTING.md) and the
[Code of Conduct](CODE_OF_CONDUCT.md). Report vulnerabilities through [Security](SECURITY.md).

## License

Oro Runtime is licensed under [Apache 2.0](LICENSE.txt).
See [NOTICE](NOTICE) and [third-party notices](THIRD_PARTY_NOTICES.md) for attribution.
