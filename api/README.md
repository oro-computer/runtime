# Buffer

Buffer module is a [third party](https://github.com/feross/buffer) vendor module provided by Feross Aboukhadijeh and other contributors (MIT License).

External docs: https://nodejs.org/api/buffer.html


# Events

Events module is a [third party](https://github.com/browserify/events/blob/main/events.js) module provided by Browserify and Node.js contributors (MIT License).

External docs: https://nodejs.org/api/events.html

# application

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fapplication

Provides Application level methods

Example usage:
```js
import { createWindow } from 'oro:application'
```
## MAX_WINDOWS


Maximum number of concurrently tracked application windows.

The runtime currently caps window indices at this value when enumerating
or creating windows through the high-level application APIs.
## ApplicationWindowList


Ordered collection of `ApplicationWindow` instances keyed by window index.

The list is iterable, preserves ascending window-index order, and also
exposes each window at `list[window.index]` for direct indexed lookup.
### `from(args)`


Creates a window list from either a single array or variadic window
arguments.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| args | ...ApplicationWindow \\| ApplicationWindow[] |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ApplicationWindowList |  |

### `constructor(items)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| items | ApplicationWindow[] |  | true |  |

### `length()`


Number of windows currently stored in the list.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number |  |

### `size()`


Alias for `length`.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number |  |

### `forEach(callback, thisArg)`


Invokes `callback` once for each window in the list.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| callback | (window: ApplicationWindow, index: number, list: ApplicationWindow[]) => void |  | false |  |
| thisArg | any |  | true |  |

### `item(index)`


Returns the window stored at `index`, if present.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| index | number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ApplicationWindow \\| undefined |  |

### `entries()`


Returns `[window.index, window]` pairs for the current list contents.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Array<[number, ApplicationWindow]> |  |

### `keys()`


Returns the ordered window indices contained in the list.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number[] |  |

### `values()`


Returns the ordered window instances contained in the list.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ApplicationWindow[] |  |

### `add(window)`


Inserts or replaces a window in the list using its `window.index`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| window | ApplicationWindow |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ApplicationWindowList |  |

### `remove(windowOrIndex)`


Removes a window from the list by instance or numeric index.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| windowOrIndex | ApplicationWindow \\| number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | boolean |  |

### `contains(windowOrIndex)`


Returns `true` when the list contains a window for the given instance or
numeric index.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| windowOrIndex | ApplicationWindow \\| number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | boolean |  |

### `clear()`


Removes all windows from the list.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ApplicationWindowList |  |

## `addEventListener(type, listener, options)`


Add an application event `type` callback `listener` with `options`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| type | string |  | false |  |
| listener | function(Event \\| MessageEvent \\| CustomEvent \\| ApplicationURLEvent): boolean |  | false |  |
| options | { once?: boolean } \\| boolean | null | true |  |

## `removeEventListener(type, listener)`


Remove an application event `type` callback `listener` with `options`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| type | string |  | false |  |
| listener | function(Event \\| MessageEvent \\| CustomEvent \\| ApplicationURLEvent): boolean |  | false |  |

## `getCurrentWindowIndex()`


Returns the current window index
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number |  |

## `createWindow(opts)`


Creates a new window and returns an instance of ApplicationWindow.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | false | an options object |
| opts.aspectRatio | string |  | true | a string (split on ':') provides two float values which set the window's aspect ratio. |
| opts.closable | boolean |  | true | deterime if the window can be closed. |
| opts.minimizable | boolean |  | true | deterime if the window can be minimized. |
| opts.maximizable | boolean |  | true | deterime if the window can be maximized. |
| opts.margin | number |  | true | a margin around the webview. (Private) |
| opts.radius | number |  | true | a radius on the webview. (Private) |
| opts.index | number | -1 | true | the index of the window, if not provided or the value is `-1`, then one will be assigned |
| opts.path | string |  | false | the path to the HTML file to load into the window. |
| opts.title | string |  | true | the title of the window. |
| opts.titlebarStyle | string |  | true | determines the style of the titlebar (MacOS only). |
| opts.windowControlOffsets | string |  | true | a string (split on 'x') provides the x and y position of the traffic lights (MacOS only). |
| opts.backgroundColorDark | string |  | true | determines the background color of the window in dark mode. |
| opts.backgroundColorLight | string |  | true | determines the background color of the window in light mode. |
| opts.followSystemTheme | boolean |  | true | whether the window should follow the desktop theme (default: true). |
| opts.preferDarkTheme | boolean |  | true | whether the window should prefer a dark theme when not following the system theme. |
| opts.width | number \\| string |  | true | the width of the window. If undefined, the window will have the main window width. |
| opts.height | number \\| string |  | true | the height of the window. If undefined, the window will have the main window height. |
| opts.minWidth | number \\| string | 0 | true | the minimum width of the window |
| opts.minHeight | number \\| string | 0 | true | the minimum height of the window |
| opts.maxWidth | number \\| string | '100%' | true | the maximum width of the window |
| opts.maxHeight | number \\| string | '100%' | true | the maximum height of the window |
| opts.resizable | boolean | true | true | whether the window is resizable |
| opts.frameless | boolean | false | true | whether the window is frameless |
| opts.utility | boolean | false | true | whether the window is utility (macOS only) |
| opts.shouldExitApplicationOnClose | boolean | false | true | whether the window can exit the app |
| opts.headless | boolean |  | true | overrides the project headless setting for this window |
| opts.userScript | string | null | true | A user script that will be injected into the window (desktop only) |
| opts.protocolHandlers | string[] |  | true | An array of protocol handler schemes to register with the new window (requires service worker) |
| opts.config | Record<string, string \\| number \\| boolean \\| (string \\| number \\| boolean)[]> |  | true | additional configuration key/value pairs |
| opts.resourcesDirectory | string |  | true |  |
| opts.shouldPreferServiceWorker | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ApplicationWindow> |  |

## `getScreenSize()`


Returns the current screen size.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<{ width: number, height: number }> |  |

## `getWindows(indices, options)`


Returns the ApplicationWindow instances for the given indices or all windows if no indices are provided.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| indices | number[] |  | true | the indices of the windows |
| options | ApplicationWindowQueryOptions | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ApplicationWindowList> |  |

## `getWindow(index, options)`


Returns the ApplicationWindow instance for the given index
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| index | number |  | false | the index of the window |
| options | ApplicationWindowQueryOptions |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ApplicationWindow \\| undefined> | the ApplicationWindow instance or `undefined` if the window does not exist |

## `getCurrentWindow()`


Returns the ApplicationWindow instance for the current window.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ApplicationWindow> |  |

## `exit(code)`


Quits the backend process and then quits the render process, the exit code used is the final exit code to the OS.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| code | number | 0 | true | an exit code |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result['data']> |  |

## `setSystemMenu(options)`


Set the native menu for the app.

Oro Runtime provides a minimalist DSL that makes it easy to create cross
platform native system and context menus.

Menus are created at run time. They can be created from either the Main or
Render process. The can be recreated instantly by calling the `setSystemMenu` method.

The method takes a string. Here's an example of a menu. The semi colon is
significant indicates the end of the menu. Use an underscore when there is no
accelerator key. Modifiers are optional. And well known OS menu options like
the edit menu will automatically get accelerators you dont need to specify them.

```js
oro.application.setSystemMenu({ index: 0, value: `
  App:
    Foo: f;

  Edit:
    Cut: x
    Copy: c
    Paste: v
    Delete: _
    Select All: a;

  Other:
    Apple: _
    Another Test: T
    !Im Disabled: I
    Some Thing: S + Meta
    ---
    Bazz: s + Meta, Control, Alt;
`)
```

Separators

To create a separator, use three dashes `---`.

Accelerator Modifiers

Accelerator modifiers are used as visual indicators but don't have a
material impact as the actual key binding is done in the event listener.

A capital letter implies that the accelerator is modified by the `Shift` key.

Additional accelerators are `Meta`, `Control`, `Option`, each separated
by commas. If one is not applicable for a platform, it will just be ignored.

On MacOS `Meta` is the same as `Command`.

Disabled Items

If you want to disable a menu item just prefix the item with the `!` character.
This will cause the item to appear disabled when the system menu renders.

Submenus

We feel like nested menus are an anti-pattern. We don't use them. If you have a
strong argument for them and a very simple pull request that makes them work we
may consider them.

Event Handling

When a menu item is activated, it raises the `menuItemSelected` event in
the front end code, you can then communicate with your backend code if you
want from there.

For example, if the `Apple` item is selected from the `Other` menu...

```js
window.addEventListener('menuItemSelected', event => {
  assert(event.detail.parent === 'Other')
  assert(event.detail.title === 'Apple')
})
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | ApplicationMenuOptions |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

## `setTrayMenu(options)`


An alias to `setSystemMenu()` for creating a tray menu.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | ApplicationMenuOptions |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

## `setSystemMenuItemEnabled(value)`


Set the enabled state of the system menu.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| value | ApplicationMenuItemEnabledOptions |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

## `isPaused()`


Predicate function to determine if application is in a "paused" state.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | boolean |  |

## runtimeVersion


Oro Runtime semantic version metadata mirrored from `process.versions.oro`.
The legacy `process.versions.socket` string remains frozen for compatibility.
## debug


Runtime debug flag.
## config


Application configuration.
## backend


The application's backend instance.
### `open(opts)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | false | an options object |
| opts.force | boolean | false | true | whether to force the existing process to close |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `close()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |


# crypto

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fcrypto

Some high-level methods around the `crypto.subtle` API for getting
random bytes and hashing.

Example usage:
```js
import { randomBytes } from 'oro:crypto'
```
## webcrypto


WebCrypto API
## ready


A promise that resolves when all internals to be loaded/ready.
## `getRandomValues(buffer, ...args)`


Generate cryptographically strong random values into the `buffer`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| buffer | TypedArray |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | TypedArray |  |

## `rand64()`


Generate a random 64-bit number.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | BigInt | A random 64-bit number. |

## RANDOM_BYTES_QUOTA


Maximum total size of random bytes per page
## MAX_RANDOM_BYTES


Maximum total size for random bytes.
## MAX_RANDOM_BYTES_PAGES


Maximum total amount of allocated per page of bytes (max/quota)
## `randomBytes(size)`


Generate `size` random bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| size | number |  | false | The number of bytes to generate. The size must not be larger than 2**31 - 1. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Buffer | A `Buffer` containing random bytes. |

## `createDigest(algorithm, buf)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| algorithm | string |  | false | `SHA-1` \| `SHA-256` \| `SHA-384` \| `SHA-512` |
| message | Buffer \\| TypedArray \\| DataView |  | false | A `Buffer`, TypedArray, or DataView. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Buffer> | A promise that resolves to a `Buffer` containing the digest. |

## `murmur3(value, seed)`


A murmur3 hash implementation based on https://github.com/jwerle/murmurhash.c
that works on strings and `ArrayBuffer` views (typed arrays)
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| value | string \\| Uint8Array \\| ArrayBuffer |  | false |  |
| seed | number | 0 | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number |  |


# dgram

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fdgram

This module provides an implementation of UDP datagram sockets. It does
not (yet) provide any of the multicast methods or properties.

Example usage:
```js
import { createSocket } from 'oro:dgram'
```
## `createSocket(options, callback)`


Creates a `Socket` instance.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | string \\| Object |  | false | either a string ('udp4' or 'udp6') or an options object |
| options.type | string |  | true | The family of socket. Must be either 'udp4' or 'udp6'. Required. |
| options.reuseAddr | boolean | false | true | When true socket.bind() will reuse the address, even if another process has already bound a socket on it. Default: false. |
| options.ipv6Only | boolean | false | true | Default: false. |
| options.recvBufferSize | number |  | true | Sets the SO_RCVBUF socket value. |
| options.sendBufferSize | number |  | true | Sets the SO_SNDBUF socket value. |
| options.signal | AbortSignal |  | true | An AbortSignal that may be used to close a socket. |
| callback | function(Buffer, RemoteInfo) |  | true | Attached as a listener for 'message' events. Optional. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Socket |  |

## `Socket` (extends `EventEmitter`)


New instances of dgram.Socket are created using dgram.createSocket().
The new keyword is not to be used to create dgram.Socket instances.
Emitted when a new datagram is available to read.
Emitted once the socket has been bound and is ready to receive messages.
Emitted when an error occurs on the socket.
Emitted when the socket has been closed.
### `bind(port, address, callback)`


Listen for datagram messages on a named port and optional address
If the address is not specified, the operating system will attempt to
listen on all addresses. Once the binding is complete, a 'listening'
event is emitted and the optional callback function is called.

If binding fails, an 'error' event is emitted.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| port | number |  | false | The port to listen for messages on |
| address | string |  | false | The address to bind to (0.0.0.0) |
| callback | function |  | false | With no parameters. Called when binding is complete. |

### `connect(port, host, connectListener)`


Associates the dgram.Socket to a remote address and port. Every message sent
by this handle is automatically sent to that destination. Also, the socket
will only receive messages from that remote peer. Trying to call connect()
on an already connected socket will result in an ERR_SOCKET_DGRAM_IS_CONNECTED
exception. If the address is not provided, '0.0.0.0' (for udp4 sockets) or '::1'
(for udp6 sockets) will be used by default. Once the connection is complete,
a 'connect' event is emitted and the optional callback function is called.
In case of failure, the callback is called or, failing this, an 'error' event
is emitted.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| port | number |  | false | Port the client should connect to. |
| host | string |  | true | Host the client should connect to. |
| connectListener | function |  | true | Common parameter of socket.connect() methods. Will be added as a listener for the 'connect' event once. |

### `disconnect()`


A synchronous function that disassociates a connected dgram.Socket from
its remote address. Trying to call disconnect() on an unbound or already
disconnected socket will result in an ERR_SOCKET_DGRAM_NOT_CONNECTED exception.
### `send(msg, offset, length, port, address, callback)`


Broadcasts a datagram on the socket. For connectionless sockets, the
destination port and address must be specified. Connected sockets, on the
other hand, will use their associated remote endpoint, so the port and
address arguments must not be set.

> The msg argument contains the message to be sent. Depending on its type,
different behavior can apply. If msg is a Buffer, any TypedArray, or a
DataView, the offset and length specify the offset within the Buffer where
the message begins and the number of bytes in the message, respectively.
If msg is a String, then it is automatically converted to a Buffer with
'utf8' encoding. With messages that contain multi-byte characters, offset,
and length will be calculated with respect to byte length and not the
character position. If msg is an array, offset and length must not be
specified.

> The address argument is a string. If the value of the address is a hostname,
DNS will be used to resolve the address of the host. If the address is not
provided or otherwise nullish, '0.0.0.0' (for udp4 sockets) or '::'
(for udp6 sockets) will be used by default.

> If the socket has not been previously bound with a call to bind, the socket
is assigned a random port number and is bound to the "all interfaces"
address ('0.0.0.0' for udp4 sockets, '::' for udp6 sockets.)

> An optional callback function may be specified as a way of reporting DNS
errors or for determining when it is safe to reuse the buf object. DNS
lookups delay the time to send for at least one tick of the Node.js event
loop.

> The only way to know for sure that the datagram has been sent is by using a
callback. If an error occurs and a callback is given, the error will be
passed as the first argument to the callback. If a callback is not given,
the error is emitted as an 'error' event on the socket object.

> Offset and length are optional but both must be set if either is used.
They are supported only when the first argument is a Buffer, a TypedArray,
or a DataView.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | Buffer \\| TypedArray \\| DataView \\| string \\| Array |  | false | Message to be sent. |
| offset | number |  | true | Offset in the buffer where the message starts. |
| length | number |  | true | Number of bytes in the message. |
| port | number |  | true | Destination port. |
| address | string |  | true | Destination host name or IP address. |
| callback | Function |  | true | Called when the message has been sent. |

### `close(callback)`


Close the underlying socket and stop listening for data on it. If a
callback is provided, it is added as a listener for the 'close' event.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| callback | function(Error?) |  | false | Called when the connection is completed or on error. |

### `address()`


Returns an object containing the address information for a socket. For
UDP sockets, this object will contain address, family, and port properties.

This method throws EBADF if called on an unbound socket.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Object | socketInfo - Information about the local socket |
| Not specified | string | socketInfo.address - The IP address of the socket |
| Not specified | string | socketInfo.port - The port of the socket |
| Not specified | string | socketInfo.family - The IP family of the socket |

### `remoteAddress()`


Returns an object containing the address, family, and port of the remote
endpoint. This method throws an ERR_SOCKET_DGRAM_NOT_CONNECTED exception
if the socket is not connected.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Object | socketInfo - Information about the remote socket |
| Not specified | string | socketInfo.address - The IP address of the socket |
| Not specified | string | socketInfo.port - The port of the socket |
| Not specified | string | socketInfo.family - The IP family of the socket |

### `setRecvBufferSize(size)`


Sets the SO_RCVBUF socket option. Sets the maximum socket receive buffer in
bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| size | number |  | false | The size of the new receive buffer |

### `setSendBufferSize(size)`


Sets the SO_SNDBUF socket option. Sets the maximum socket send buffer in
bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| size | number |  | false | The size of the new send buffer |

### `getRecvBufferSize()`


### `getSendBufferSize()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number | the SO_SNDBUF socket send buffer size in bytes. |

### `setBroadcast(on)`


Enable or disable SO_BROADCAST on the socket.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| on | boolean | true | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `setTTL(ttl)`


Set unicast TTL for outgoing packets.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| ttl | number | 64 | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `setMulticastTTL(ttl)`


Set multicast TTL for outgoing multicast packets.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| ttl | number | 1 | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `setMulticastLoopback(on)`


Enable or disable multicast loopback.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| on | boolean | true | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `setMulticastInterface(iface)`


Set the default network interface for multicast.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| iface | string | '' | true | network interface name or address |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `addMembership(address, iface)`


Join a multicast group.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| address | string |  | false | multicast group address |
| iface | string | '' | true | optional interface name or address |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `dropMembership(address, iface)`


Leave a multicast group.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| address | string |  | false | multicast group address |
| iface | string | '' | true | optional interface name or address |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `addSourceSpecificMembership(address, source, iface)`


Add source-specific multicast membership (if supported).
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| address | string |  | false | multicast group address (SSM range) |
| source | string |  | false | source address |
| iface | string | '' | true | optional interface name or address |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `dropSourceSpecificMembership(address, source, iface)`


Drop source-specific multicast membership (if supported).
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| address | string |  | false | multicast group address (SSM range) |
| source | string |  | false | source address |
| iface | string | '' | true | optional interface name or address |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `setIPv6Only(on)`


Configure the socket as IPv6-only.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| on | boolean | true | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

## `getCapabilities()`


Query UDP capabilities from the runtime (multicast, broadcast, ipv6only, ssm).
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<{ multicast: boolean, broadcast: boolean, ipv6only: boolean, ssm: boolean }> |  |

## `isSSMSupported()`


Convenience helper for Source-Specific Multicast support.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<boolean> |  |

### `code()`


## `ERR_SOCKET_ALREADY_BOUND` (extends `SocketError`)


Thrown when a socket is already bound.
## `ERR_SOCKET_DGRAM_IS_CONNECTED` (extends `SocketError`)


Thrown when the socket is already connected.
## `ERR_SOCKET_DGRAM_NOT_CONNECTED` (extends `SocketError`)


Thrown when the socket is not connected.
## `ERR_SOCKET_DGRAM_NOT_RUNNING` (extends `SocketError`)


Thrown when the socket is not running (not bound or connected).
## `ERR_SOCKET_BAD_TYPE` (extends `TypeError`)


Thrown when a bad socket type is used in an argument.
## `ERR_SOCKET_BAD_PORT` (extends `RangeError`)


Thrown when a bad port is given.

# dns

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fdns

This module enables name resolution. For example, use it to look up IP
addresses of host names. Although named for the Domain Name System (DNS),
it does not always use the DNS protocol for lookups. dns.lookup() uses the
operating system facilities to perform name resolution. It may not need to
perform any network communication. To perform name resolution the way other
applications on the same system do, use dns.lookup().

Example usage:
```js
import { lookup } from 'oro:dns'
```
## `lookup(hostname, options, cb)`


Resolves a host name (e.g. `example.org`) into the first found A (IPv4) or
AAAA (IPv6) record. All option properties are optional. If options is an
integer, then it must be 4 or 6 – if options is 0 or not provided, then IPv4
and IPv6 addresses are both returned if found.

From the node.js website...

> With the all option set to true, the arguments for callback change to (err,
addresses), with addresses being an array of objects with the properties
address and family.

> On error, err is an Error object, where err.code is the error code. Keep in
mind that err.code will be set to 'ENOTFOUND' not only when the host name does
not exist but also when the lookup fails in other ways such as no available
file descriptors. dns.lookup() does not necessarily have anything to do with
the DNS protocol. The implementation uses an operating system facility that
can associate names with addresses and vice versa. This implementation can
have subtle but important consequences on the behavior of any Node.js program.
Please take some time to consult the Implementation considerations section
before using dns.lookup().
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| hostname | string |  | false | The host name to resolve. |
| options | LookupOptions \\| number \\| string |  | true | Lookup options or the record family. |
| cb | function(Error, string \\| LookupAddress[], 4 \\| 6=):void |  | true | Invoked when the lookup completes. |


# dns.promises


This module enables name resolution. For example, use it to look up IP
addresses of host names. Although named for the Domain Name System (DNS),
it does not always use the DNS protocol for lookups. dns.lookup() uses the
operating system facilities to perform name resolution. It may not need to
perform any network communication. To perform name resolution the way other
applications on the same system do, use dns.lookup().

Example usage:
```js
import { lookup } from 'oro:dns/promises'
```
## `lookup(hostname, opts)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| hostname | string |  | false | The host name to resolve. |
| opts | LookupOptions \\| number \\| string |  | true | Lookup options or family. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<LookupAddress \\| LookupAddress[]> |  |


# fs

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Ffs

This module enables interacting with the file system in a way modeled on
standard POSIX functions.

The Application Sandbox restricts access to the file system.

iOS Application Sandboxing has a set of rules that limits access to the file
system. Apps can only access files in their own sandboxed home directory.

| Directory | Description |
| --- | --- |
| `Documents` | The app’s sandboxed documents directory. The contents of this directory are backed up by iTunes and may be set as accessible to the user via iTunes when `UIFileSharingEnabled` is set to `true` in the application's `info.plist`. |
| `Library` | The app’s sandboxed library directory. The contents of this directory are synchronized via iTunes (except the `Library/Caches` subdirectory, see below), but never exposed to the user. |
| `Library/Caches` | The app’s sandboxed caches directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. It's a good place to store data which provides a good offline-first experience for the user. |
| `Library/Preferences` | The app’s sandboxed preferences directory. The contents of this directory are synchronized via iTunes. Its purpose is to be used by the Settings app. Avoid creating your own files in this directory. |
| `tmp` | The app’s sandboxed temporary directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. Although, it's recommended that you delete data that is not necessary anymore manually to minimize the space your app takes up on the file system. Use this directory to store data that is only useful during the app runtime. |

Example usage:
```js
import * as fs from 'oro:fs';
```
## `watchFile(path, options, listener)`


Polls for file changes and invokes listener with (curr, prev) Stats.
This is a compatibility helper; prefer fs.watch for evented changes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| options | object \\| function |  | true |  |
| options.interval | number | 5007 | true |  |
| options.bigint | boolean | false | true |  |
| listener | function(Stats, Stats) |  | true |  |

## `unwatchFile(path, listener)`


Removes a watchFile listener or stops watching entirely for a path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| listener | function | null | true |  |

## `access(path, mode, callback)`


Asynchronously check access to a file for a given mode calling `callback`
upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number \\| function(Error \\| null):any | F_OK(0) | true |  |
| callback | function(Error \\| null):any |  | true |  |

## `accessSync(path, mode)`


Synchronously check access to a file for a given mode calling `callback`
upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | true |  |

## `exists(path, callback)`


Checks if a path exists
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| callback | function(Boolean)? |  | true |  |

## `existsSync(path)`


Checks if a path exists
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| callback | function(Boolean)? |  | true |  |

## `chmod(path, mode, callback)`


Asynchronously changes the permissions of a file.
No arguments other than a possible exception are given to the completion callback
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |
| callback | function(Error?) |  | false |  |

## `chmodSync(path, mode)`


Synchronously changes the permissions of a file.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |

## `chown(path, uid, gid, callback)`


Changes ownership of file or directory at `path` with `uid` and `gid`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |
| callback | function |  | false |  |

## `chownSync(path, uid, gid)`


Changes ownership of file or directory at `path` with `uid` and `gid`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |

## `close(fd, callback)`


Asynchronously close a file descriptor calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false |  |
| callback | function(Error?)? |  | true |  |

## `closeSync(fd)`


Synchronously close a file descriptor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false | fd |

## `copyFile(src, dest, flags, callback)`


Asynchronously copies `src` to `dest` calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false | The source file path. |
| dest | string |  | false | The destination file path. |
| flags | number | 0 | false | Modifiers for copy operation. |
| callback | function(Error=) |  | true | The function to call after completion. |

## `copyFileSync(src, dest, flags)`


Synchronously copies `src` to `dest` calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false | The source file path. |
| dest | string |  | false | The destination file path. |
| flags | number | 0 | false | Modifiers for copy operation. |

## `createReadStream(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object? |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ReadStream |  |

## `createWriteStream(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object? |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | WriteStream |  |

## `fstat(fd, options, callback)`


Invokes the callback with the <fs.Stats> for the file descriptor. See
the POSIX fstat(2) documentation for more detail.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false | A file descriptor. |
| options | object? \\| function? |  | true | An options object. |
| callback | function? |  | false | The function to call after completion. |

## `fsync(fd, callback)`


Request that all data for the open file descriptor is flushed
to the storage device.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false | A file descriptor. |
| callback | function |  | false | The function to call after completion. |

## `ftruncate(fd, offset, callback)`


Truncates the file up to `offset` bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false | A file descriptor. |
| offset | number= \\| function | 0 | true |  |
| callback | function? |  | false | The function to call after completion. |

## `lchown(path, uid, gid, callback)`


Changes ownership of a symbolic link at `path` with `uid` and `gid`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |
| callback | function |  | false |  |

## `lchmod(path, mode, callback)`


Changes permissions of link at `path` with `mode` (POSIX). No-op where unsupported.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |
| callback | function(Error \\| null):any |  | false |  |

## `lchmodSync(path, mode)`


Synchronously changes permissions of a symbolic link at `path`.

On platforms that do not implement `lchmod`, the native backend may treat
this as a no-op or return a platform-specific error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |

## `link(src, dest, callback)`


Creates a link to `dest` from `src`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |
| (Position 2) | function |  | false |  |

## `linkSync(src, dest)`


Creates a hard link synchronously
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |

## `mkdtemp(prefix, options, callback)`


Create a unique temporary directory. The `prefix` is appended with a
platform-specific unique suffix.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| prefix | string |  | false |  |
| options | object \\| string \\| function |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| callback | function(Error \\| null, string \\| Buffer):any |  | true |  |

## `mkdtempSync(prefix, options)`


Create a unique temporary directory synchronously
## `open(path, flags, mode, options, callback)`


Asynchronously open a file calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| flags | string | 'r' | true |  |
| mode | number | 0o666 | true |  |
| options | object \\| function(Error \\| null, number \\| undefined):any | null | true |  |
| callback | (function(Error \\| null, number \\| undefined):any) \\| null | null | true |  |

## `openSync(path, flags, mode, options)`


Synchronously open a file.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| flags | string | 'r' | true |  |
| mode | string | 0o666 | true |  |
| options | object | null | true |  |

## `opendir(path, options, callback)`


Asynchronously open a directory calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object \\| function(Error \\| null, Dir \\| undefined):any |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.withFileTypes | boolean | false | true |  |
| callback | function(Error \\| null, Dir \\| undefined):any |  | true |  |

## `opendirSync(path, options)`


Synchronously open a directory.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.withFileTypes | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Dir |  |

## `read(fd, buffer, offset, length, position, options, callback)`


Asynchronously read from an open file descriptor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false |  |
| buffer | object \\| Buffer \\| Uint8Array |  | false | The buffer that the data will be written to. |
| offset | number |  | false | The position in buffer to write the data to. |
| length | number |  | false | The number of bytes to read. |
| position | number \\| BigInt \\| null |  | false | Specifies where to begin reading from in the file. If position is null or -1 , data will be read from the current file position, and the file position will be updated. If position is an integer, the file position will be unchanged. |
| callback | function(Error \\| null, number \\| undefined, Buffer \\| undefined):any |  | false |  |

## `write(fd, buffer, offset, length, position, options, callback)`


Asynchronously write to an open file descriptor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false |  |
| buffer | object \\| Buffer \\| Uint8Array |  | false | The buffer that the data will be written to. |
| offset | number |  | false | The position in buffer to write the data to. |
| length | number |  | false | The number of bytes to read. |
| position | number \\| BigInt \\| null |  | false | Specifies where to begin reading from in the file. If position is null or -1 , data will be read from the current file position, and the file position will be updated. If position is an integer, the file position will be unchanged. |
| callback | function(Error \\| null, number \\| undefined, Buffer \\| undefined):any |  | false |  |

## `writev(fd, buffers, position, callback)`


Vector write convenience: writes multiple buffers sequentially to fd
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false |  |
| buffers | Array<Buffer \\| TypedArray> |  | false |  |
| position | number \\| null \\| function |  | true |  |
| callback | function(Error \\| null, number=):any |  | true |  |

## `readv(fd, buffers, position, callback)`


Vector read convenience: reads into multiple buffers sequentially from fd
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number |  | false |  |
| buffers | Array<Buffer \\| TypedArray> |  | false |  |
| position | number \\| null \\| function |  | true |  |
| callback | function(Error \\| null, number, any[]):any |  | true |  |

## `readdir(path, options, callback)`


Asynchronously read all entries in a directory.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object \\| function(Error \\| null, (Dirent \\| string)[] \\| undefined):any |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.withFileTypes | boolean | false | true |  |
| callback | function(Error \\| null, (Dirent \\| string)[]):any |  | true |  |

## `readdirSync(path, options)`


Synchronously read all entries in a directory.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.withFileTypes | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | (Dirent \\| string)[] |  |

## `readFile(path, options, callback)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false |  |
| options | object \\| function(Error \\| null, Buffer \\| string \\| undefined):any |  | false |  |
| options.encoding | string | 'utf8' | true |  |
| options.flag | string | 'r' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |
| callback | function(Error \\| null, Buffer \\| string \\| undefined):any |  | false |  |

## `readFileSync(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false |  |
| options | { encoding?: string, flags?: string } | null | true |  |
| options | object \\| function(Error \\| null, Buffer \\| undefined):any | null | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string \\| Buffer |  |

## `readlink(path, options, callback)`


Reads link at `path`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| callback | function(Error \\| null, string \\| undefined):any |  | false |  |

## `readlinkSync(path, options)`


Reads link target at `path` synchronously
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `realpath(path, callback)`


Computes real path for `path`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| callback | function(Error \\| null, string \\| undefined):any |  | false |  |

## `realpathSync(path)`


Computes real path for `path`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `rename(src, dest, callback)`


Renames file or directory at `src` to `dest`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |
| callback | function(Error \\| null):any |  | false |  |

## `renameSync(src, dest)`


Renames file or directory at `src` to `dest`, synchronously.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |

## `rmdir(path, callback)`


Removes directory at `path`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| callback | function(Error \\| null):any |  | false |  |

## `rmdirSync(path)`


Removes directory at `path`, synchronously.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

## `statSync(path, options)`


Synchronously get the stats of a file
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false | filename or file descriptor |
| options | object | null | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.flag | string | 'r' | true |  |

## `fstatSync(fd, options)`


Synchronously get the stats of an open file descriptor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number \\| FileHandle |  | false |  |
| options | object | null | true |  |

## `stat(path, options, callback)`


Get the stats of a file
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false | filename or file descriptor |
| options | object \\| function(Error \\| null, Stats \\| undefined):any |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.flag | string | 'r' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |
| callback | function(Error \\| null, Stats \\| undefined):any |  | true |  |

## `lstat(path, options, callback)`


Get the stats of a symbolic link
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false | filename or file descriptor |
| options | object \\| function(Error \\| null, Stats \\| undefined):any |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.flag | string | 'r' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |
| callback | function(Error \\| null, Stats \\| undefined):any |  | true |  |

## `lstatSync(path, options)`


Synchronously get stats of a symbolic link
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object | null | true |  |

## `symlink(src, dest, type, callback)`


Creates a symlink of `src` at `dest`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |
| callback | function(Error \\| null):any |  | true |  |

## `symlinkSync(src, dest, type)`


Synchronously create a symlink
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |
| type | string | null | true |  |

## `unlink(path, callback)`


Unlinks (removes) file at `path`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| callback | function(Error \\| null):any |  | false |  |

## `unlinkSync(path)`


Unlinks (removes) file at `path`, synchronously.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

## `lchownSync(path, uid, gid)`


Changes ownership of link at `path` synchronously
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |

## `writeFile(path, data, options, callback)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false | filename or file descriptor |
| data | string \\| Buffer \\| TypedArray \\| DataView \\| object |  | false |  |
| options | object \\| function(Error \\| null):any |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.mode | string | 0o666 | true |  |
| options.flag | string | 'w' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |
| callback | function(Error \\| null):any |  | true |  |

## `writeFileSync(path, data, options)`


Writes data to a file synchronously.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false | filename or file descriptor |
| data | string \\| Buffer \\| TypedArray \\| DataView \\| object |  | false |  |
| options | object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.mode | string | 0o666 | true |  |
| options.flag | string | 'w' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |

## `truncate(path, len, callback)`


Truncate file at `path` to `len` bytes (default 0)
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| len | number \\| function | 0 | true |  |
| callback | function(Error \\| null):any |  | true |  |

## `truncateSync(path, len)`


Truncate file synchronously
## `appendFile(path, data, options, callback)`


Append data to a file
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| number |  | false |  |
| data | string \\| Buffer \\| TypedArray \\| DataView \\| object |  | false |  |
| options | object \\| function(Error \\| null):any |  | true |  |
| options.encoding | string |  | true |  |
| options.mode | number |  | true |  |
| options.flag | string |  | true |  |
| callback | function(Error \\| null):any |  | false |  |

## `appendFileSync(path, data, options)`


Append data synchronously
## `rm(path, options, callback)`


Remove a file or directory
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| options | { recursive?: boolean, force?: boolean } |  | true |  |
| callback | function(Error \\| null):any |  | false |  |

## `rmSync(path, options)`


Remove synchronously
## `cp(src, dest, options, callback)`


Copy file or directory.
Options:
- recursive: copy directories recursively
- dereference: follow symlinks (default true). When false, copies symlinks as symlinks
- force: overwrite if destination exists; when false and errorOnExist is false, leaves dest untouched
- errorOnExist: if true and destination exists, error (when force is false)
- preserveTimestamps: set atime/mtime on dest to match src (files)
- preserveMode: apply src mode (chmod) to dest
- preserveOwner: attempt to apply src uid/gid (chown) to dest (may be ignored by platform; may require privileges)
- filter: function (src, dest) => boolean|Promise<boolean> to include/exclude entries
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |
| options | { recursive?: boolean, dereference?: boolean, force?: boolean, errorOnExist?: boolean, preserveTimestamps?: boolean, preserveMode?: boolean, preserveOwner?: boolean, filter?: function(string, string): (boolean \\| Promise<boolean>) } |  | true |  |
| callback | function(Error \\| null):any |  | false |  |

## `cpSync(src, dest, options)`


Copy synchronously
## `utimes(path, atime, mtime, callback)`


Update atime/mtime for a path
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| atime | number \\| Date \\| string |  | false |  |
| mtime | number \\| Date \\| string |  | false |  |
| callback | function(Error=) |  | true |  |

## `lutimes(path, atime, mtime, callback)`


Update atime/mtime for a symlink without following it
## `lutimesSync(path, atime, mtime)`


Update atime/mtime for a symlink without following it (sync)
## `utimesSync(path, atime, mtime)`


Update atime/mtime for a path (sync)
## `futimes(fd, atime, mtime, callback)`


Update atime/mtime for an fd
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number \\| FileHandle |  | false |  |

## `futimesSync(fd, atime, mtime)`


Update atime/mtime for an fd (sync)
## `watch(path, options, callback)`


Watch for changes at `path` calling `callback`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| (Position 0) | string |  | false |  |
| options | function \\| object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| callback | function | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Watcher |  |


# fs.promises


* This module enables interacting with the file system in a way modeled on
standard POSIX functions.

The Application Sandbox restricts access to the file system.

iOS Application Sandboxing has a set of rules that limits access to the file
system. Apps can only access files in their own sandboxed home directory.

| Directory | Description |
| --- | --- |
| `Documents` | The app’s sandboxed documents directory. The contents of this directory are backed up by iTunes and may be set as accessible to the user via iTunes when `UIFileSharingEnabled` is set to `true` in the application's `info.plist`. |
| `Library` | The app’s sandboxed library directory. The contents of this directory are synchronized via iTunes (except the `Library/Caches` subdirectory, see below), but never exposed to the user. |
| `Library/Caches` | The app’s sandboxed caches directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. It's a good place to store data which provides a good offline-first experience for the user. |
| `Library/Preferences` | The app’s sandboxed preferences directory. The contents of this directory are synchronized via iTunes. Its purpose is to be used by the Settings app. Avoid creating your own files in this directory. |
| `tmp` | The app’s sandboxed temporary directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. Although, it's recommended that you delete data that is not necessary anymore manually to minimize the space your app takes up on the file system. Use this directory to store data that is only useful during the app runtime. |

Example usage:
```js
import fs from 'oro:fs/promises'
```
## `access(path, mode, options)`


Asynchronously check access a file.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | true |  |
| options | object |  | true |  |

## `chmod(path, mode)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

## `chown(path, uid, gid)`


Changes ownership of file or directory at `path` with `uid` and `gid`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `copyFile(src, dest, flags)`


Asynchronously copies `src` to `dest` calling `callback` upon success or error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false | The source file path. |
| dest | string |  | false | The destination file path. |
| flags | number | 0 | false | Modifiers for copy operation. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `lchown(path, uid, gid)`


Changes ownership of a symbolic link at `path` with `uid` and `gid`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| uid | number |  | false |  |
| gid | number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `lchmod(path, mode)`


Changes permissions of a symbolic link at `path`.

On platforms that do not implement `lchmod`, the native backend may treat
this as a no-op or reject the request with a platform-specific error.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| mode | number |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

## `link(src, dest)`


Creates a hard link to `dest` from `src`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `mkdir(path, options)`


Asynchronously creates a directory.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false | The path to create |
| options | object |  | true | The optional options argument can be an integer specifying mode (permission and sticky bits), or an object with a mode property and a recursive property indicating whether parent directories should be created. Calling fs.mkdir() when path is a directory that exists results in an error only when recursive is false. |
| options.recursive | boolean | false | true | Recursively create missing path segments. |
| options.mode | number | 0o777 | true | Set the mode of directory, or missing path segments when recursive is true. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise | Upon success, fulfills with undefined if recursive is false, or the first directory path created if recursive is true. |

## `mkdtemp(prefix, options)`


Create a unique temporary directory
## `open(path, flags, mode)`


Asynchronously open a file.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| flags | string | 'r' | true | default: 'r' |
| mode | number | 0o666 | true | default: 0o666 |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<FileHandle> |  |

## `opendir(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.bufferSize | number | 32 | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Dir> |  |

## `readdir(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.withFileTypes | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<(string \\| Dirent)[]> |  |

## `readFile(path, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |
| options | object |  | true |  |
| options.encoding | string \\| null | null | true |  |
| options.flag | string | 'r' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Buffer \\| string> |  |

## `readlink(path, options)`


Reads link at `path`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string> |  |

## `realpath(path)`


Computes real path for `path`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string> |  |

## `rename(src, dest)`


Renames file or directory at `src` to `dest`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `rmdir(path)`


Removes directory at `path`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `stat(path, options)`


Get the stats of a file
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.bigint | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Stats> |  |

## `fstat(fd, options)`


Get the stats of an open file descriptor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fd | number \\| FileHandle |  | false |  |
| options | object |  | true |  |
| options.bigint | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Stats> |  |

## `lstat(path, options)`


Get the stats of a symbolic link.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL |  | false |  |
| options | object |  | true |  |
| options.bigint | boolean | false | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Stats> |  |

## `symlink(src, dest, type)`


Creates a symlink of `src` at `dest`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| src | string |  | false |  |
| dest | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `utimes(path, atime, mtime)`


Update atime/mtime for a path (promises)
## `futimes(fd, atime, mtime)`


Update atime/mtime for an fd (promises)
## `lutimes(path, atime, mtime)`


Update atime/mtime for a symlink without following it (promises)
## `unlink(path)`


Unlinks (removes) file at `path`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

## `writeFile(path, data, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | string \\| Buffer \\| URL \\| FileHandle |  | false | filename or FileHandle |
| data | string \\| Buffer \\| Array \\| DataView \\| TypedArray |  | false |  |
| options | object |  | true |  |
| options.encoding | string \\| null | 'utf8' | true |  |
| options.mode | number | 0o666 | true |  |
| options.flag | string | 'w' | true |  |
| options.signal | AbortSignal \\| undefined |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

## `writev(fdOrHandle, buffers, position)`


Vector write: write multiple buffers to a FileHandle or fd
## `readv(fdOrHandle, buffers, position)`


Vector read: read into multiple buffers from a FileHandle or fd
## `truncate(path, len)`


Truncate file
## `appendFile(path, data, options)`


Append data
## `rm(path, options)`


Remove file or directory
## `cp(src, dest, options)`


Copy file or directory.
Options:
- recursive: copy directories recursively
- dereference: follow symlinks (default true). When false, copies symlinks as symlinks
- force: overwrite if destination exists; when false and errorOnExist is false, leaves dest untouched
- errorOnExist: if true and destination exists, error (when force is false)
- preserveTimestamps: set atime/mtime on dest to match src (files)
- preserveMode: apply src mode (chmod) to dest
- preserveOwner: attempt to apply src uid/gid (chown) to dest (may be ignored by platform; may require privileges)
- filter: function (src, dest) => boolean|Promise<boolean> to include/exclude entries
## `watch(path, options)`


Watch for changes at `path` calling `callback`
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| (Position 0) | string |  | false |  |
| options | function \\| object |  | true |  |
| options.encoding | string | 'utf8' | true |  |
| options.signal | AbortSignal |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Watcher |  |


# ipc

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fipc

This is a low-level API that you don't need unless you are implementing
a library on top of Oro Runtime. An Oro app has one or more processes.

When you need to send a message to another window or to the backend, you
should use the `application` module to get a reference to the window and
use the `send` method to send a message.

- The `Render` process, is the UI where the HTML, CSS, and JS are run.
- The `Bridge` process, is the thin layer of code that manages everything.
- The `Main` process, is for apps that need to run heavier compute jobs. And
unlike electron it's optional.

The Bridge process manages the Render and Main process, it may also broker
data between them.

The Binding process uses standard input and output as a way to communicate.
Data written to the write-end of the pipe is buffered by the OS until it is
read from the read-end of the pipe.

The IPC protocol uses a simple URI-like scheme. Data is passed as
ArrayBuffers.

```
ipc://command?key1=value1&key2=value2...
```

Example usage:
```js
import { send } from 'oro:ipc'
```
## `maybeMakeError(error, caller)`


Converts a structured IPC error payload into an `Error` instance.

Native responses sometimes serialize errors as plain objects. This helper
rehydrates them into the closest matching runtime error type while
preserving extra fields like `code`, `url`, and backend metadata.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| error | Error \\| { type?: string, code?: string \\| number, message?: string, [key: string]: any } \\| null \\| undefined |  | false |  |
| caller | Function |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Error \\| null |  |

### `constructor(params, nonce)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| params | object \\| any |  | false | Either a params object or a bare value which becomes `value`. |
| nonce | string \\| number \\| null | null | true | Optional nonce to include. |

## `emit(name, value, target, options)`


Emit event to be dispatched on `window` object.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| value | any |  | false |  |
| target | EventTarget | window | true |  |
| options | Object |  | true |  |

## `send(command, value, options)`


Sends an async IPC command request with parameters.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| command | string |  | false |  |
| value | any |  | true |  |
| options | { cache?: boolean, bytes?: (Buffer \\| Uint8Array \\| ArrayBuffer \\| string \\| Array), useExtensionIPCIfAvailable?: boolean } | null | true |  |
| options.cache | boolean | false | true |  |
| options.bytes | Buffer \\| Uint8Array \\| ArrayBuffer \\| string \\| Array |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Result> |  |

## `write(command, value, buffer, options)`


Sends an async IPC command request with parameters and buffered bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| command | string |  | false |  |
| value | any |  | true |  |
| buffer | Buffer \\| Uint8Array \\| ArrayBuffer \\| string \\| Array |  | true |  |
| options | { timeout?: number, responseType?: string, signal?: AbortSignal, useExtensionIPCIfAvailable?: boolean } |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Result> |  |

## `request(command, value, options)`


Sends an async IPC command request with parameters requesting a response
with buffered bytes.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| command | string |  | false |  |
| value | any |  | true |  |
| options | { timeout?: number, responseType?: string, signal?: AbortSignal, cache?: boolean, useExtensionIPCIfAvailable?: boolean } | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<Result> |  |

## `inflateIPCMessageTransfers(object, types)`


Rehydrates structured-clone friendly IPC payloads back into richer runtime
objects.

This helper recursively reconstructs encoded `Buffer` instances,
`IPCMessagePort` handles, and custom tagged values supplied in `types`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| object | any |  | false |  |
| types | Map<string, Function> |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | any |  |

## `findIPCMessageTransfers(transfers, object)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| transfers | Set<any> |  | false |  |
| object | any |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | any |  |

## `IPCMessagePort` (extends `MessagePort`)


A message port abstraction implemented using BroadcastChannel under the hood.
This mirrors the MessagePort surface where practical and enables structured
clone + transfer of ArrayBuffers and nested IPCMessagePorts.
Emitted when a message is received by this port.
Emitted when an error occurs while processing a message.
### `from(options)`


Create or retrieve an IPCMessagePort from options.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | { id?: string, rx?: string, tx?: string, transferred?: boolean } | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | IPCMessagePort |  |

### `transfer(port)`


Mark a port as transferred (used when passing through postMessage).
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| port | IPCMessagePort |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | IPCMessagePort |  |

### `create(options)`


Create a new IPCMessagePort instance from options.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | { id?: string, rx?: string, tx?: string, transferred?: boolean } | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | IPCMessagePort |  |

### `start()`


Start the port; subsequent messages are delivered to listeners.
### `close()`


Close the port and release references.
### `postMessage(message, optionsOrTransferList)`


Post a message to the paired port.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| message | any |  | false |  |
| optionsOrTransferList | { transfer?: any[] } \\| any[] |  | true |  |

## `IPCMessageChannel` (extends `MessageChannel`)


A message channel abstraction that pairs two IPCMessagePorts together.
### `constructor(options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | { id?: string, port1?: object, port2?: object } | null | true |  |

## `IPCBroadcastChannel` (extends `EventTarget`)


Emitted when a broadcast message is received.
Emitted when an error occurs while posting or receiving a message.
### `constructor(name, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| options | { origin?: string } | null | true |  |

### `onmessage()`


### `onmessageerror()`


### `onerror()`


### `addEventListener(type, callback, options, type, callback, options)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| type | 'message' |  | false |  |
| callback | function(MessageEvent):any |  | false |  |
| options | { once?: boolean } |  | true |  |
| type | 'messageerror' |  | false |  |
| callback | function(ErrorEvent):any |  | false |  |
| options | { once?: boolean } |  | true |  |

### `postMessage(message, optionsOrTransferList)`


Post a message to subscribers.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| message | any |  | false |  |
| optionsOrTransferList | { origin?: string, transfer?: any[] } \\| any[] |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<any> |  |


# network

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fnetwork

Provides a higher level API over the latica protocol.

Options:
- `allowUnsigned?: boolean` Deliver unverified payloads to handlers. Default: false
- `controlPlaneAuth?: 'sig'|'off'` Require signatures on control-plane frames (Ping/Pong/Join/Intro/Query). Default: 'off'

# os

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fos

This module provides normalized system information from all the major
operating systems.

Example usage:
```js
import { arch, platform } from 'oro:os'
```
## `arch()`


Returns the operating system CPU architecture for which Socket was compiled.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | 'arm64', 'ia32', 'x64', or 'unknown' |

## `cpus()`


Returns an array of objects containing information about each CPU/core.
The properties of the objects are:
- model `<string>` - CPU model name.
- speed `<number>` - CPU clock speed (in MHz).
- times `<object>` - An object containing the fields user, nice, sys, idle, irq representing the number of milliseconds the CPU has spent in each mode.
- user `<number>` - Time spent by this CPU or core in user mode.
- nice `<number>` - Time spent by this CPU or core in user mode with low priority (nice).
- sys `<number>` - Time spent by this CPU or core in system mode.
- idle `<number>` - Time spent by this CPU or core in idle mode.
- irq `<number>` - Time spent by this CPU or core in IRQ mode.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Array<object> | cpus - An array of objects containing information about each CPU/core. |

## `networkInterfaces()`


Returns an object containing network interfaces that have been assigned a network address.
Each key on the returned object identifies a network interface. The associated value is an array of objects that each describe an assigned network address.
The properties available on the assigned network address object include:
- address `<string>` - The assigned IPv4 or IPv6 address.
- netmask `<string>` - The IPv4 or IPv6 network mask.
- family `<string>` - The address family ('IPv4' or 'IPv6').
- mac `<string>` - The MAC address of the network interface.
- internal `<boolean>` - Indicates whether the network interface is a loopback interface.
- scopeid `<number>` - The numeric scope ID (only specified when family is 'IPv6').
- cidr `<string>` - The CIDR notation of the interface.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | object | An object containing network interfaces that have been assigned a network address. |

## `platform()`


Returns the operating system platform.
The returned value is equivalent to `process.platform`.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | 'android', 'cygwin', 'freebsd', 'linux', 'darwin', 'ios', 'openbsd', 'win32', or 'unknown' |

## `type()`


Returns the operating system name.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | 'CYGWIN_NT', 'Mac', 'Darwin', 'FreeBSD', 'Linux', 'OpenBSD', 'Windows_NT', 'Win32', or 'Unknown' |

## `isWindows()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | boolean | `true` if the operating system is Windows. |

## `tmpdir()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | The operating system's default directory for temporary files. |

## EOL


The operating system's end-of-line marker. `'\r\n'` on Windows and `'\n'` on POSIX.
## `rusage()`


Get resource usage.
## `uptime()`


Returns the system uptime in seconds.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number | The system uptime in seconds. |

## `uname()`


Returns the operating system name.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | The operating system name. |

## `homedir()`


Returns the home directory of the current user.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |


# path


Example usage:
```js
import { Path } from 'oro:path'
```

Notes
- URL-aware joins: When the first component is a URL, subsequent components
are applied to its pathname. An absolute component (starting with `/` or `\\` on Windows)
resets the pathname to the origin root. Username/password/host are preserved.
- Windows specifics:
- Drive-relative paths like `C:foo` are not absolute.
- Cross-drive relative paths do not exist; `relative('C:\\a', 'D:\\b')` returns `D:\\b`.
- UNC roots (e.g., `\\\\server\\share`) are preserved by join/normalize/parse.
- Supported PathComponent inputs include strings, URL objects, and objects
with `{ url }` or `{ pathname }`.
## `resolve(options, ...components)`


The path.resolve() method resolves a sequence of paths or path segments into an absolute path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| components | ...PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `cwd(opts)`


Computes current working directory for a path
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | true |  |
| opts.posix | boolean |  | true | Set to `true` to force POSIX style path |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `origin()`


Computed location origin. Defaults to `oro:///` if not available.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `relative(options, from, to)`


Computes the relative path from `from` to `to`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| from | PathComponent |  | false |  |
| to | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `join(options, ...components)`


Joins path components. This function may not return an absolute path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| components | ...PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `dirname(options, path)`


Computes directory name of path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| path | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `basename(options, path)`


Computes base name of path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| path | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `extname(options, path)`


Computes extension name of path.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| path | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `normalize(options, path)`


Computes normalized path
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| path | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `format(options, path)`


Formats `Path` object into a string.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false |  |
| path | object \\| Path |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

## `parse(path)`


Parses input `path` into a `Path` instance.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | PathComponent |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | object |  |

## Path


A container for a parsed Path.
### `from(input, cwd)`


Creates a `Path` instance from `input` and optional `cwd`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| input | PathComponent |  | false |  |
| cwd | string |  | true |  |

### `constructor(pathname, cwd)`


`Path` class constructor.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| pathname | string |  | false |  |
| cwd | string | Path.cwd() | true |  |

### `isRelative()`


`true` if the path is relative, otherwise `false.
### `value()`


The working value of this path.
### `source()`


The original source, unresolved.
### `parent()`


Computed parent path.
### `root()`


Computed root in path.
### `dir()`


Computed directory name in path.
### `base()`


Computed base name in path.
### `name()`


Computed base name in path without path extension.
### `ext()`


Computed extension name in path.
### `drive()`


The computed drive, if given in the path.
### `toURL()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | URL |  |

### `toString()`


Converts this `Path` instance to a string.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |


# process

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fprocess

Example usage:
```js
import process from 'oro:process'
```
## `ProcessEnvironmentEvent` (extends `Event`)


Event emitted by `env` when an environment variable is set, deleted, or
otherwise changed.
### `constructor(type, key, value)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| type | string |  | false |  |
| key | string \\| symbol |  | false |  |
| value | ProcessEnvironmentValue |  | true |  |

## `ProcessEnvironment` (extends `EventTarget`)


Observable environment store backing `process.env`.

Listen on this object to receive `set`, `delete`, and `change` events while
reading environment values through `process.env` or `env.proxy`.
## env


Emitted when an environment variable is set.
Emitted when an environment variable is deleted.
Emitted when an environment variable is changed (set or delete).
Observable process-environment state.

`process.env` returns `env.proxy`, which behaves like a mutable object of
string keys to string values. The exported `env` object itself is the
`EventTarget` you can subscribe to for environment change events.
### `versions()`


Reports the frozen legacy `process.versions.socket` identifier plus current Oro Runtime
and native library versions.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | ProcessVersionsMap |  |

## `nextTick(callback, ...args)`


Adds callback to the 'nextTick' queue.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| callback | Function |  | false |  |

## `hrtime(time)`


Computed high resolution time as a `BigInt`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| time | Array<number>? |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | bigint |  |

## `exit(code)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| code | number | 0 | true | The exit code. Default: 0. |

## `memoryUsage()`


Returns an object describing the memory usage of the Node.js process measured in bytes.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Object |  |


# test

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Ftest

Provides a test runner for Oro Runtime.

Example usage:
```js
import { test } from 'oro:test'

test('test name', async t => {
  t.equal(1, 1)
})
```
## `getDefaultTestRunnerTimeout()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number | The default timeout for tests in milliseconds. |

## Test


### `constructor(name, fn, runner)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| fn | TestFn |  | false |  |
| runner | TestRunner |  | false |  |

### `comment(msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | string |  | false |  |

### `plan(n)`


Plan the number of assertions.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| n | number |  | false |  |

### `deepEqual(actual, expected, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | T |  | false |  |
| expected | T |  | false |  |
| msg | string |  | true |  |

### `same(actual, expected, msg)`


Assert that two values are deeply equivalent.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | T |  | false |  |
| expected | T |  | false |  |
| msg | string |  | true |  |

### `notDeepEqual(actual, expected, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | T |  | false |  |
| expected | T |  | false |  |
| msg | string |  | true |  |

### `equal(actual, expected, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | T |  | false |  |
| expected | T |  | false |  |
| msg | string |  | true |  |

### `notEqual(actual, expected, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | unknown |  | false |  |
| expected | unknown |  | false |  |
| msg | string |  | true |  |

### `fail(msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | string |  | true |  |

### `ok(actual, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | unknown |  | false |  |
| msg | string |  | true |  |

### `notOk(actual, msg)`


Assert that a value is falsy.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | unknown |  | false |  |
| msg | string |  | true |  |

### `match(actual, expected, msg)`


Assert that a value matches a regular expression.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| actual | unknown |  | false |  |
| expected | RegExp |  | false |  |
| msg | string |  | true |  |

### `pass(msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | string |  | true |  |

### `skip(msg)`


Mark the current test as skipped.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | string |  | true |  |

### `ifError(err, msg)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| err | Error \\| null \\| undefined |  | false |  |
| msg | string |  | true |  |

### `throws(fn, expected, message)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| fn | Function |  | false |  |
| expected | RegExp \\| any |  | true |  |
| message | string |  | true |  |

### `rejects(input, expected, message)`


Assert that a promise or async function rejects.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| input | PromiseLike<any> \\| (() => any) |  | false |  |
| expected | RegExp \\| ((error: Error) => boolean) |  | true |  |
| message | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `sleep(ms, msg)`


Sleep for ms with an optional msg

```js
await t.sleep(100)
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| ms | number |  | false |  |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `requestAnimationFrame(msg)`


Request animation frame with an optional msg. Falls back to a 0ms setTimeout when
tests are run headlessly.

```js
await t.requestAnimationFrame()
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| msg | string | null | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `click(selector, msg)`


Dispatch the `click` method on an element specified by selector.

```js
await t.click('.class button', 'Click a button')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `eventClick(selector, msg)`


Dispatch the click window.MouseEvent on an element specified by selector.

```js
await t.eventClick('.class button', 'Click a button with an event')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `dispatchEvent(event, target, msg)`


Dispatch an event on the target.

```js
await t.dispatchEvent('my-event', '#my-div', 'Fire the my-event event')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string \\| Event |  | false | The event name or Event instance to dispatch. |
| target | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element to dispatch the event on. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `focus(selector, msg)`


Call the focus method on element specified by selector.

```js
await t.focus('#my-div')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `blur(selector, msg)`


Call the blur method on element specified by selector.

```js
await t.blur('#my-div')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `type(selector, str, msg)`


Consecutively set the str value of the element specified by selector to simulate typing.

```js
await t.typeValue('#my-div', 'Hello World', 'Type "Hello World" into #my-div')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| str | string |  | false | The string to type into the :focus element. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `appendChild(parentSelector, el, msg)`


appendChild an element el to a parent selector element.

```js
const myElement = createElement('div')
await t.appendChild('#parent-selector', myElement, 'Append myElement into #parent-selector')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| parentSelector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element to appendChild on. |
| el | HTMLElement \\| Element |  | false | A element to append to the parent element. |
| msg | string | 'Appended child element' | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `removeElement(selector, msg)`


Remove an element from the DOM.

```js
await t.removeElement('#dom-selector', 'Remove #dom-selector')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element to remove from the DOM. |
| msg | string | 'Removed element' | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `elementVisible(selector, msg)`


Test if an element is visible

```js
await t.elementVisible('#dom-selector','Element is visible')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element to test visibility on. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `elementInvisible(selector, msg)`


Test if an element is invisible

```js
await t.elementInvisible('#dom-selector','Element is invisible')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element to test visibility on. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `waitFor(querySelectorOrFn, opts, msg)`


Test if an element is invisible

```js
await t.waitFor('#dom-selector', { visible: true },'#dom-selector is on the page and visible')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| querySelectorOrFn | string \\| (() => HTMLElement \\| Element \\| null \\| undefined) |  | false | A query string or a function that returns an element. |
| opts | Object |  | true |  |
| opts.visible | boolean |  | true | The element needs to be visible. |
| opts.timeout | number |  | true | The maximum amount of time to wait. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<HTMLElement \\| Element \\| void> |  |

### `waitForText(selector, opts, msg)`


Test if an element is invisible

```js
await t.waitForText('#dom-selector', 'Text to wait for')
```

```js
await t.waitForText('#dom-selector', /hello/i)
```

```js
await t.waitForText('#dom-selector', {
  text: 'Text to wait for',
  multipleTags: true
})
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| HTMLElement \\| Element |  | false | A CSS selector string, or an instance of HTMLElement, or Element. |
| opts | WaitForTextOpts \\| string \\| RegExp |  | true |  |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<HTMLElement \\| Element \\| void> |  |

### `querySelector(selector, msg)`


Run a querySelector as an assert and also get the results

```js
const element = await t.querySelector('#dom-selector')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string |  | false | A CSS selector string, or an instance of HTMLElement, or Element to select. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | HTMLElement \\| Element |  |

### `querySelectorAll(selector, msg)`


Run a querySelectorAll as an assert and also get the results

```js
const elements = await t.querySelectorAll('#dom-selector', '')
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string |  | false | A CSS selector string, or an instance of HTMLElement, or Element to select. |
| msg | string |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Array<HTMLElement \\| Element> |  |

### `getComputedStyle(selector, msg)`


Retrieves the computed styles for a given element.

```js
// Using CSS selector
const style = getComputedStyle('.my-element', 'Custom success message');
```

```js
// Using Element object
const el = document.querySelector('.my-element');
const style = getComputedStyle(el);
```
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| selector | string \\| Element |  | false | The CSS selector or the Element object for which to get the computed styles. |
| msg | string |  | true | An optional message to display when the operation is successful. Default message will be generated based on the type of selector. |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | CSSStyleDeclaration | The computed styles of the element. |

### `run()`


pass: number,
fail: number
}>}
## TestRunner


