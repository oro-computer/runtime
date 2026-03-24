Security & Hardening Guide (Oro Runtime Maintainers — security@oro.computer)

Overview

- Oro Runtime includes several opt-in security controls. This guide summarizes the knobs and recommended defaults and explains how to reach the maintainers when you discover a vulnerability.

Contact & Disclosure

- Email: `security@oro.computer` (goes to the Oro Runtime Maintainers security alias).
- Public issues: please **do not** file sensitive reports publicly. Use the email above; we target an initial response within 3 business days and coordinate fixes on private channels before disclosing.

CLI Environment Flags

- ORO_DEBUG / ORO_VERBOSE: enable debug mode and verbose logging (also available via `-D` / `-V`).
- ORO_ALLOW_EXEC: explicitly allow external command execution during builds.
- ORO_ENABLE_SANITIZERS: enable ASan/UBSan in desktop debug builds.

Exec Gating (CLI)

- Purpose: Prevent unintentional external command execution (e.g., ndk-build, gradle, git, custom build scripts).
- Enable per run: `oroc build --allow-exec` (also honored by `oroc run`).
- Environment: `ORO_ALLOW_EXEC=1` or `true`.
- Config (oro.toml): `[build] allow_exec = true`.
- Behavior: If disabled, the CLI aborts before executing external commands. Git sources are sanitized (protocol allowlist, control character filtering).

Developer Sanitizers (desktop only)

- Purpose: Catch memory and undefined behavior defects during development.
- Enable per run: `oroc build --sanitizers` (adds `ORO_ENABLE_SANITIZERS=1`).
- Environment: `ORO_ENABLE_SANITIZERS=1`.
- Scope: Linux/macOS desktop only. Not for mobile or release builds.
- Overhead: Expect higher CPU/memory and possible 3rd-party false positives. Recommended for dedicated debug builds.

File System Controls (runtime)

- Sandbox (non-Apple platforms):
  - Config: `filesystem_sandbox_enabled = true`
  - Behavior: Restricts access to mounted roots (`webview_navigator_mounts_*`) and well-known paths. Violations return SecurityError.
- No-Follow Symlinks:
  - Config: `filesystem_no_follow_symlinks = true`
  - Behavior: Denies traversing symlinks in the target path or any existing ancestor. Symlink creation (fs.symlink) and hard link creation (fs.link) are rejected when this policy is enabled.
- Disable Hard Links:
  - Config: `filesystem_disable_links = true`
  - Behavior: Rejects hard link creation requests (fs.link) independently of the symlink policy.

WebView Security Headers & CORS

- Optional headers from config:
  - `webview_csp` → `Content-Security-Policy`
  - `webview_referrer_policy` → `Referrer-Policy`
- CORS:
  - `webview_cors_allow_all` (default true)
  - `webview_cors_allow_credentials` (default true)
  - `webview_cors_allow_headers`, `webview_cors_allow_methods`
  - `webview_cors_allowed_origins` (space-separated allowlist; reflected only when allow_all=false; adds `Vary: Origin`)

Native Extensions (loading)

- Safe naming enforced: `[A-Za-z0-9_-]+`.
- Optional allowlisted roots:
  - `extensions_allowed_roots = "/abs/path1 /abs/path2"`
  - Extension library directory must resolve under one of the allowed roots when configured.

Process Spawning (runtime)

- Unix: Avoids shell by default (uses execvp with argv vector). Arguments preserved exactly.
- Windows: Uses CreateProcess without shell when possible, with robust quoting.
- JS `oro:child_process` preserves arg boundaries (0x01 delimiter through IPC → native argv).

Platform Hardening Flags

- Linux: `-Wl,-z,relro -Wl,-z,now` for hardened linking.
- Windows: `/NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA /guard:cf` via `-Xlinker`.
- Compile-time: `-fstack-protector-strong -D_FORTIFY_SOURCE=2`.

Recommended Defaults (Prod)

- CLI: Do not enable `--allow-exec` in prod pipelines unless required.
- Sanitizers: Off in prod builds; use dedicated debug lane.
- Runtime: Enable `filesystem_sandbox_enabled` and `filesystem_no_follow_symlinks`.
- WebView: Provide a CSP and strict Referrer-Policy; set `webview_cors_allow_all=false` and allowlist necessary origins.
- Extensions: Use `extensions_allowed_roots` to constrain load locations.

CLI Reference

- For CLI usage and common workflows, see README.md (CLI section).
- For environment flags that affect the CLI, see README.md (Environment Variables section).

Common Errors

- External exec disabled: The CLI blocked a tool invocation (e.g., ndk-build/gradle/git). Allow it temporarily with `oroc build --allow-exec` or `ORO_ALLOW_EXEC=1`. See README (Environment Variables) and Exec Gating above.
- Missing SDK/toolchain: Run `oroc setup --platform=<host-or-target>` and confirm with `oroc env`. See README (Troubleshooting).
- Codesign/Notary failures (Apple): Ensure identities and provisioning are set in your config (`oro.toml`). See README (Troubleshooting).
- Path issues: Verify `ORO_HOME`/`ORO_HOME_API`. `oroc env` prints current values. See README (Environment Variables).

Windows Dispatcher Troubleshooting

- Symptom: Early UI/IPC calls appear ignored or delayed during app startup on Windows.
- Likely cause: Posting to the main thread before the message pump is ready. The dispatcher now queues pre‑ready callbacks and flushes after `notifyReady()`.
- Enable logs: Enable dispatcher debug logging to print construction, queue, post, and flush activity.
- Smoke tests: Run `test/src/window-dispatch-smoke.js` and `test/src/window-dispatch-early.js` on Windows.
- CI (Windows): `npm ci && npm run test:runtime-core && npm test`.
