# MCP Server Configuration

Oro Runtime ships with a lightweight MCP HTTP/SSE bridge. The bridge can be configured
statically through `oro.toml` or dynamically at runtime.

## CLI MCP server (`oroc mcp`)

The Oro CLI also ships with an MCP server that exposes common `oroc` workflows as MCP tools,
alongside safe workspace/config file access helpers.

The server is tuned for both human-operated clients and agentic tools:
- `initialize` returns a descriptive `instructions` string plus a titled `serverInfo` block.
- `tools/list` publishes richer tool metadata, including standard human-readable `title` values,
  standard MCP safety annotations, and a custom `metadata` object with guidance about when a
  specialized tool should be preferred over the generic `run_cli` fallback.
- Successful tool calls return `structuredContent` in addition to a text copy of the same JSON,
  so older clients still work while newer clients can consume typed results directly.

### Stdio transport

`oroc mcp` defaults to JSON-RPC over stdio, which is the recommended transport for Codex and
other local agent runners.

```sh
oroc mcp --stdio
```

### Streamable HTTP transport

Use `--http` to run a Streamable HTTP MCP endpoint. The CLI prints a single JSON line to
stdout describing the bound host/port/endpoint/token, then continues serving requests.

```sh
oroc mcp --http --host 127.0.0.1 --port 0 --endpoint /mcp
```

Notes:
- The CLI follows the MCP Streamable HTTP transport (2025-06-18): JSON-RPC requests return
  `Content-Type: application/json`; notifications/responses return `202 Accepted` with no body.
- Sessions are normally created during `initialize`. Subsequent requests must include
  `Mcp-Session-Id` (missing: `400`). If a client supplies an unknown session id (POST or SSE),
  the CLI will create a new session for that id so stateless clients keep working.
- The CLI supports `GET` SSE streams for server-to-client messages, scoped to a session id.
  Only one active SSE stream is allowed per session (second connection returns `409` unless
  `--replace-sse-stream` is used).
- Loopback auth defaults to disabled unless `--token` is provided or `[mcp].token` is set.
- `--endpoint` normalises common variants (`mcp`, `/mcp/`) to avoid client/server mismatches.
- The HTTP server binds ports exclusively (no `SO_REUSEPORT`) so multiple `oroc mcp --http`
  instances cannot share a host/port. This prevents cross-session and cross-workspace confusion.

### Tools and resources

The CLI exposes:
- Tools: `run_cli`, `search_docs`, workspace file read/write/list helpers, active project config read/write/validate,
  and convenience wrappers for common `oroc` commands (e.g., `build_app`, `run_app`).
- Tool metadata is intentionally descriptive:
  - Read-only tools advertise MCP safety hints such as `readOnlyHint` and `idempotentHint`.
  - Mutating tools advertise destructive/update behavior so clients can make better planning decisions.
  - Convenience wrappers such as `config_get`, `config_describe`, `build_app`, and `get_versions`
    should usually be preferred over `run_cli` because they express clearer intent, and the
    custom `metadata` object includes extra examples and hints when a client chooses to read it.
- `read_file` reads absolute paths outside the workspace root by default. Use
  `--read-workspace-only` to disable this capability.
- Resources: `workspace:/` (root listing), `workspace:/<active-config>` when the selected project
  config file exists (it uses the standard `oro.toml` or `oro.ini` filename), and additional resources discovered from the active config (build inputs,
  copy maps, icons) when present.
- If `mcp --config` is omitted, the server prefers `oro.toml` and falls back to `oro.ini` when that
  is the only standard project config present.
- `resources/list` also advertises documentation from two places:
  - `workspace:/...` for project-local docs under conventional paths when they exist in the current
    app workspace, such as `README.md`, `docs/`, `api/`, the active config file, and generated
    man trees.
  - `runtime-doc:/...` for the installed runtime distribution, including shipped README, MCP,
    LLM reference, API, CLI, config, and man1/man3/man7 docs when those files are installed.

### `search_docs`

Use `search_docs` when you know the topic you want but not the exact file or manpage yet.

- Inputs:
  - `query` (required): free-form keywords or phrase such as `ios signing`, `update manifest signature`, or `json logs`.
  - `scope` (optional): `all` (default), `workspace`, or `runtime`.
  - `max_hits` (optional): maximum result rows to return, default `20`, capped at `100`.
  - `max_files` (optional): maximum files to scan during the search, default `400`, capped at `1000`.
- Search corpus:
  - Workspace docs advertised through `resources/list`, including app-local README/docs/config/manpages when present.
  - Installed runtime docs advertised as `runtime-doc:/...`, including shipped README, MCP, CLI, config, API, and manpage trees when installed.
