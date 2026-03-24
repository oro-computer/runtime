# MCP Control Center

This example demonstrates how to expose Oro Runtime features through the
Model Context Protocol (MCP). It registers several tools and resources that let
an external client inspect and steer the running application – for example,
changing the window title, resizing the window, moving it on screen, or
recording operator notes. The activity feed is also published as a MCP
resource, so clients can subscribe and react to changes in real time.

## Running the example

```bash
# From the repo root
oroc dev examples/oro.toml --index /mcp-control
```

Once the app loads you will see the control dashboard with the host, port, and
authorization token for the embedded MCP server. Every new launch generates a
fresh token unless you configure a static bearer in `oro.toml`.

### Customizing authentication

- Add a `[mcp]` section to your config (`oro.toml`) to provide a fixed bearer token or change the listen host/port (see [`docs/MCP.md`](../../docs/MCP.md)).
- Use `mcp.setAuthorizationHandler()` or the `authorize` option passed to
  `mcp.startServer()` to implement custom authorization logic (for example
  validating session cookies or per-tenant tokens).
- The control center UI always enables the bundled OAuth demo flow; use the
  preview button to open the consent screen with a sample PKCE challenge.

### Trying the OAuth consent flow

1. Start the embedded server (it launches automatically with the dashboard).
2. Click **Open OAuth consent screen** (or open the URL highlighted in the
   UI) and approve the request. The demo uses a plain PKCE challenge named
   `demo-challenge` with a redirect URI of
   `http://127.0.0.1:43110/callback` (you can copy the authorization code from
   the browser URL even if nothing is listening on that address).
3. Exchange the authorization code for a bearer token:

   ```bash
   CODE="<authorization_code_from_redirect>"
   curl -sS -X POST "http://<host>:<port>/mcp/oauth/token" \
     -H "Content-Type: application/x-www-form-urlencoded" \
     -d "grant_type=authorization_code" \
     -d "client_id=oro-mcp-example-client" \
     -d "redirect_uri=http://127.0.0.1:43110/callback" \
     -d "code_verifier=demo-challenge" \
     -d "code=$CODE"
   ```

4. Use the returned `access_token` as the `Authorization: Bearer` credential for
   SSE and JSON-RPC calls (it can be mixed with the static token if enabled).

## Connecting a MCP client

1. **Open a Server-Sent Events stream**

   ```bash
   HOST=127.0.0.1
   PORT=<value from UI>
   TOKEN=<value from UI>
   ENDPOINT=<value from UI, e.g. /mcp>

   curl -N -H "Authorization: Bearer $TOKEN" "http://$HOST:$PORT$ENDPOINT"
   ```

   Keep the connection running. The response headers include an
   `Mcp-Session-Id`, and the first `ready` event contains a JSON payload with
   the same session identifier. You will need it for subsequent POST requests.
   (Add `-i` to the command if you want to inspect the response headers.)

2. **Invoke tools**

   ```bash
   SESSION=<session id from the ready event or response header>

   curl -sS -X POST \
     -H "Authorization: Bearer $TOKEN" \
     -H "Mcp-Session-Id: $SESSION" \
     -H "Content-Type: application/json" \
     "http://$HOST:$PORT$ENDPOINT" \
     -d '{
       "jsonrpc": "2.0",
       "id": "set-title",
       "method": "tools/call",
       "params": {
         "name": "set_application_title",
         "arguments": { "title": "Remote controlled at " }
       }
     }'
   ```

   Available tools:

   | MCP name                            | Description                               |
   | ----------------------------------- | ----------------------------------------- |
   | `set_application_title`             | Update the window title.                  |
   | `resize_application_window`         | Resize the window (`width`/`height`).     |
   | `move_application_window`           | Move the window (`x`/`y`).                |
   | `set_application_window_visibility` | Show or hide the window (`visible`).      |
   | `log_application_note`              | Append a custom entry to the activity log |

3. **Read resources**

   ```bash
   curl -sS -X POST \
     -H "Authorization: Bearer $TOKEN" \
     -H "Mcp-Session-Id: $SESSION" \
     -H "Content-Type: application/json" \
     "http://$HOST:$PORT$ENDPOINT" \
     -d '{
       "jsonrpc": "2.0",
       "id": "status-read",
       "method": "resources/read",
       "params": { "uri": "oro.examples.mcp://status" }
     }' | jq -r '.result.contents[0].text'
   ```

   Resources exposed by the example:

   | URI                           | Description                                 |
   | ----------------------------- | ------------------------------------------- |
   | `oro.examples.mcp://status`   | JSON snapshot of the window/server state.   |
   | `oro.examples.mcp://activity` | Subscribable stream of activity JSON lines. |

4. **Subscribe to updates**

   ```bash
   curl -sS -X POST \
     -H "Authorization: Bearer $TOKEN" \
     -H "Mcp-Session-Id: $SESSION" \
     -H "Content-Type: application/json" \
     "http://$HOST:$PORT$ENDPOINT" \
     -d '{
       "jsonrpc": "2.0",
       "id": "activity-sub",
       "method": "resources/subscribe",
       "params": { "uri": "oro.examples.mcp://activity" }
     }'
   ```

   The SSE stream will now emit `resources/update` notifications whenever a tool
   runs, the app pauses/resumes, or a custom note is logged.

## Tips

- The dashboard highlights the message endpoint needed for JSON-RPC POST
  requests as soon as the server starts.
- Every tool invocation automatically refreshes and republishes the status
  resource, so clients that subscribe to `oro.examples.mcp://status` see the
  latest window metadata without polling.
- You can copy the generated token directly from the UI – it is required for
  both SSE and JSON-RPC requests via the `Authorization: Bearer` header.
