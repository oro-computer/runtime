# Contributing

## Validation Commands

- `npm run lint`: authoritative repo-wide validation. Runs Standard for JavaScript and MJS sources, regenerates TypeScript declarations, verifies third-party dependency fetch defaults and recursive submodule fetches use CI-safe HTTPS GitHub URLs, runs oxlint, checks Prettier-managed files, and runs cpplint.
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
- `lint:deps` rejects SSH-style GitHub URL defaults and recursive submodule fetch paths in installer scripts so public dependency fetches remain usable on hosted CI and downstream machines without SSH credentials.

## Generated Artifacts

- Run `npm run gen:tsc` after changing public JSDoc that affects shipped declarations.
- Do not hand-edit generated declaration artifacts such as `api/index.d.ts` or `api/index.tmp.d.ts`.
- If `npm run lint` changes generated files, review and keep those updates with the source edits that required them.

## Before Sending Changes

1. Run `npm run lint`.
2. If you changed docs or public APIs, also run `npm run gen`.
3. Keep source edits and their required generated artifacts in the same change.
