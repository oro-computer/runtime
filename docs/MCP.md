# MCP Server Configuration

Oro Runtime ships with MCP servers for the `oroc` CLI and embedded applications. Both servers
implement MCP `2026-07-28` and retain explicit compatibility with MCP `2025-11-25` and
`2025-06-18` clients.
The embedded HTTP server can be configured statically through `oro.toml` or dynamically at
runtime.

## CLI MCP server (`oroc mcp`)

The Oro CLI also ships with an MCP server that exposes common `oroc` workflows as MCP tools,
alongside safe workspace/config file access helpers.

The server is tuned for both human-operated clients and agentic tools:

- `server/discover` returns the server description, version, capabilities, and instructions for
  modern clients. Legacy clients use `initialize`.
- `tools/list` publishes human-readable titles, JSON input schemas, icons, standard
  safety annotations, and extension data under `_meta`.
- Successful tool calls may return any JSON value in `structuredContent`. Arrays, primitives,
  and `null` also include their serialized JSON in a text content block for compatibility.
- Tool schemas are compiled and validated at registration. Schemas without `$schema` use JSON
  Schema 2020-12; explicitly declared Draft 4, 6, 7, and 2019-09 schemas are also supported.
  Unsupported dialects, invalid schemas, and external references are rejected without network
  access. Input and output validation is bounded to 64 nested levels and 10,000 JSON values.
- Successful structured results are validated against `outputSchema` before they are returned.
  For legacy clients, only object-root output schemas are advertised and non-object structured
  values are wrapped in `{ "value": ... }` on the wire.
- Modern responses identify their result shape through the top-level `resultType` field.

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

- Modern `2026-07-28` requests are stateless. Each request includes protocol version and client
  capability data in `_meta`; clients discover the server with `server/discover`. Modern clients
  do not call `initialize` or send `Mcp-Session-Id`.
- Modern server-to-client events use `subscriptions/listen` over a long-lived HTTP POST response
  with `Content-Type: text/event-stream`. The subscription is acknowledged before events begin.
- Modern requests require `MCP-Protocol-Version` and `Mcp-Method`, plus `Mcp-Name` for named
  operations. The server decodes mirrored values and rejects missing or body-mismatched headers
  with `HeaderMismatch`.
- A tool parameter may opt into `Mcp-Param-*` mirroring with `x-mcp-header` when its schema is
  reachable from the input root solely through `properties` keys. Nested properties are allowed;
  paths through arrays, composition, conditionals, or `$ref` are rejected during registration.
  Only singular `string`, `integer`, and `boolean` property types are supported, integer values
  must stay in the JavaScript safe range, and header names must be unique ASCII HTTP tokens.
- Legacy `2025-11-25` and `2025-06-18` clients continue to use `initialize`,
  `Mcp-Session-Id`, and GET SSE streams. The server returns the requested version when it is
  supported, otherwise it offers `2025-11-25` as its newest handshake-era revision.
  Unknown legacy session IDs return `404`; the server never silently creates a replacement
  session. Only one GET SSE stream is active per legacy session unless `--replace-sse-stream` is
  used.
- HTTP mode requires bearer authentication by default and prints a cryptographically random token
  in its startup JSON. Pass `--token` to choose the token. `--no-auth` is an explicit loopback-only
  opt-out and is rejected for non-loopback hosts.
- Requests with an `Origin` header are accepted only from a loopback origin. Credentials must be
  supplied in `Authorization`, not query parameters.
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
    `_meta` object includes extra examples and hints when a client chooses to read it.
- File reads are workspace-scoped by default. Use `--allow-read-outside-workspace` only when a
  trusted local client genuinely requires arbitrary host file access. `--read-workspace-only`
  remains as an explicit compatibility flag for the secure default.
- Resources: `workspace:/` (root listing), `workspace:/<active-config>` when the selected project
  config file exists (it uses the standard `oro.toml` or `oro.ini` filename), and additional resources discovered from the active config (build inputs,
  copy maps, icons) when present.
- If `mcp --config` is omitted, the server prefers `oro.toml` and falls back to `oro.ini` when that
  is the only standard project config present.
- `resources/list` also advertises documentation from two places:
  - `workspace:/...` for project-local docs under conventional paths when they exist in the current
    app workspace, such as `README.md`, `docs/`, `api/`, the active config file, and generated
    man trees.
  - `runtime-doc:/...` for the installed runtime distribution, including shipped README,
    `BUILD_ENVIRONMENT.md`, MCP, LLM reference, API, CLI, config, and man1/man3/man7 docs when
    those files are installed. The build-environment resource explicitly distinguishes
    `NO_ANDROID` (Android-only exclusion) from `NO_IOS` (iOS/iOS Simulator-only exclusion); both
    are presence flags and neither selects an `oroc build --platform` target.

### `search_docs`

Use `search_docs` when you know the topic you want but not the exact file or manpage yet.

- Inputs:
  - `query` (required): free-form keywords or phrase such as `ios signing`, `update manifest signature`, or `json logs`.
  - `scope` (optional): `all` (default), `workspace`, or `runtime`.
  - `max_hits` (optional): maximum result rows to return, default `20`, capped at `100`.
  - `max_files` (optional): maximum files to scan during the search, default `400`, capped at `1000`.
