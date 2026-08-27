# DBus demo

This example shows how to drive the Oro Runtime DBus bridge directly from JavaScript. It connects to the session bus, requests a well-known name (`com.oro.examples.DBusDemo`), exports custom methods, installs signal match rules, and demonstrates calling both system and application services.

## Running the example

1. Build or relink the runtime so the examples bundle is available
   (`NO_ANDROID=1 NO_IOS=1 npm run relink` for a desktop-only relink, or
   `npm run gen && npm test` if you are refreshing everything). `NO_ANDROID`
   disables only Android bootstrap/artifacts and `NO_IOS` disables only
   iOS/iOS Simulator work on macOS; both are presence flags, so `0`/`false`
   still disable the named family. See
   [Source-build environment](../../docs/BUILD_ENVIRONMENT.md).
2. Launch the Oro app with the provided config (`examples/oro.toml`).
3. Open the `dbus` example from the index page.
4. Use the buttons in the UI to connect, claim the bus name, and try each action.

> **Note:** DBus support is only available on Linux builds that were compiled with `ORO_RUNTIME_HAVE_DBUS=1`. Other platforms will report that the service is unavailable.

## What it demonstrates

- Detecting DBus availability at runtime.
- Connecting to the session bus and handling `Connection` lifecycle.
- Requesting and releasing a well-known bus name.
- Exporting an object path so method calls are delivered to JavaScript.
- Responding to incoming calls (`Echo`, `Sum`) or returning errors for unknown methods.
- Installing and removing match rules, including the optional per-rule callback.
- Emitting custom signals and observing standard system signals (e.g. `NameOwnerChanged`).
- Calling into well-known services such as `org.freedesktop.DBus.ListNames`.
