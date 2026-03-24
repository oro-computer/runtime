# CODE_STYLE

Authoritative reference for runtime code conventions. Keep this document updated as patterns evolve so new code remains consistent.

## JavaScript

- **StandardJS** tooling drives formatting for `*.js`, `*.mjs`, and `*.cjs`: two-space indentation, no semicolons, single quotes, and trailing commas only where ECMAScript permits. Run `npm run lint` after edits.
- **Parenthesis spacing** matches StandardJS: keep a space before the parameter list in named functions (`function openFile (path)`), but none inside the parentheses (`(path, options)`), and include spaces after keywords (`if (condition)`).
- **Prettier** is reserved for non-JavaScript text formats such as Markdown, JSON, YAML, and CSS. Generated API declarations under `api/**/*.d.ts` are validated by `npm run gen:tsc` rather than hand-formatted.
- **Named functions first.** Declare exports with the `function` keyword so stack traces and docs stay descriptive. Reserve arrow functions for cases that require lexical `this`/`arguments`, or for compact inline callbacks.
- **Prefer `const`.** Declare bindings with `const` and switch to `let` only when reassignment is inevitable (e.g. loop counters, incremental accumulators). `var` is never used.
- **Descriptive names.** Avoid single-letter identifiers outside of tight scopes; spell out intent (`value`, `handle`, `result`) so JSDoc and readers align.
- **Quotes.** Use single quotes for strings; switch to double quotes only when embedding an apostrophe or matching external formats (e.g. JSON payloads).
- **Private fields.** Use native `#privateField` declarations instead of pseudo-private `_fields`; expose data through getters/setters so callers trigger validation or coercion hooks.
- **Events.** Prefer DOM-style `EventTarget` over `EventEmitter` from `oro:events`; dispatch errors with the global `ErrorEvent` (extend it when custom fields or names are needed).

```js
/**
 * @param {string} path
 * @returns {Promise<Stats>}
 */
export async function readStats(path) {
  return await stats(path)
}

const timers = new Map()
const scheduleCleanup = () => timers.size === 0 && queueMicrotask(disposeTimers)
```

- **Arrow exception example.** When lexical scope is required (e.g. preserving `this` inside a class), use an arrow while keeping outer APIs as named functions.

```js
export class FileWatcher {
  constructor(path) {
    this.path = path
    this.onTick = () => checkForUpdates(this.path)
  }

  #intervalId = null

  get intervalId() {
    return this.#intervalId
  }

  set intervalId(value) {
    if (value !== null && typeof value !== 'number') {
      throw new TypeError('intervalId must be a timer handle')
    }
    this.#intervalId = value
  }
}
```

- **Whitespace** mirrors existing modules: keep `catch`/`else` on the closing brace line per C++ guidelines reused in JS.
- **Error handling.** Always propagate actionable errors; catch only to perform cleanup or throw a more specific error.

```js
export async function removeFile(path) {
  try {
    await FileHandle.unlink(path)
  } catch (err) {
    throw err
  }
}
```

- **Error causes.** When wrapping an error, use the `cause` option so the original stack remains traceable.

```js
export async function ensureDirectory(path) {
  try {
    await FileHandle.mkdir(path, { recursive: true })
  } catch (err) {
    throw new Error(`Failed to create directory ${path}`, { cause: err })
  }
}
```

## JSDoc with TypeScript Syntax

- Document every exported symbol with TypeScript-flavoured tags so `npm run gen` produces accurate declaration files.
- Import complex types inline via `import()` expressions to avoid circular imports.

```js
/**
 * Write bytes to disk.
 * @param {string|URL} path
 * @param {import('../buffer.js').Buffer|Uint8Array} data
 * @param {{ flag?: string, mode?: number }} [options]
 * @returns {Promise<void>}
 */
export async function writeFile(path, data, options = {}) {
  const handle = await FileHandle.open(
    path,
    options.flag,
    options.mode,
    options
  )
  await handle.write(data)
  await handle.close()
}
```

- Prefer `@throws {Error}` when rethrowing and `@see` for external references (e.g. Node.js docs). Use plural form (`@returns`) for readability, matching generated d.ts output.

## Re-export Pattern

- Modules that expose a namespaced surface (e.g. `oro:fs`) provide a thin entry point that re-exports the real implementation directory. This keeps the public path short and enables both named and default imports.

```js
// api/fs.js
import * as exports from './fs/index.js'
export * from './fs/index.js'
export default exports
```