### `constructor(report)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| report | (lines: string) => void | null | true |  |

### `nextId()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string |  |

### `length()`


### `add(name, fn, only)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| fn | TestFn |  | false |  |
| only | boolean |  | false |  |

### `run()`


| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `onFinish(callback)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| callback | (result: { total: number, success: number, fail: number }) => void |  | false |  |

## `only(name, fn)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| fn | TestFn |  | true |  |

## `skip(_name, _fn)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| _name | string |  | false |  |
| _fn | TestFn |  | true |  |

## `setStrict(strict)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| strict | boolean |  | false |  |

## `test(name, fn)`


| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| name | string |  | false |  |
| fn | TestFn |  | true |  |


# window

Web docs: https://oro.computer/runtime/docs/?p=javascript%2Fwindow

Provides ApplicationWindow class and methods

Don't use this module directly, get instances of ApplicationWindow with
`oro:application` methods like `getCurrentWindow`, `createWindow`,
`getWindow`, and `getWindows`.
## `ApplicationWindow` (extends `EventTarget`)


Represents a window in the application
### `id()`


The unique ID of this window.
### `index()`


Get the index of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number | the index of the window |

### `hotkey()`


### `channel()`


The broadcast channel for this window.
### `size()`


Get the size of the window
### `position()`


get  the position of the window
### `title()`


