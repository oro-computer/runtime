# Security and Hardening

Maintained by the Oro Runtime maintainers at `security@oro.computer`.

## Overview

- Oro Runtime includes several opt-in security controls. This guide summarizes the knobs and recommended defaults and explains how to reach the maintainers when you discover a vulnerability.

## Contact and Disclosure

- Email: `security@oro.computer` (goes to the Oro Runtime Maintainers security alias).
- Public issues: please **do not** file sensitive reports publicly. Use the email above; we target an initial response within 3 business days and coordinate fixes on private channels before disclosing.

## Supported Versions

| Version | Security fixes |
| ------- | -------------- |
| 0.1.x   | Supported      |
| < 0.1.0 | Not supported  |

Include the affected version, platform, impact, reproduction steps, and any suggested mitigation.
Do not include live credentials or personal data. We will acknowledge reports within three
business days, provide status updates as the investigation progresses, and coordinate disclosure
and credit with the reporter.

## CLI Environment Flags

- ORO_DEBUG / ORO_VERBOSE: enable debug mode and verbose logging (also available via `-D` / `-V`).
- ORO_ALLOW_EXEC: explicitly allow external command execution during builds.
- ORO_ENABLE_SANITIZERS: enable ASan/UBSan in desktop debug builds.

## Source-bootstrap target exclusions

- `NO_ANDROID=<non-empty>` excludes only Android SDK/NDK setup, Android ABI libraries, and staged
  Android artifacts from runtime source bootstrap.
- `NO_IOS=<non-empty>` excludes only iOS and iOS Simulator dependency, library, prebuild, and
  staging work on macOS.
- The variables are independent and neither disables a desktop target. A pipeline excluding both
  mobile families must name both. Because these are presence flags, `0` and `false` still disable
  the corresponding target; leave a variable unset or empty to enable it.
- These variables control `bin/install.sh` and `npm run relink`, not the target selected by
  `oroc build --platform`. See [Source-build environment](docs/BUILD_ENVIRONMENT.md).

## Exec Gating (CLI)

- Purpose: Prevent unintentional external command execution (e.g., ndk-build, gradle, git, custom build scripts).
- Enable per run: `oroc build --allow-exec` (also honored by `oroc run`).
- Environment: `ORO_ALLOW_EXEC=1` or `true`.
- Config (oro.toml): `[build] allow_exec = true`.
- Behavior: If disabled, the CLI aborts before executing external commands. Git sources are sanitized (protocol allowlist, control character filtering).

## Developer Sanitizers (desktop only)

- Purpose: Catch memory and undefined behavior defects during development.
- Enable per run: `oroc build --sanitizers` (adds `ORO_ENABLE_SANITIZERS=1`).
- Environment: `ORO_ENABLE_SANITIZERS=1`.
- Scope: Linux/macOS desktop only. Not for mobile or release builds.
- Overhead: Expect higher CPU/memory and possible 3rd-party false positives. Recommended for dedicated debug builds.

## File System Controls (runtime)

- Sandbox (non-Apple platforms):
  - Config: `filesystem_sandbox_enabled = true`
  - Behavior: Restricts access to mounted roots (`webview_navigator_mounts_*`) and well-known paths. Violations return SecurityError.
- No-Follow Symlinks:
  - Config: `filesystem_no_follow_symlinks = true`
  - Behavior: Denies traversing symlinks in the target path or any existing ancestor. Symlink creation (fs.symlink) and hard link creation (fs.link) are rejected when this policy is enabled.
- Disable Hard Links:
  - Config: `filesystem_disable_links = true`
  - Behavior: Rejects hard link creation requests (fs.link) independently of the symlink policy.

## WebView Security Headers and CORS

- Optional headers from config:
  - `webview_csp` → `Content-Security-Policy`
  - `webview_referrer_policy` → `Referrer-Policy`
- CORS:
  - `webview_cors_allow_all` (default true)
  - `webview_cors_allow_credentials` (default true)
  - `webview_cors_allow_headers`, `webview_cors_allow_methods`
  - `webview_cors_allowed_origins` (space-separated allowlist; reflected only when allow_all=false; adds `Vary: Origin`)

