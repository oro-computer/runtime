# Oro Runtime

Oro Runtime is the cross-platform runtime and CLI toolchain for Oro applications. A built distribution gives you the `oroc` CLI, the public `oro:*` JavaScript modules, generated API/config/CLI references, and installed manpages for downstream application development.

These docs assume you are using an installed runtime from an app workspace. They do not assume you are inside the runtime source repository.

This repository is the active Oro Runtime source tree.

## Install

Oro Runtime release packages are published under the `@oro-computer` npm
scope. After `v0.1.0` is published, install the CLI with:

```sh
npm install --global @oro-computer/runtime
oroc --version
```

Node.js 22 or newer is required by the npm launcher. Platform packages are
selected automatically for Linux, macOS, and Windows on supported CPU
architectures.

## Build From Source

Source builds require Node.js 22 or newer, pnpm 11, Python 3, a C++20
toolchain, and the platform SDKs for the targets being built.

```sh
corepack enable
pnpm install --frozen-lockfile
NO_ANDROID=1 NO_IOS=1 ./bin/install.sh
build/*-desktop/bin/oroc --version
```

`NO_ANDROID` and `NO_IOS` are deliberately separate, presence-based source-build controls:

- `NO_ANDROID=<non-empty>` disables only Android setup, ABI libraries, and staged Android
  artifacts. It does not disable iOS or any desktop target.
- `NO_IOS=<non-empty>` disables only iOS and iOS Simulator dependency, library, prebuild, and
  staging work on macOS. It does not disable Android or macOS desktop.

Values such as `0` and `false` are non-empty and therefore still disable the named target. Leave a
variable unset or empty to enable that target. These variables control runtime source bootstrap;
they do not replace `oroc build --platform=<platform>` for application builds. See the complete
[source-build environment contract](docs/BUILD_ENVIRONMENT.md).

Use `VERBOSE=1 DEBUG=1 NO_ANDROID=1 NO_IOS=1 npm run relink` for an explicitly desktop-only local
debug build linked into your development environment. See [Contributing](CONTRIBUTING.md) for
validation and platform prerequisites.

When Android is enabled, source bootstrap installs the SDK, NDK, and build tools
needed to compile both supported Android ABIs. It does not download an emulator
or system image. Emulator workflows provision one image matching the host
architecture on demand; see the
[source-build environment contract](docs/BUILD_ENVIRONMENT.md#android-build-and-emulator-packages).

## Canonical Links

- Source repository: https://github.com/oro-computer/runtime
- Project website: https://oro.computer/runtime
- Top-level JavaScript module docs live under `https://oro.computer/runtime/docs/?p=javascript%2F<module>` (for example, `oro:application` maps to `https://oro.computer/runtime/docs/?p=javascript%2Fapplication`).
- CLI command docs live under `https://oro.computer/runtime/docs/?p=cli%2F<path>` where `oroc` maps to `cli/oroc`, `oroc run` maps to `cli/run`, and nested commands map by path segments such as `oroc update init` -> `cli/update/init`.
- Nested JavaScript namespace modules do not have their own website URLs; use the installed/generated docs and manpages for those entries.

## Installed Surface

- `oroc`: project setup, build, packaging, update, device, config, and inspection workflows.
- `api/README.md`: generated JavaScript API reference for public `oro:*` modules.
- `api/index.d.ts`: generated TypeScript declarations for the shipped public API.
- `api/CLI.md`: generated CLI reference.
- `api/CONFIG.md`: generated configuration reference for `oro.toml`, the `oro.ini` fallback, and `.ororc`.
- `share/man/man1`: `oroc` command manuals.
- `share/man/man3`: JavaScript and C API manuals.
- `share/man/man7`: concepts, IPC, route catalogs, and workflow guides.
- `share/doc/oroc/{README.md,MCP.md,llms.txt,LIMITATIONS.md}` and
  `share/doc/oroc/docs/BUILD_ENVIRONMENT.md`: installed high-level reference docs.

## Start Here

1. Run `oroc help <query>` for task-oriented discovery such as `oroc help ios signing`, `oroc help json logs`, or `oroc help updates`.
2. Create a project with `oroc init my-app`.
3. Inspect the generated `oro.toml`.
4. Run the app with `oroc run my-app` or build it with `oroc build my-app`.
5. Use `oroc config --describe <key>` and `oroc env` to inspect effective configuration and environment inputs.
6. Use `man 3 <module>`, `man 7 <guide>`, `api/README.md`, and `api/index.d.ts` when you need deeper API detail.

## API Discovery

- Prefer public high-level modules such as `oro:application`, `oro:window`, `oro:fs/promises`, and `oro:secure-storage` before dropping to `oro:ipc`.
- Use section 3 manpages and `api/index.d.ts` for exact contracts.
- Use section 7 manpages for concepts, transport guidance, and route catalogs.
- Treat configuration as part of the API contract. `oro.toml`, the `oro.ini` fallback, and `.ororc` often explain runtime behavior that would otherwise look surprising.
- Platform support varies. Check the installed docs for capability limits before assuming desktop, mobile, and browser-like surfaces are equivalent.

## Automation And Agents

- Use subcommand-local `--json` when a command supports structured stdout.
- Use `--log-file=<path>` when you need logs without polluting structured stdout.
- Use `oroc mcp --stdio` or `oroc mcp --http` when you want stable tool-oriented access instead of shell parsing.
- In MCP clients, prefer `runtime-doc:/...` resources for runtime reference and `workspace:/...` resources for app-local docs and config when present.
- Prefer specialized MCP tools such as `search_docs`, config helpers, build helpers, and version helpers before falling back to generic CLI execution.

## Need To Know

- `oro.toml` is the primary config format. `oro.ini` is used as a fallback when `oro.toml` is absent.
- Do not assume a default service-worker mode. Respect the project config and the installed runtime docs.
- Autoindex is opt-in.
- The runtime does not guarantee `SharedArrayBuffer` or `Atomics.wait`; code that depends on shared memory needs a safe fallback.
- For current platform gaps and compatibility caveats, see [Runtime Limitations](docs/LIMITATIONS.md).

## License

Apache-2.0. See [LICENSE.txt](LICENSE.txt), [NOTICE](NOTICE), and
[third-party notices](THIRD_PARTY_NOTICES.md).
