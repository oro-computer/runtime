# Window Management

This guide covers common window operations available in Oro Runtime across desktop and mobile platforms (legacy Socket terminology is noted where users may still encounter it).

APIs live under `oro:window` and are typically used via an `ApplicationWindow` instance returned from `oro:application` helpers.

Quick Start

- Get current window: `const win = await application.getCurrentWindow()`
- Focus/Blur (desktop/mobile): `await win.focus()` / `await win.blur()`
- Always On Top (desktop):
  - `await win.setAlwaysOnTop(true)`
  - `const onTop = await win.isAlwaysOnTop()`
- Context Menu (native, desktop): `await win.setContextMenu({ value: 'Menu:\n  Foo: f;' })`

Examples

- Focus and blur current window

```
import application from 'oro:application'

const win = await application.getCurrentWindow()
await win.blur()
await win.focus()
```

- Always on top (desktop)

```
import application from 'oro:application'

const win = await application.getCurrentWindow()
await win.setAlwaysOnTop(true)
console.log(await win.isAlwaysOnTop()) // -> true
await win.setAlwaysOnTop(false)
```

- Targeted context menu for a specific window (desktop)

```
import application from 'oro:application'

const child = await application.createWindow({ index: 2, path: 'examples/window/secondary.html', title: 'Secondary' })
await child.setContextMenu({ value: 'Menu:\n  Foo: f;', targetWindowIndex: child.index })
```

Platform Notes

- Desktop: `focus`, `blur`, and `Always On Top` map to native window manager features.
- Mobile: `focus`/`blur` map to `show`/`hide`; `Always On Top` is not supported.

See also

- `api/window.js` (ApplicationWindow)
- `api/application.js` (createWindow, getWindow(s))
- `examples/kitchen-sink` (interactive demo)
