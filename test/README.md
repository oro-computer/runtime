## Test Runner

The test app now ships with a single orchestrator:

```bash
node ./scripts/run.js [target] [options]
```

Key targets:

- `desktop` (default) — builds and runs the full TAP suite.
- `runtime-core` — executes the native runtime-core harness.
- `android`, `android-emulator`, `ios-simulator`, `desktop-autoindex`, `desktop-spa`, `cli`, `cli-ip`, `node` — delegated to their existing scripts.

Run `node ./scripts/run.js --list-targets` to see the complete list.

### Developer Ergonomics

- `--quick` skips native test extensions, reuses the staged workdir/fixtures, and disables the strict lifecycle pass for faster iteration.
- `--entry ./filename.js` runs a specific desktop entry (defaults to `./index.js`).
- `--reuse-workdir`, `--refresh-workdir`, `--keep-workdir` control the isolated copy of the test app when extensions are disabled.
- `--skip-test-extensions` avoids building sqlite/runtime-core helpers.
- `--skip-desktop-extension` skips the heavyweight desktop extension build (helpful on CI or Linux workstations).
- Additional `--oroc-arg <value>` flags are forwarded directly to `oroc`.

### Filtering Tests

The built-in `oro:test` runner now honours the following environment variables (or matching CLI flags):

- `--match <regex>` / `ORO_TEST_GREP` — only run tests whose name matches the pattern.
- `--skip <regex>` / `ORO_TEST_SKIP` — skip tests whose name matches the pattern.
- `--only <pattern>` / `ORO_TEST_ONLY` — focus the run on selected tests.

Each pattern accepts comma-separated entries or `/regex/flags` syntax. Filter matches are reported in the TAP output.

### Dependency Management

`node ./scripts/run.js ...` installs `test/` dependencies on-demand unless `--skip-install` is provided. Use `--install` (or `--force-install`) to force a fresh install.
