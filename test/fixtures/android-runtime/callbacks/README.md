# Public compiled callback fixture

This isolated application uses only fixed public references. Its package is
`oro.runtime.qualification.callbacks`; it neither opens product accounts nor
enrolls with a server. The DUMP-protected receiver is qualification scaffolding,
not production glue required by the C++ facade.

Build from this directory with a matching packaged CLI and SDK:

```sh
oroc build --platform=android
```

From the runtime repository root, install and qualify that debug APK:

```sh
ANDROID_SERIAL=device-id AAPT=/path/to/android/build-tools/36.0.0/aapt \
  node test/scripts/test-native-callbacks-android.js /path/to/app-dev-debug.apk
```

Unlock the selected device for Activity, focus, foreground-service and deferred
checks. The driver selects exactly one device, validates the APK's fixed public
package name before installing, and accesses only this application's receipts.
It enables Android's stay-awake-while-powered setting, presses Home during the
pause check, and force-stops only this public package. It does not uninstall the
fixture or erase its storage. Use a fresh installation for a full run: retained
terminal execution IDs intentionally prevent silently rerunning work.

To repeat only the cold receiver/native checks on an installed fixture:

```sh
ANDROID_SERIAL=device-id ORO_ANDROID_NATIVE_COLD_ONLY=1 \
  node test/scripts/test-native-callbacks-android.js
```

Cold checks cover compiled Application initialization, encrypted C++ records,
JNI callback dispatch, scoped/redacted push discovery, revision-fenced cleanup,
renewal and ID reuse, cancellation during push authorization, scoped Application
interop rejection, and key-access refusal after shutdown. The full run adds
shared foreground/continuous/deferred dispatch, Activity pause, worker cleanup
and retained records across process loss. Receipts carry unique request IDs so
an earlier result cannot satisfy a new check.

For rotation, Activity recreation and process loss during active work, use
`ORO_ANDROID_NATIVE_ROTATION_LOSS=1` with the same driver. This mode verifies one
visible window in both orientations, continued encrypted writes, unchanged
execution generation, and an uncertain durable outcome after force-stop. It
restores the device's original rotation settings. Activity identity/window
counts are collected only from this fixture through its DUMP-protected receiver.

For a second full pass without erasing public test history, stage a fixture copy
and give the `public-finite`, `public-deferred` and `public-monitor` operation ID
literals a unique suffix in both `index.html` and `Receiver.kt`. Keep the handler
kinds unchanged, rebuild, and record the transformed source hashes and APK hash.
Do not reuse or release terminal IDs merely to make a repeated run succeed.

Push records and dispatch are seeded locally with public data. A successful run
does not verify distributor transport, authenticated server enrollment, account
authorization, legacy product migration, reboot/Doze delivery, or completed
remote operations. Debug signing and debuggable receipts are also separate from
production release signing and independent upgrade qualification.
