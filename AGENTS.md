# Repository Guidelines

## Project Structure & Module Organization

- `api/`: JavaScript APIs exposed to apps (ESM). Types in `api/index.d.ts` (generated via `npm run gen`). NEVER modify `api/index.d.ts` or `api/index.tmp.d.ts`; both files are programmatically generated from parsing the JSDoc in `api/`, and `api/index.tmp.d.ts` is an intermediate artifact.
- Module specifier rule: every `oro:<module>` maps to `api/<module>.js`, and nested paths map 1:1 as well (`oro:fs/promises` → `api/fs/promises.js`).
- `src/`: Native runtime (C++20, plus Android Kotlin in `src/android`, iOS/desktop in platform dirs). Public headers in `include/`.
- `test/`: End‑to‑end and integration tests run via the Oro test runner.
- `bin/`: Development helpers (docs/types generation, protocol updates).
- `build/`: Generated artifacts and vendored third‑party code. This directory is ignored by git and rebuilt as needed; never edit files here or stage changes.
- `tmp/`: Ephemeral scratch space; content is not persisted and must never be referenced by build scripts or checked-in tooling. This directory is also ignored by git—do not modify its contents.

## Shared Workspace Safety (Git)

- Assume multiple agents may share the same working tree.
- NEVER run destructive or state-changing git commands unless the user explicitly asks (no `git checkout`/`switch`/`restore`/`reset`/`clean`/`stash`/`rebase`/`merge`/`cherry-pick`/`revert`, and never use `-f`/`--force`).
- NEVER discard or overwrite uncommitted work unless the user explicitly asks. This includes “manual restore” patterns that still destroy local edits, such as `git show HEAD:<file> > <file>`, copying files out of `.git/`, or otherwise replacing working-tree files to match `HEAD`.
- NEVER run filesystem-destructive commands unless the user explicitly asks (no `rm`/`unlink`/`find -delete`/cleanup scripts like `./bin/clean.sh`). Treat “cleanup” as destructive even when it targets ignored/generated directories.
- Before any potentially destructive operation, first inspect and preserve local changes (e.g., capture `git status --porcelain` and `git diff` output) and get explicit confirmation.
- If a clean baseline is needed, first preserve local changes (e.g., save `git diff`/`git status --porcelain` output to a patch file in the workspace) and get explicit confirmation before proceeding.
- Do not create branches, commits, tags, or pushes unless the user explicitly requests it.
- Prefer read-only inspection commands (`git status`, `git diff`, `git log`) when needed.

## Build, Test, and Development Commands

- `npm run gen`: Regenerate docs and TypeScript typings.
- `npm test`: Run desktop tests (installs `test` deps, uses the Oro test runner).
- `npm run test:android` | `npm run test:ios-simulator`: Run mobile tests.
- `npm run test:runtime-core`: Headless core tests.
- `npm run lint`: Authoritative repo-wide validation. Runs Standard for JS/MJS/CJS, checks generated documentation for drift, regenerates TypeScript declarations, verifies third-party dependency fetch defaults and recursive submodule fetches use CI-safe HTTPS GitHub URLs, checks the distinct `NO_ANDROID`/`NO_IOS` documentation contract, runs oxlint, checks Prettier-managed files, and runs cpplint. If `lint:docs` fails, run `npm run gen:docs` and include the generated updates; source line changes can affect man-page links even when API signatures are unchanged.
- `npm run lint:fix`: Applies supported auto-fixes, regenerates TypeScript declarations, and rewrites Prettier-managed files.
- `npm run test:lint`: Compatibility alias for `npm run lint`.
- `npm run test:lint:ci`: Compatibility alias for the CI-safe lint entrypoint with a writable Standard cache path.
- `npm run lint:cpp` uses `python3 -m cpplint`; install the Python `cpplint` package locally when you need to run the full lint suite outside CI.
- `npm run lint:deps` rejects SSH-style GitHub URL defaults and unsafe recursive submodule fetch paths in installer scripts so hosted CI and downstream builds do not depend on preconfigured SSH credentials.
- `npm run relink`: Link local CLI/module for app development.
- Prefer `VERBOSE=1 DEBUG=1 NO_ANDROID=1 NO_IOS=1 npm run relink` for an explicitly desktop-only rebuild with debug symbols and verbose logging.
- `NO_ANDROID` and `NO_IOS` are distinct presence switches. A non-empty `NO_ANDROID` disables only Android bootstrap/artifacts; a non-empty `NO_IOS` disables only iOS/iOS Simulator work on macOS. Neither implies the other, and values such as `0` or `false` still disable the named target. See `docs/BUILD_ENVIRONMENT.md`.
- `./bin/clean.sh`: Remove generated outputs.
- Use `ag` for repository text searches (prefer it over `rg`).
- Third-party networking deps:
  - Git-hosted native deps (e.g., `libuv`, `libusb`) are cloned into `build/` via `bin/install.sh`, we strip their `.git` metadata, and we never check the sources into the repository.
  - The UniFFI bindings ship via the local `rust/oro-iroh` crate; `bin/install.sh` builds it into `build/<arch>-<platform>/lib/liboro_iroh.*`.

## Coding Style & Naming Conventions

- JavaScript: StandardJS (ESM, no semicolons). Run `npm run lint`.
- Formatting ownership: Standard formats `*.js`, `*.mjs`, and `*.cjs`. Prettier formats non-JavaScript text formats. Generated `api/**/*.d.ts` files are validated by `npm run gen:tsc` instead of being hand-formatted.
- TypeScript: Types live in `api/*.d.ts`; keep declarations in sync with JS. ALWAYS add/update typings for any new or changed JS APIs (e.g., add `declare module` blocks in `api/index.d.ts`).
- Document every exported JavaScript API with TypeScript-style JSDoc (types, params, returns, events) so generated docs stay accurate.
- User JSDOC typedefs for options and return type objects that input to functions/methods instead of inlining them such as, inless an onbvious already declared type is available

