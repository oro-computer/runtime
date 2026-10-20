# Android local network access

Apps targeting Android 17 (API 37) must request local network access before
connecting to LAN hosts. This covers WebView fetch/XHR, native TCP connections,
and UDP bootstrap or discovery. Internet access alone does not grant LAN access.

Oro includes `android.permission.ACCESS_LOCAL_NETWORK` in generated Android
manifests unless `[permissions] allow_local_network = false`. Declaring the
permission does not display a prompt or grant access. Request it when the user
chooses to connect to a local server:

```js
const status = await navigator.permissions.request({ name: 'local-network' })
if (status.state !== 'granted') {
  throw new Error('Allow local network access in Android app permissions to connect')
}
```

Use `navigator.permissions.query({ name: 'local-network' })` to check access
without prompting. Both calls return a `PermissionStatus` with `state` equal to
`granted`, `prompt`, or `denied`. Recheck when connecting instead of caching a
grant, because users can revoke access in Settings. Concurrent requests share a
single Android prompt. Denying or dismissing the prompt returns `denied`.

On devices below Android 17 or apps targeting below API 37, these calls report
`granted` without requesting the new permission. Setting
`[permissions] allow_local_network = false` returns `denied` on all Android
versions and omits the generated manifest permission. The configuration gate
controls the permission API and generated declaration, not individual socket
operations on older Android releases. The `local-network` permission API is
Android-only and throws on other platforms. Gate cross-platform app usage:

```js
import os from 'oro:os'

if (os.platform() === 'android') {
  const status = await navigator.permissions.request({ name: 'local-network' })
  if (status.state !== 'granted') {
    throw new Error('Local network access is required to connect to this server')
  }
}
```

Rebuild and reinstall apps built before this support was added. Updating
JavaScript alone cannot add the manifest permission or the native request flow.
Certificate validation, TLS pins, and CORS remain separate requirements.

See [Android's local network permission documentation](https://developer.android.com/privacy-and-security/local-network-permission).
