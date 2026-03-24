# Oro Runtime Release Automation (npm packages)

This document describes how the release scripts in this repo publish Oro Runtime
CLI/runtime packages to npm.

## Scripts

- `bin/version.sh`
  - Interactively bumps `VERSION.txt` and `clib.json`.
  - When the major/minor version changes, bumps `npm/packages/@orocomputer/runtime-node`.

- `bin/publish-npm-modules.sh`
  - Builds the CLI and runtime artifacts under a temporary publish root (`$ORO_HOME`).
  - Stages and publishes the Oro npm packages (top-level + platform variants).

- `bin/runtime-artifacts.sh`
  - Centralizes runtime artifact naming (`ORO_RUNTIME_ARTIFACT_NAME=oro-runtime`).
  - Provides helpers used by `bin/publish-npm-modules.sh` to locate and validate
    platform/arch outputs.

## Guardrails

- Release tooling is Oro-only: no legacy CLI names, package scopes, or artifact aliases.
- `test/unit/bootstrap-tooling.test.js` asserts the scripts stay Oro-only.