get  the title of the window
### `followSystemTheme()`


Indicates whether the window follows the host desktop theme.
### `preferDarkTheme()`


Indicates whether the window prefers a dark theme when not
following the system theme.
### `isDarkMode()`


Whether the window is currently in dark mode.
### `appearance()`


Current appearance metadata for the window.
### `token()`


### `status()`


get  the status of the window
### `getSize()`


Get the size of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | { width: number, height: number } | the size of the window |

### `getPosition()`


Get the position of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | { x: number, y: number } | the position of the window |

### `getTitle()`


Get the title of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | string | the title of the window |

### `getStatus()`


Get the status of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | number | the status of the window |

### `close()`


Close the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<object> | the options of the window |

### `show()`


Shows the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `hide()`


Hides the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `focus()`


Brings the window to the foreground and focuses it.
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `blur()`


Removes focus from the window (desktop: sends to back; mobile: hides).
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `maximize()`


Maximize the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `minimize()`


Minimize the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `restore()`


Restore the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `setTitle(title)`


Sets the title of the window
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| title | string |  | false | the title of the window |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `setSize(opts)`


Sets the size of the window
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | false | an options object |
| opts.width | number \\| string |  | true | the width of the window |
| opts.height | number \\| string |  | true | the height of the window |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `setPosition(opts)`


