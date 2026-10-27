# Public historical source selection

This fixture tests one generated receiver against both AES preferences and a
provenance-guarded NativeRecords scope. It uses only public test data, never a
production application or the older migration qualification package.

Prepare a project by copying `test/fixtures/migration/public/` to a writable
location outside `build/` and root `tmp/`. Change its bundle identifier to
`oro.runtime.qualification.sourcehistory`; replace `migration.json` and
`index.html` with the files in this directory. Build it through a matching
packaged SDK. The project itself has no custom native sources.

For the separate Android test APK, copy `SourceInstrumentation.kt` to the
project's generated Android `app/src/androidTest/java/oro/runtime/app/` directory
and select `oro.runtime.app.SourceInstrumentation` as its test runner. Build
`:app:assembleDevDebugAndroidTest`. This test code belongs only in the test APK;
it is not an application reader or a generated runtime adapter.

Install the public application and its test APK, then run against an explicitly
selected device:

```sh
adb -s "$ORO_TEST_ANDROID_SERIAL" shell am instrument -w -r \
  oro.runtime.qualification.sourcehistory.test/oro.runtime.app.SourceInstrumentation
ORO_TEST_SOURCE_SELECTION_REPORT=/tmp/source-history-ui.json \
  node test/scripts/test-migration-source-selection-android.js
```

Instrumentation requires an unused scope and original preference key, and
refuses to overwrite existing fixture history. It decrypts an actual Android
Keystore AES envelope, reads the native copy after promotion by the test writer,
checks cold exported receiver inputs before any approved handover, rejects an
old-writer digest change, preserves a native deletion, rejects a malformed
marker, removes its own public native wrapping key and verifies `EKEYREVOKED`.
The JavaScript driver then confirms the capability fields and key-loss rejection
through private-document IPC. It preserves that deliberate key-loss state and
closes only its own app and CDP forwarding connection.

For another complete run, use a fresh test environment. Do not clear the older
migration qualifier or a real application's histories to obtain a baseline.
Unknown input receipts remain unknown: no remote reconciliation or delivery is
claimed. This fixture does not qualify signed historical application upgrades,
a bridge release, activated jobs, authenticated servers or production alarms.

Host regression:
`node --test test/unit/migration-source-selection.test.js`. The reader group
requires `kotlinc` plus `ORO_KOTLIN_STDLIB`, `ORO_TEST_JSON_JAR`,
`ORO_TEST_SQLITE_JAR` and `ORO_TEST_SLF4J_JAR`; without them it explicitly skips.
It compiles actual runtime source and policy helpers against real SQLite with
host-only Android/Keystore facades. It does not substitute for device Keystore
qualification. The generator group also checks byte-identical policy in bridge
and generated modes, duplicate-class rejection and bounded selector validation.
