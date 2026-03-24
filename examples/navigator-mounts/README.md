# Navigator Mounts Example

Demonstrates how `[webview.navigator.mounts]` maps a host directory into the runtime's `oro://<bundle id>` origin.

The example seeds `$HOST_HOME/.oro/navigator-mounts` with a few files, exposes it at `/navigator-mounts`, and shows how to fetch assets through the mount. Edit the files with any editor to see changes reflected immediately when you refresh the listing.

## Running

```bash
npm run relink
oroc build examples
oroc run examples --entry navigator-mounts/index.html
```

Configure the mount in the example config (`examples/oro.toml`):

```toml
[webview.navigator.mounts]
"$HOST_HOME/.oro/navigator-mounts" = "/navigator-mounts"
```

To target specific platforms, prefix the key with `mac_`, `win_`, `linux_`, `ios_`, or `android_`.
