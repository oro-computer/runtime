# Changelog

## Unreleased

Security & Hardening

- CLI exec gating with `--allow-exec`, env `ORO_ALLOW_EXEC`, and `[build] allow_exec=true`.
- Developer sanitizers (ASan/UBSan) for desktop via `--sanitizers` or `ORO_ENABLE_SANITIZERS=1`.
- Filesystem sandbox and symlink policies:
  - `filesystem_sandbox_enabled=true`
  - `filesystem_no_follow_symlinks=true` (also denies symlink creation)
  - `filesystem_disable_links=true` (denies hard links)
- WebView headers & CORS configuration:
  - CSP (`webview_csp`) and Referrer-Policy (`webview_referrer_policy`)
  - CORS allow-all default; allowlisted origin reflection when disabled
- Extension loading restrictions:
  - Safe names (`[A-Za-z0-9_-]+`)
  - `extensions_allowed_roots` allowlist
- Platform/linker hardening defaults:
  - Linux: `-Wl,-z,relro -Wl,-z,now`
  - Windows: `/NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA /guard:cf`
  - Compile-time: `-fstack-protector-strong -D_FORTIFY_SOURCE=2`

Tooling

- CLI uses `oroc` across npm entry points, build artifacts, and packaging metadata.

Fixes & Correctness

- HTTP Request: fix parsing of `Host` header port.
- Android: copyFile now writes asset/content bytes correctly in binary mode.
- process API: expose `process.versions.oro` while pinning `process.versions.socket` to `0.6.0`.
- runtime/webview: all platforms now append `OroRuntime/<version>` to the navigator user agent and diagnostics prefer the `isOroRuntime` flag.

Docs

- SECURITY.md and RELEASE_CHECKLIST.md added.
- README updated with Security section and CORS examples.
- Release communication docs: README now links to the download/rename/policy plans, and detailed Oro GA notes live in `docs/release/oro-runtime-0.6.0.md`.
- For full configuration and guidance, see SECURITY.md and RELEASE_CHECKLIST.md.
- License & legal notices: document Apache-2.0 licensing, Oro Runtime Maintainers publisher strings, and third-party license locations in README.
- feat(runtime/ai): autoload + prewarm default model
  - Reads `ai_llm_default_model` from userConfig to autoload a model on startup when AI is enabled.
  - Optional prewarm using `ai_llm_pool_prewarm` and `ai_llm_pool_prewarm_size`, capped by `ai_llm_pool_capacity`.
  - Guarded to avoid duplicate autoload when model already present.

- fix(runtime/ai): robust token piece handling
  - Replace fixed-size token piece buffers with dynamic thread-local buffers to avoid truncation and improve memory safety in streaming and tokenize paths.

- fix(runtime/ai): enforce tokenize input bounds
  - `/ai/llama/tokenize` now returns 413 for oversized inputs (aligned with prompt-size guard).

- fix(runtime/ai): LoRA load failure semantics
  - Return error if LoRA cannot be loaded; avoid half-initialized handles.

- fix(runtime/ai): correct listModels IPC payload shape
  - Always returns `{ data: [...] }`.

- perf(runtime/ai): sampler + usedTokens correctness
  - Remove duplicate sampler rebuild and duplicate token accounting during prefill.
- docs/runtime: clarify INI flattening
  - INI sections and keys flatten with underscores: `[a] b=1` → `a_b=1`, `[a.b.c] d=2` → `a_b_c_d=2`.
  - All per-model keys updated to use underscores with normalized model names (non-alphanumeric → `_`).
  - Examples: `ai_llm_model_model_gguf_n_threads`, `ai_llm_model_model_gguf_pool_prewarm_list`.
