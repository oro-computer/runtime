# Secure Storage

Oro Runtime provides a cross-platform secure storage service that keeps key/value
pairs in the native credential stores. Data written through this API is backed
by the platform keystore (Keychain on Apple platforms, Credential Manager on
Windows, libsecret on Linux, and the Android Keystore).

```js
import { setItem, getItem, removeItem, clear, keys } from 'oro:secure-storage'

const scope = 'oro://com.example.my-app'

// Store sensitive data
await setItem('refresh-token', 'secret', { scope })

const token = await getItem('refresh-token', { scope })

// Enumerate known keys for the scope
const storedKeys = await keys({ scope })

// Remove data when it is no longer needed
await removeItem('refresh-token', { scope })

// Or clear the scope entirely
await clear({ scope })
```

### Scopes

Keys live inside a _scope_ which should be an origin-style string (for example
`oro://com.example.app`). When no scope is provided the runtime uses the
default origin of the current application window.

### Encodings

`setItem` accepts strings, `Uint8Array`, `ArrayBuffer`, or `Buffer` instances.
`getItem` returns strings by default and can return binary data when
`{ encoding: 'buffer' }` is supplied.

### Platform notes

- Linux backends depend on `libsecret-1`; if the library is unavailable the API
  will reject with an informative error.
- Android values are encrypted with an AES-GCM key stored in the platform
  keystore and then persisted using app-private shared preferences.