Sets the position of the window
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | false | an options object |
| opts.x | number \\| string |  | true | the x position of the window |
| opts.y | number \\| string |  | true | the y position of the window |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<object> |  |

### `navigate(path)`


Navigate the window to a given path
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| path | object |  | false | file path |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `showInspector()`


Opens the Web Inspector for the window (desktop only).
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<object> |  |

### `setBackgroundColor(opts)`


Sets the background color of the window
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| opts | object |  | false | an options object |
| opts.red | number |  | false | the red value |
| opts.green | number |  | false | the green value |
| opts.blue | number |  | false | the blue value |
| opts.alpha | number |  | false | the alpha value |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<object> |  |

### `getBackgroundColor()`


Gets the background color of the window
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string> |  |

### `setContextMenu(options)`


Opens a native context menu.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<object> |  |

### `setAlwaysOnTop(enabled)`


Sets whether the window should stay always on top (desktop only).
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| enabled | boolean |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `isAlwaysOnTop()`


Checks if the window is set to always be on top (desktop only).
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<boolean> |  |

### `showOpenFilePicker(options)`


Shows a native open file dialog.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string[]> | an array of file paths |

### `showSaveFilePicker(options)`


Shows a native save file dialog.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string \\| null> | the selected file path or null |

### `showDirectoryFilePicker(options)`