- The `import * as exports` trick captures every named export for the default export. Always place aggregate logic in `api/<module>/index.js` (and submodules like `promises.js`) so tree-shaking stays predictable.

## Working with the fs API

- Mirror existing structure under `api/fs/`: group helpers (e.g. `dir.js`, `handle.js`, `promises.js`) and keep shared utilities (`normalizePath`, `visit`) near the top of `index.js` to aid documentation parsing.
- When adding new filesystem capabilities, document them with the patterns above and ensure both callback-style and `fs.promises` surfaces stay aligned.

## C++

- **Headers first.** Include local headers with quotes and keep the order broad-to-specific. See `src/runtime/ipc/router.cc` and `src/runtime/url/url.cc`.
- **Namespaces.** Open namespaces once and indent members with two spaces. Close with a trailing comment only when clarity requires it.
- **Function signatures.** Place a space before the parameter list (`void Router::init ()`) and align constructor initializer lists vertically:

```cpp
Router::Router (bridge::Bridge& bridge)
  : dispatcher(bridge.dispatcher),
    bridge(bridge) {}
```

- **Brace style.** Follow K&R: braces on the same line for `if/else`/`catch`, with `else` sharing the closing brace line (`if (...) { ... } else { ... }`).
- **Const correctness.** Use `const` for immutable values and references; prefer `const auto` when type deduction keeps code readable (`const auto key = toLowerCase(name);`).
- **Moves and ownership.** Use `std::move` when transferring ownership (`context = std::move(context)`), and default to range-based `for` loops; fall back to indexed loops only when the index is required.
- **Early returns.** Guard failure conditions immediately (`if (!this->bridge.active()) { return false; }`) to keep control flow shallow.
- **Lambdas.** Format multi-line captures vertically and place the parameter list on the same line as the capture body:

```cpp
return dispatcher.dispatch([
  this,
  callback = std::move(callback)
]() mutable {
  callback(result);
});
```

- **Error handling.** Prefer RAII over manual cleanup. When propagating exceptions, let them bubble (`throw;`) or wrap with a richer exception type as needed.

## C99

- **Headers.** Include `<stdbool.h>`, `<string.h>`, and other standard headers before project headers (see `include/oro/extension.h`).
- **Extern guards.** Wrap exported APIs in `ORO_RUNTIME_EXTENSION_EXTERN_BEGIN`/`END` so C++ consumers get `extern "C"` automatically.
- **Macros.** Use uppercase names with parentheses around arguments and align continuations with trailing backslashes:

```c
#define ORO_RUNTIME_EXTENSION_ABI_VERSION ((int) ( \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_MAJOR << 16 | \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_MINOR << 8  | \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_PATCH << 0   \
))
```

- **Typedefs and structs.** Declare opaque structs via forward typedefs (`typedef struct oapi_context oapi_context_t;`) and document fields with `/** ... */` blocks.
- **Function prototypes.** When parameter lists exceed ~80 chars, place one argument per line and align closing parentheses:

```c
ORO_RUNTIME_EXTENSION_EXPORT
oapi_context_t* oapi_context_create (
  oapi_context_t* parent,
  bool retained
);
```

- **Callbacks.** Alias function pointers with `typedef` for readability (`typedef void (*oapi_context_dispatch_callback)(oapi_context_t*, const void*);`).
- **Documentation.** Prefer block comments using the existing JSDoc-like style to describe params, return values, and ownership expectations.

## Platform Detection

- Always include `oro/platform.h` when branching on targets; avoid raw compiler defines like `__linux__` or `_WIN32`.
- Use `ORO_RUNTIME_PLATFORM_IS(WINDOWS)` / `ORO_RUNTIME_PLATFORM_IS(LINUX)` helpers or the specific constants (`ORO_RUNTIME_PLATFORM_ANDROID`, `ORO_RUNTIME_PLATFORM_IOS`, etc.); prefer the Oro-prefixed forms in new code.
- Prefer the semantic aliases: `ORO_RUNTIME_PLATFORM_UNIX` for POSIX code paths, `ORO_RUNTIME_PLATFORM_BSD` (and its `FREEBSD`/`OPENBSD` variants) for BSD-specific behavior, and `ORO_RUNTIME_PLATFORM_MOBILE` when gating shared mobile logic.
- Defaults resolve to `"unknown"` and `0`, so code should not guard against undefined macros.
