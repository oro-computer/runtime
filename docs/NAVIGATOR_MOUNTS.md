# Navigator Mounts

The navigator mounting system lets you expose host file system directories to the webview so that `fetch`, `<img>`, `<video>`, navigation, and other URL-based requests can read live files without bundling them into the application. Each mount maps a host directory to a virtual root under the app's `oro://<bundle id>` origin, so in-page navigation behaves like a regular static file server while still honouring the runtime's security policies.

## Configuration

Mounts are declared in `oro.toml` under `[webview.navigator.mounts]`. Each entry takes the form `<host path> = <navigator base path>`.

```ini
[webview.navigator.mounts]
$HOST_HOME/.oro/navigator/example = /navigator
linux_/srv/shared/assets = /shared
mac_$HOST_CONTAINER/Resources = /app-bundle
```

**Key conventions**

- Host aliases: `$HOST_HOME`, `$HOST_CONTAINER`, `$HOST_PROCESS_WORKING_DIRECTORY`, `~`, and `$HOME` expand to platform-specific directories.
- Platform targeting: Prefix a key with `mac_`, `win_`, `linux_`, `ios_`, or `android_` to enable the mount only on that platform. The prefix is removed after matching the current platform.
- Navigator paths should begin with `/` and may omit the trailing slash. They are compared against incoming request paths using a simple prefix test.
- Aliases are resolved before the path is canonicalised, so you can point at network shares or other absolute paths once expanded.

Changes to `src/cli/templates.hh` sync into `api/CONFIG.md` via `npm run gen`.

## URL Resolution Semantics

When a webview requests `oro://<bundle-id>/navigator/foo`, the runtime resolves the path using the following rules (mirroring how bundled resources behave):

1. Check for an explicit file (`foo`).
2. Check for a directory containing `index.html`; requesting `/navigator/foo` will redirect to `/navigator/foo/` for `GET` requests if that index exists.
3. As a convenience, resolve `foo.html` if present.

If the request matches a mount, the resolved host file path is served. Otherwise the runtime falls back to packaged resources (`/index.html`, `default_index`, or SPA fallback when `allow_any_route` is enabled) after giving registered service workers a chance to respond.

## Security Model

Navigator mounts participate in the runtime's sandbox and entitlements:

- `filesystem::Resource::isMountedPath` whitelists mounted directories so they can be opened without triggering macOS security-scoped bookmarks.
- When the macOS sandbox is enabled (`mac_sandbox != false`), any `$HOST_HOME`-based mounts are added to the generated entitlements automatically.
- On Linux, each mount is injected into the WebKit sandbox via `webkit_web_context_add_path_to_sandbox` so the renderer can read mounted files directly.
- The filesystem sandbox (`filesystem_sandbox_enabled`) still applies: requests are denied with `SecurityError` if they escape the declared mount roots or recognised well-known paths.

## Behavioural Notes

- Mount resolution is case-sensitive on Unix-like systems and case-preserving on Windows—follow the host file system's semantics when authoring URLs.
- Service workers see the mounted responses exactly as if they were bundled resources, so they can cache or intercept them as usual.
- Mounted directories are read-only from the webview's perspective; write operations must use the runtime `fs` APIs and target the host path directly.

## Example Workflow

1. Create a host directory and initial content:
   ```bash
   mkdir -p "$HOME/.oro/navigator/example"
   echo "Hello from host" > "$HOME/.oro/navigator/example/hello.txt"
   ```
2. Declare the mount in `oro.toml`:
   ```ini
   [webview.navigator.mounts]
   $HOST_HOME/.oro/navigator/example = /navigator
   ```
3. Reference it from the webview:
   ```html
   <img src="/navigator/hello.txt" alt="Mounted content" />
   ```

## Repository Example

The `examples/navigator-mounts` demo scaffolds a mount at `$HOST_HOME/.oro/navigator-mounts` and shows how to:

- Populate the host directory using the runtime's `fs` module when bootstrapping the app.
- List mounted files and stream their contents through `fetch`.
- Toggle between bundled assets and mounted assets to compare behaviours.

Build the examples bundle and run the `navigator-mounts` entry to try it out:

```bash
npm run relink
oroc build examples
oroc run examples --entry navigator-mounts/index.html
```

(Replace `oroc build`/`oroc run` with your usual workflow if you already have the examples app linked.)
