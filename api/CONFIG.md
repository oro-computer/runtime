# Configuration
## Overview


The configuration file now lives in `oro.toml` (TOML) at the root of every Oro Runtime project.
The configuration file is read at compile time. Use `.ororc` in the project root for
developer-local overrides, simulator selections, and signing secrets that should not live in
version control.

Configuration precedence is:

1. `oro.toml`
2. `.ororc`
3. explicit CLI flags such as `oroc build --platform=ios`

The generated tables below document the scaffolded keys and starter values emitted by `oroc init`.
Values containing placeholders such as `{{project_name}}` are substituted when the project is created.
These are project-template values, not universal runtime defaults. For the live
runtime/CLI registry, use:

- `oroc config --list` to inspect all known keys and their effective values
- `oroc config --describe <key>` to inspect the description, default, and source for one key
- `oroc config --format json` to print the fully merged effective configuration

Example:

`oro.toml`:
```toml
# other settings

[build]

headless = false

# other settings
```

`.ororc`:
```ini
[build]

platform = ios ; override the `oroc build --platform` CLI option


[settings.ios] ; override the `[ios]` section in `oro.toml`

codesign_identity = "iPhone Developer: John Doe (XXXXXXXXXX)"
distribution_method = "release-testing"
provisioning_profile = "johndoe.mobileprovision"
simulator_device = "iPhone 15"
```

<tonic-toaster-inline
  title="Note"
  type="info">
    Note that "~" alias won't expand to the home directory in any of the configuration files.
    Use the full path instead.
</tonic-toaster-inline>

### `build`

Build-time bundling, output, and environment forwarding.

| Key | Scaffolded Value | Description |
| :--- | :--- | :--- |
| name | "{{project_name}}" | Required: CLI build name (also used as binary name) Must contain only letters, numbers, dashes, or underscores |
| copy | "src" | Resource directory copied into the app bundle and served by the `oro:` scheme. Most apps keep HTML, JS, CSS, icons, and service workers under `src`. |
| output | "build" | Build output directory (relative to the project root). Safe to delete and regenerate. |
| env | ["USER", "TMPDIR", "PWD"] | Environment variables forwarded from your shell into the runtime at launch. Use `.ororc` or `[env]`/`env_*` overrides when values differ per machine. |

### `meta`

Package metadata embedded in desktop/mobile artifacts and update manifests.

| Key | Scaffolded Value | Description |
| :--- | :--- | :--- |
| bundle_identifier | "com.{{project_name}}" | Required: reverse-DNS bundle identifier (used by app stores). |
| title | "{{project_name}}" | Human-readable app name for metadata. |
| version | "1.0.0" | Required: semantic version (major.minor.patch). |
| license | "custom" | Optional: SPDX or custom license identifier for packaging metadata. |
| url | "" | Optional: project homepage URL for packaging metadata. |

### `webview`

WebView bootstrap and resource-loading defaults.

| Key | Scaffolded Value | Description |
| :--- | :--- | :--- |
| root | "/" | URL path inside the packaged resources that becomes the initial app root. |
| watch | true | Automatically reload the webview when bundled resources change during local development. Packaged production builds ignore this. |

### `window`

Main application window defaults.

| Key | Scaffolded Value | Description |
| :--- | :--- | :--- |
| width | "50%" | Initial window width as a percentage of the available screen. |
| height | "50%" | Initial window height as a percentage of the available screen. |
