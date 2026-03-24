Release Readiness Checklist

Use this checklist to prepare production builds. See SECURITY.md for details.

Build/CLI

- [ ] Do not pass `--allow-exec` in CI unless required; prefer prebuilt deps.
- [ ] Ensure `ORO_ENABLE_SANITIZERS` is NOT set for release builds.
- [ ] macOS/iOS: validate codesigning, notarization if applicable.
- [ ] Linux: confirm `-Wl,-z,relro -Wl,-z,now` in link flags (already default).
- [ ] Windows: confirm `/NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA /guard:cf` in link flags (already default).

Runtime Configuration (`oro.toml`)

- [ ] Enable file system sandbox:
  - `filesystem_sandbox_enabled = true`
- [ ] Deny symlink traversal:
  - `filesystem_no_follow_symlinks = true`
- [ ] Optionally disable hard links:
  - `filesystem_disable_links = true`

WebView Security

- [ ] Provide a CSP policy under `webview_csp`.
- [ ] Provide `webview_referrer_policy` (e.g., `no-referrer`).
- [ ] Restrict CORS:
  - `webview_cors_allow_all = false`
  - `webview_cors_allowed_origins = "https://app.example.com"`
  - `webview_cors_allow_credentials = false` (unless required)

Extensions

- [ ] If using native extensions, constrain load locations:
  - `extensions_allowed_roots = "/abs/path1 /abs/path2"`
- [ ] Verify extension names follow `[A-Za-z0-9_-]+`.

Misc

- [ ] Ensure debug logs are off for release (no verbose/debug env set).
- [ ] Validate app behavior under a standard user account (non-admin).
- [ ] Review permissions settings in `oro.toml` (notifications, media, etc.).

AI/LLM Runtime (if applicable)

- [ ] `ai_llm_enabled = true` only when shipping the AI server.
- [ ] Set `ai_llm_default_model` (optional) to autoload at startup.
- [ ] Tune pooling:
  - [ ] `ai_llm_pool_capacity` sized for device.
  - [ ] `ai_llm_pool_prewarm` and `ai_llm_pool_prewarm_size` for responsiveness.
- [ ] Validate `/ai/llama/health` shows expected pool/metrics.
- [ ] Optional IPC prewarm in build/start scripts:
  - `ipc://ai.llm.pool.prewarm?name=<model>&size=2048&count=2`.