```js
/**
 * @typedef {{ key: string, value: string|number }} SetOptions
 */

/**
 * @param {SetOptions} [options]
 */
function set(options) {}
```

- Read `CODE_STYLE.md` before touching runtime code to apply the latest JavaScript, C++, and C99 conventions.
- IPC calls from JS should use `ipc.request` for async round-trips. Reserve `ipc.send` for re-entrant flows that must not block an in-flight request, and use `ipc.sendSync` only when a blocking call is unavoidable.
- C++: C++20 (see `.clangd`/`compile_flags.txt`). Follow existing `.cc`/`.hh` patterns, RAII, and platform splits under `src/runtime/**`.
  - try/catch formatting: place `catch` on the same line as the closing brace of `try`, with a single space before parentheses.

    Example:

    ```
    try {
      // ...
    } catch (const std::exception& e) {
      // ...
    }
    ```

  - if/else formatting: mirror try/catch style by placing `else` or `else if` on the same line as the closing brace of the previous block.

    Example:

    ```
    if (expr) {
      // ...
    } else if (other) {
      // ...
    } else {
      // ...
    }
    ```

### C++ Formatting Checklist (src/)

- Braces: Use K&R style for `else`/`else if` and `catch` (same line as closing brace).
- One-liners: Do not place multiple statements in a single-line block. Expand into multiple lines.
- Close callbacks: Prefer `uv_close(handle, callback)` when cleanup is required (never rely on `nullptr` when state must be updated).
- Threading: Schedule libuv handle ops on the loop thread (e.g., via `loop.dispatch`).
- Consistent indentation and spacing: match surrounding code; avoid trailing whitespace.
- Naming: Use descriptive file names (e.g., `runtime/core/services/fs.cc`). Scopes should mirror directory names.

### Comments & Quality Bar

- NEVER say "best-effort" or "best effort" in comments. Use concrete, testable language: what is supported, what is stubbed, what errors are returned, and what the exact limits/caps are.
- "Best effort" is not enough: strive for robust, predictable, production-grade behavior. When full parity is impossible, make constraints explicit and ensure clients still get stable results (clear errors, bounded memory, no crashes).

## Testing Guidelines

- Place new tests under `test/src/<area>/...` next to related features.
- Prefer the built‑in Oro test harness (see `test/scripts/*`).
- Quick runs: `npm test` (desktop). Platform checks: mobile scripts above.
- Keep tests deterministic; avoid network unless explicitly required.

## Commit & Pull Request Guidelines

- Use Karma-style Conventional Commits with scope (e.g., `feat(cli): …`, `fix(api): …`, `refactor(runtime/udp): …`, `wip(core): …`). This means the type list follows the original Karma conventions (`feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`, etc.) and every message includes a scope plus a short, imperative summary.
- PRs must include: purpose, scope (api/runtime/platform), test plan (`npm test` output), and affected platforms.
- Link related issues; add screenshots/logs for UI/runtime behavior changes when relevant.
- Only commit/push/open PRs when the user explicitly requests it.

## Security & Configuration Tips

- Do not commit secrets. Local settings go in `.oro.env`.
- Do not run the automated test suites in this workspace; the packaged `oroc` CLI is unavailable so any `npm run test:*` invocation fails. Come back to the verification step once the artifacts are rebuilt or provided.
- Validate changes on at least one desktop target; mobile changes should include simulator/emulator runs.

## Configuration Assumptions

- Examples and tests should assume the current working directory is the project root (`.`). When adding example configs (`oro.toml`), use paths relative to `.` (e.g., `[build] copy_map = examples/copy.map.ini`).
- Never hardcode or assume a Service Worker mode. Do not default to `hybrid` in code, examples, or tests. Respect the project's `[webview] service_worker_mode` if set; otherwise, leave the runtime defaults intact.
- The autoindex feature is disabled by default and must be explicitly enabled via `[webview] autoindex = true`. Keep example configs explicit and minimal.

## Runtime Constraints

- The Oro runtime exposes only a subset of the Node.js standard library and does **not** provide `SharedArrayBuffer` or `Atomics.wait`. Always defensively gate those APIs and supply synchronous fallbacks (e.g., for sleep primitives) so behaviour remains correct when shared memory is unavailable.

## Linear Ticket Workflow

- Whenever we explicitly decide to work on a Linear ticket (e.g., "let's work on Linear ticket X"), follow this flow without skipping steps: (1) perform the ticket's tasks and keep its checkboxes in sync, (2) move the ticket to In Progress, (3) write the required code, and (4) audit and review the work.
- Only proceed with commit/push/open PR/`@codex` review when the user explicitly requests those steps.
- Tests are currently skipped by default because the packaged `oroc` toolchain is unavailable; note the omission in status updates/PRs.
- Do not move a Linear ticket to `Done` while its linked GitHub PR is still open; use `In Review` (or `In Progress` when appropriate) until the PR is merged or explicitly closed.

## Agent Execution Preferences

- Do not re-ask for confirmation once the user has given an instruction—just carry it out, escalating only when technically required by the sandbox.
- When a user explicitly asks you to "just do it" or to avoid confirmations/status preambles, execute the instruction directly without restating future intent (no "I will…" or "Next I’ll…" phrasing); continue to follow higher-priority safety and sandbox rules.