- Search corpus:
  - Workspace docs advertised through `resources/list`, including app-local README/docs/config/manpages when present.
  - Installed runtime docs advertised as `runtime-doc:/...`, including shipped README,
    `BUILD_ENVIRONMENT.md`, MCP, CLI, config, API, and manpage trees when installed.
- Result shape:
  - `query`, `scope`, `truncated`
  - `results[]` entries with `uri`, `path`, `scope`, `title`, `description`, `mime_type`, `excerpt`, `score`, `matched_in`, and `line` when a content line matched.
- Ranking:
  - Exact phrase hits rank above token-only hits.
  - File metadata such as URI, title, and description contribute to ranking.
  - Content matches include short excerpts and line numbers so clients can jump directly into the right resource.

### Recommended client flow

For a new workspace or agent session, the lowest-friction sequence is:

1. Call `server/discover` with MCP `2026-07-28` request metadata and read the returned
   `instructions`. A legacy `2025-11-25` or `2025-06-18` client calls `initialize` instead.
2. Call `workspace_info` to confirm the workspace root, config path, config_exists status, and filesystem policy.
3. Call `search_docs` when you already have a topic in mind (for example `ios signing`, `update manifest`, or `json logs`).
4. Call `resources/list` or `list_workspace` to discover docs and project layout.
5. Call `read_config` when `workspace_info.config_exists` is true or `resources/list` advertises `workspace:/<active-config>`.
6. Read whichever docs were advertised by `resources/list`:
   - Prefer `runtime-doc:/BUILD_ENVIRONMENT.md` for source-bootstrap questions. It defines
     `NO_ANDROID` and `NO_IOS` independently and explains their presence semantics.
   - Prefer `runtime-doc:/llms.txt`, `runtime-doc:/api/README.md`, `runtime-doc:/api/CONFIG.md`,
     `runtime-doc:/api/CLI.md`, `runtime-doc:/man1`, `runtime-doc:/man3`, or `runtime-doc:/man7`
     for runtime behavior, API contracts, and operator workflows.
   - Prefer `workspace:/...` resources for the current app’s config, README, local docs, or generated manpages when those exist in the project workspace.
7. Prefer specialized tools for common tasks:
   - `search_docs` for topic-driven discovery across README, source-build environment, MCP, API,
     config, and manpage resources
   - `config_get`, `config_describe`, `config_list`, `config_format`
   - `build_app`, `run_app`, `print_build_dir`, `list_devices`
   - `get_env`, `get_versions`, `project_version`
8. Use `run_cli` only when a dedicated tool does not cover the command or flag combination you need.

## Embedded server configuration

`oroc mcp --http` reads optional host, port, endpoint, and token defaults from
the active workspace's `oro.toml`. Command-line flags override them:

```ini
[mcp]
host = "127.0.0.1"
port = 0
endpoint = "/mcp"
token = "my-shared-secret"
auth_timeout_ms = 5000
```

- `auth_timeout_ms` applies to an embedded dynamic authorization handler; the
  CLI server does not install one.
- The embedded `oro:mcp` server takes its host, port, endpoint, limits, and
  OAuth settings from `mcp.startServer(options)`. It also honors the project
  token and authorization timeout when the call does not supply a token.
- Port `0`, in either CLI configuration or `mcp.startServer()`, selects an
  available port.
- When `token` is provided the HTTP server enforces bearer authentication unless
  a dynamic authorization handler is registered. A dynamic handler owns the
  authorization decision after the server's Origin check, so it must validate
  every credential it requires.

### Embedded OAuth 2.1 flow

The embedded server can host a small, pre-registered OAuth authorization
server for local tools and development deployments. It implements protected
resource metadata, authorization-server metadata, S256 PKCE, resource
indicators, issuer identification in authorization responses, exact redirect
URI registration, one-time consent requests, bounded token state, and
audience-bound access tokens.

```js
import mcp from 'oro:mcp'

const server = await mcp.startServer({
  port: 0,
  oauth: {
    defaultClientId: 'my-local-client',
    redirectUris: ['http://127.0.0.1:49152/callback'],
    defaultScope: 'mcp:read mcp:write'
  }
})

console.log(server.oauth.protectedResourceMetadataPath)
```

OAuth clients must include the exact MCP endpoint URI as `resource` in both
authorization and token requests and must use `code_challenge_method=S256`.
The authorization response includes `iss`; clients should validate it before
redeeming the code.

The built-in HTTP listener does not terminate TLS. Keep it on loopback for
local development. For a public deployment, place it behind a trusted HTTPS
reverse proxy and set both `oauth.issuer` and `oauth.resource` to their public
HTTPS values. Non-loopback binds require authentication. Custom authorization
screen forms must submit the opaque `authorization_request` value exposed as
`{{AUTHORIZATION_REQUEST}}` or
`window.__MCP_AUTHORIZATION_CONTEXT__.authorizationRequest`; clients must not
resubmit the raw authorization parameters to the approval POST.

The server defaults to a 16 MiB request limit, 1,024 concurrent request/session
contexts, 1,024 queued events, and 8 MiB of queued data per stream. Use
`maxRequestBytes`, `maxSessions`, `maxQueuedEvents`, `maxQueuedBytes`, and
`sessionTtlSeconds` in `mcp.startServer()` to lower or deliberately raise those
bounds.

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
the server and set the callback in a single call. The callback is evaluated
after mandatory Origin validation and replaces the built-in token decision for
that request.

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