## Native Extensions (loading)

- Safe naming enforced: `[A-Za-z0-9_-]+`.
- Optional allowlisted roots:
  - `extensions_allowed_roots = "/abs/path1 /abs/path2"`
  - Extension library directory must resolve under one of the allowed roots when configured.

## Process Spawning (`oro:child_process`)

- `spawn` and `execFile` invoke executables directly on Unix and Windows,
  preserving empty arguments, whitespace, quotes, and trailing backslashes.
- `exec` and `execSync` use the platform shell by contract; do not interpolate
  untrusted input into their command strings.
- The internal U+0001 IPC delimiter is rejected from commands, arguments, and
  environment entries instead of being interpreted ambiguously.
- Explicit child environments replace inherited values on both Unix and Windows.

## MCP HTTP Security

- Bind to loopback unless the server is protected by a static bearer token or
  the built-in OAuth flow. Unauthenticated non-loopback binds are rejected.
- Requests carrying an `Origin` header are accepted only from loopback origins;
  this invariant is checked before dynamic authorization callbacks.
- Built-in OAuth requires a pre-registered client ID and redirect URI, S256
  PKCE, `resource` in authorization and token requests, protected-resource
  metadata, and audience-bound tokens. It uses operating-system CSPRNG bytes
  for authorization requests, codes, access tokens, and session IDs.
- The embedded listener is HTTP-only. Public OAuth deployments require a
  trusted TLS reverse proxy plus explicit public `issuer` and `resource` URLs.
- Request bodies, concurrent contexts, and per-stream event queues have finite
  defaults configurable through `mcp.startServer()`.

## Platform Hardening Flags

- Linux: `-Wl,-z,relro -Wl,-z,now` for hardened linking.
- Windows: `/NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA /guard:cf` via `-Xlinker`.
- Compile-time: `-fstack-protector-strong -D_FORTIFY_SOURCE=2`.

## Recommended Defaults (production)

- CLI: Do not enable `--allow-exec` in prod pipelines unless required.
- Sanitizers: Off in prod builds; use dedicated debug lane.
- Source target scope: Set `NO_ANDROID` and `NO_IOS` separately and explicitly for desktop-only
  bootstrap jobs; do not treat either variable as a combined mobile switch.
- Runtime: Enable `filesystem_sandbox_enabled` and `filesystem_no_follow_symlinks`.
- WebView: Provide a CSP and strict Referrer-Policy; set `webview_cors_allow_all=false` and allowlist necessary origins.
- Extensions: Use `extensions_allowed_roots` to constrain load locations.

## CLI Reference

- For task-oriented CLI usage, run `oroc help <query>` and see the generated
  [CLI reference](api/CLI.md).
- For environment and configuration inputs, use `oroc env`,
  `oroc config --describe <key>`, and the generated
  [configuration reference](api/CONFIG.md).

## Common Errors

- External exec disabled: The CLI blocked a tool invocation (e.g., ndk-build/gradle/git). Allow it temporarily with `oroc build --allow-exec` or `ORO_ALLOW_EXEC=1`. See Exec Gating above and `oroc help allow exec`.
- Missing SDK/toolchain: Run `oroc setup --platform=<host-or-target>`, confirm with `oroc env`, and use `oroc help setup` for platform-specific guidance.
- Codesign/Notary failures (Apple): Ensure identities and provisioning are set in `oro.toml`; use `oroc help ios signing` for the relevant configuration.
- Path issues: Verify `ORO_HOME` and `ORO_HOME_API`; `oroc env` prints the effective values.

## Windows Dispatcher Troubleshooting

- Symptom: Early UI/IPC calls appear ignored or delayed during app startup on Windows.
- Likely cause: Posting to the main thread before the message pump is ready. The dispatcher now queues pre‑ready callbacks and flushes after `notifyReady()`.
- Enable logs: Enable dispatcher debug logging to print construction, queue, post, and flush activity.
- Smoke tests: Run `test/src/window-dispatch-smoke.js` and `test/src/window-dispatch-early.js` on Windows.
- CI (Windows): `npm ci && npm run test:runtime-core && npm test`.
