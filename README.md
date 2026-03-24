# Oro Runtime

Oro Runtime is the cross-platform runtime and CLI toolchain for Oro applications. A built distribution gives you the `oroc` CLI, the public `oro:*` JavaScript modules, generated API/config/CLI references, and installed manpages for downstream application development.

These docs assume you are using an installed runtime from an app workspace. They do not assume you are inside the runtime source repository.

This repository is the final legacy snapshot of the runtime. Active forward
development continues in the new Oro Runtime repository; this tree is being
preserved as the last legacy reference point.

## Installed Surface

- `oroc`: project setup, build, packaging, update, device, config, and inspection workflows.
- `api/README.md`: generated JavaScript API reference for public `oro:*` modules.
- `api/index.d.ts`: generated TypeScript declarations for the shipped public API.
- `api/CLI.md`: generated CLI reference.
- `api/CONFIG.md`: generated configuration reference for `oro.toml`, legacy `oro.ini`, and `.ororc`.
- `share/man/man1`: `oroc` command manuals.
- `share/man/man3`: JavaScript and C API manuals.
- `share/man/man7`: concepts, IPC, route catalogs, and workflow guides.
- `share/doc/oroc/{README.md,MCP.md,llms.txt,LEGACY_LIMITATIONS.md}`: installed high-level reference docs.

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
- Treat configuration as part of the API contract. `oro.toml`, legacy `oro.ini`, and `.ororc` often explain runtime behavior that would otherwise look surprising.
- Platform support varies. Check the installed docs for capability limits before assuming desktop, mobile, and browser-like surfaces are equivalent.

## Automation And Agents

- Use subcommand-local `--json` when a command supports structured stdout.
- Use `--log-file=<path>` when you need logs without polluting structured stdout.
- Use `oroc mcp --stdio` or `oroc mcp --http` when you want stable tool-oriented access instead of shell parsing.
- In MCP clients, prefer `runtime-doc:/...` resources for runtime reference and `workspace:/...` resources for app-local docs and config when present.
- Prefer specialized MCP tools such as `search_docs`, config helpers, build helpers, and version helpers before falling back to generic CLI execution.

## Need To Know

- `oro.toml` is the primary config format. `oro.ini` remains supported for legacy projects when `oro.toml` is absent.
- Do not assume a default service-worker mode. Respect the project config and the installed runtime docs.
- Autoindex is opt-in.
- The runtime does not guarantee `SharedArrayBuffer` or `Atomics.wait`; code that depends on shared memory needs a safe fallback.
- For frozen platform gaps and archive-era caveats, see [Legacy Runtime Limitations](/home/werle/repos/oro-computer/legacy-runtime/docs/LEGACY_LIMITATIONS.md).