Shows a native directory dialog.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false | an options object |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<string[]> | an array of file paths |

### `share(options)`


Opens the platform share sheet for the current window.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | { title?: string, text?: string, url?: string } |  | true |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<void> |  |

### `send(options)`


This is a high-level API that you should use instead of `ipc.request` when
you want to send a message to another window or to the backend.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| options | object |  | false | an options object |
| options.window | number |  | true | the window to send the message to |
| options.backend | boolean | false | true | whether to send the message to the backend |
| options.event | string |  | false | the event to send |
| options.value | string \\| object |  | true | the value to send |

### `postMessage(data)`


Post a message to a window
TODO(@jwerle): research using `BroadcastChannel` instead
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| data | object |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

### `openExternal(value)`


Opens an URL in the default application associated with the URL protocol,
such as 'https:' for the default web browser.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| value | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<{ url: string }> |  |

### `revealFile(value)`


Opens a file in the default file explorer.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| value | string |  | false |  |

| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise |  |

### `update()`


Updates window state
| Return Value | Type | Description |
| :---         | :--- | :---        |
| Not specified | Promise<ipc.Result> |  |

### `addListener(event, cb)`


Adds a listener to the window.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to listen to |
| cb | function(*): void |  | false | the callback to call |

### `on(event, cb)`


Adds a listener to the window. An alias for `addListener`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to listen to |
| cb | function(*): void |  | false | the callback to call |

### `once(event, cb)`


Adds a listener to the window. The listener is removed after the first call.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to listen to |
| cb | function(*): void |  | false | the callback to call |

### `removeListener(event, cb)`


Removes a listener from the window.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to remove the listener from |
| cb | function(*): void |  | false | the callback to remove |

### `removeAllListeners(event)`


Removes all listeners from the window.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to remove the listeners from |

### `off(event, cb)`


Removes a listener from the window. An alias for `removeListener`.
| Argument | Type | Default | Optional | Description |
| :---     | :--- | :---:   | :---:    | :---        |
| event | string |  | false | the event to remove the listener from |
| cb | function(*): void |  | false | the callback to remove |