- Result shape:
  - `query`, `scope`, `truncated`
  - `results[]` entries with `uri`, `path`, `scope`, `title`, `description`, `mime_type`, `excerpt`, `score`, `matched_in`, and `line` when a content line matched.
- Ranking:
  - Exact phrase hits rank above token-only hits.
  - File metadata such as URI, title, and description contribute to ranking.
  - Content matches include short excerpts and line numbers so clients can jump directly into the right resource.

### Recommended client flow

For a new workspace or agent session, the lowest-friction sequence is:

1. Call `initialize` and read the returned `instructions`.
2. Call `workspace_info` to confirm the workspace root, config path, config_exists status, and filesystem policy.
3. Call `search_docs` when you already have a topic in mind (for example `ios signing`, `update manifest`, or `json logs`).
4. Call `resources/list` or `list_workspace` to discover docs and project layout.
5. Call `read_config` when `workspace_info.config_exists` is true or `resources/list` advertises `workspace:/<active-config>`.
6. Read whichever docs were advertised by `resources/list`:
   - Prefer `runtime-doc:/llms.txt`, `runtime-doc:/api/README.md`, `runtime-doc:/api/CONFIG.md`,
     `runtime-doc:/api/CLI.md`, `runtime-doc:/man1`, `runtime-doc:/man3`, or `runtime-doc:/man7`
     for runtime behavior, API contracts, and operator workflows.
   - Prefer `workspace:/...` resources for the current app’s config, README, local docs, or generated manpages when those exist in the project workspace.
7. Prefer specialized tools for common tasks:
   - `search_docs` for topic-driven discovery across README, MCP, API, config, and manpage resources
   - `config_get`, `config_describe`, `config_list`, `config_format`
   - `build_app`, `run_app`, `print_build_dir`, `list_devices`
   - `get_env`, `get_versions`, `project_version`
8. Use `run_cli` only when a dedicated tool does not cover the command or flag combination you need.

## oro.toml configuration

Add the optional `[mcp]` section to your app configuration to define defaults
the runtime uses whenever `mcp.startServer()` is invoked without overrides.

```ini
; [mcp]
; host = 127.0.0.1
; port = 0                  ; bind to an ephemeral port by default
; endpoint = /mcp           ; base HTTP/SSE endpoint
; token = my-shared-secret  ; static bearer token enforced by the runtime
; auth_timeout_ms = 5000    ; optional timeout used when awaiting dynamic auth handlers
```

- Setting `port = 0` instructs the runtime to bind to a random available port,
  which makes it easy to spin up multiple MCP servers in the same process.
- When `token` is provided the HTTP server enforces bearer authentication. The
  token still participates in the `Authorization` callback pipeline (see below),
  so you can mix static and dynamic checks.

## Dynamic Authorization

Use `mcp.setAuthorizationHandler()` to register a callback that approves or
rejects individual HTTP requests. The handler receives all available request
metadata and can return:

- `true` to accept the request.
- `false` (or throw) to reject the request with the default `401 Unauthorized`.
- An object `{ allow, status, message }` to customise both the decision and the
  HTTP response returned to clients.

```js
import mcp from 'oro:mcp'

await mcp.setAuthorizationHandler(({ authorization, headers }) => {
  // Reject requests that do not supply an Authorization header
  if (!authorization) {
    return { allow: false, status: 401, message: 'Missing bearer token' }
  }

  // Simple shared-secret check
  return authorization === `Bearer ${process.env.EXPECTED_TOKEN}`
})
```

Pass the same handler directly to `mcp.startServer({ authorize })` to configure
the server and set the callback in a single call.

```js
await mcp.startServer({
  port: 0, // bind to any free port
  authorize: ({ headers }) => headers['x-internal-key'] === 'expected-value',
})
```

Call `mcp.setAuthorizationHandler(null)` to remove the handler. Pending
authorization requests automatically fail closed when a handler is cleared or
the server shuts down.

## API Reference

The `oro:mcp` module now provides the following helpers in addition to the
existing registration functions:

| Function                          | Description                                                       |
| --------------------------------- | ----------------------------------------------------------------- |
| `mcp.startServer(options)`        | Starts the embedded server. Accepts `authorize` and `token`.      |
| `mcp.setAuthorizationHandler(fn)` | Registers or clears the dynamic authorization handler at runtime. |

See the generated TypeScript declarations shipped with the runtime in `api/index.d.ts`
(`MCPAuthorizationRequest`, `MCPAuthorizationDecision`, `MCPStartServerOptions`) for the full schema.
