# Contributing

Thank you for helping improve Oro Runtime. Keep changes focused, open an issue or discussion for
large public API or architecture changes, and never include credentials or generated build
artifacts in a contribution. By participating, you agree to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Development setup

Source development requires Node.js 22 or newer, pnpm 11, Python 3, a C++20 compiler, and the
native SDKs for the platforms you build. Install `cpplint` for the complete validation suite.

```sh
corepack enable
pnpm install --frozen-lockfile
python3 -m pip install cpplint
```

For a desktop-only local debug build, use:

```sh
VERBOSE=1 DEBUG=1 NO_ANDROID=1 NO_IOS=1 npm run relink
```

The two mobile exclusions are independent. Any non-empty `NO_ANDROID` value disables only Android
bootstrap and artifacts; any non-empty `NO_IOS` value disables only iOS and iOS Simulator work on
macOS. Neither disables the other target family or a desktop build, and `0`/`false` still count as
set. Omit the variable for any target you intend to build. Read
[Source-build environment](docs/BUILD_ENVIRONMENT.md) before changing build or CI commands.

Do not edit `build/`, `tmp/`, or generated declaration files. Public `oro:*` modules live in
`api/`; native runtime code lives in `src/`; integration tests live in `test/src/`.

## Validation Commands

- `npm run lint`: authoritative repo-wide validation. Runs Standard for JavaScript and MJS sources, checks generated documentation for drift, regenerates TypeScript declarations, verifies third-party dependency fetch defaults and recursive submodule fetches use CI-safe HTTPS GitHub URLs, enforces the distinct `NO_ANDROID`/`NO_IOS` documentation contract, runs oxlint, checks Prettier-managed files, and runs cpplint.
- `npm run lint:fix`: applies the supported auto-fixes, regenerates TypeScript declarations, and rewrites Prettier-managed files.
- `npm run test:lint`: compatibility alias for `npm run lint`.
- `npm run test:lint:ci`: compatibility alias for the CI-safe lint entrypoint with a writable Standard cache path.

## Tool Ownership

- `standard` owns `*.js`, `*.mjs`, and `*.cjs`.
- `prettier` owns Markdown, JSON, YAML, CSS, and other non-JavaScript text formats.
- Generated API declarations under `api/**/*.d.ts` are validated by `npm run gen:tsc`, not hand-formatted.
- `oxlint` adds additional JavaScript diagnostics across the repo.
- `cpplint` validates the native C and C++ surface in `src/` and `include/`.
- `lint:cpp` runs `python3 -m cpplint`, so local environments need Python 3 plus the `cpplint` package. CI installs it explicitly.
- `lint:docs` compares documentation with the generator output without changing files. Missing, stale, or obsolete generated pages fail the check.
- `lint:deps` rejects SSH-style GitHub URL defaults and recursive submodule fetch paths in installer scripts so public dependency fetches remain usable on hosted CI and downstream machines without SSH credentials.
- `lint:build-env` rejects public help or documentation that mentions only one of the independent `NO_ANDROID` and `NO_IOS` controls and validates the canonical presence-flag contract.

## Generated Artifacts

- Run `npm run gen:tsc` after changing public JSDoc that affects shipped declarations.
- Run `npm run gen:docs` when `lint:docs` reports stale output. Even adding a line to an API implementation can change source links in generated man pages.
- Do not hand-edit generated declaration artifacts such as `api/index.d.ts` or `api/index.tmp.d.ts`.
- If `npm run lint` changes generated files, review and keep those updates with the source edits that required them.

## Commit and release signing

Contributors should follow the target branch's active signature requirements; this guide does not
require every contribution commit to be signed. Release maintainers must sign annotated release
tags, and GitHub must verify those signatures before publication can proceed.

The [signing guide](docs/release/SIGNING.md) covers SSH and GPG setup, GitHub key registration,
local allowed-signers configuration, checks without creating a tag, and release-tag verification.
It also explains optional commit signing and how to troubleshoot verification failures.

## Before Sending Changes

1. Run `npm run lint`.
2. Run the narrowest relevant tests, then the desktop suite with `npm test` when a rebuilt `oroc`
   is available. State the exact commands and platforms in the pull request.
3. If you changed docs or public APIs, also run `npm run gen`.
4. Keep source edits and their required generated artifacts in the same change.
5. Add a changelog entry for user-visible behavior, migration requirements, or security changes.

Pull requests should explain their purpose, affected API/runtime/platform surfaces, test plan, and
known limitations. Use a scoped Karma-style Conventional Commit title, such as
`fix(runtime/mcp): validate tool input schemas`.

Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md). General usage
questions and troubleshooting guidance are covered in [SUPPORT.md](SUPPORT.md).
