# Public Android historical migration qualification

Run from the runtime repository root with Node 24+, OpenSSL, a matching built
SDK/CLI, Android/JDK/Gradle tools and an explicitly selected phone. This fixture
uses only the package `oro.runtime.qualification.migration` and public values.
Do not point the harness at a product package. The main application contains
HTML/JSON/TOML only; historical seeding belongs in a separate signed test APK.

1. Run `node test/scripts/prepare-public-migration.js`. Keep its JSON output.
   It creates a new private scratch project, a two-day self-signed test
   certificate and an exact **leaf-certificate SHA-256** pin. The generated
   `oro.toml` is the project root. It does not install a system CA or change an
   existing project. Certificate/key files stay outside the application project.
   Never reuse this test certificate in a deployment.
2. Run the printed server command with its printed environment. It binds only
   `127.0.0.1:45132`; an occupied port fails instead of stopping another server.
3. Build the generated project with `oroc build --platform=android --debug`.
   For an in-place public upgrade, retain its debug signer and increase the
   public fixture version as appropriate. Never uninstall or clear product data.
4. Run `node test/scripts/prepare-public-migration.js --instrumentation <project>`
   after the build, then run its printed Gradle command in its printed `cwd`.
   Install the main APK and the separate `dev/debug` AndroidTest APK with
   `adb -s <serial> install -r <apk>`. Rebuilding through the CLI regenerates the
   Android tree; prepare the separate instrumentation again afterward.
5. Set `ORO_TEST_ANDROID_SERIAL` to that explicit device. Invoke
   `adb -s <serial> shell am instrument -w -e action seed
oro.runtime.qualification.migration.test/public.oro.migration.SeedInstrumentation`
   only for initial public history, and complete its system prompt. For later
   job checks use action `schedule`, which preserves existing ciphertext/keys.
6. Keep the public app visible. Set `ORO_TEST_MIGRATION_REPORT` to a new report
   file for every invocation. Run `node test/scripts/test-migration-android.js`
   and complete its old decrypt/new encrypt/fresh open prompts. Then schedule
   the old public job and run `node test/scripts/test-retained-components-android.js`;
   approve the two public handover dialogs. Channel defaults come from the same
   bundled definition as the read, preserving immutable historical channel IDs.
7. Run `node test/scripts/test-migration-boundaries-android.js` separately for
   each `ORO_TEST_MIGRATION_PHASE`, in this order:
   - `components`: native-v1 row preservation, checkpoint versus authorization
     generation, revoke predicate, wrapping-key epoch, aliases and exposed
     metadata. Set `ORO_TEST_MIGRATION_CA` to the printed certificate path and
     approve the public app-level consent dialogs.
   - `retained-interruption`: blocked HTTPS read, process loss, unknown-receipt
     replay fence, explicit public reconciliation and in-flight Stop/drain.
     Requires the same certificate environment and two app-level consent dialogs.
   - `interruption`: abort, rotation, background, reload and process-loss checks.
     Let these prompts cancel automatically. Rotation settings are restored.
   - `key-recovery`: target-key loss, explicit recovery nonce, corrupt GCM,
     historical-key loss and opening the already migrated copy. Complete its
     system prompts. **Run last:** it deliberately removes only the isolated
     public historical key; the original public ciphertext remains preserved.

The scripts add their own device reverse for port 45132 and remove their CDP
forwards. When done, remove only that owned reverse, stop only the public app
and shut down the fixture through `/control?mode=shutdown`. Retain all reports,
including incomplete attempts. Key-recovery is not repeatable against a missing
historical key without explicit reseeding of this public fixture. Reconcile
remaining public records before reusing its bounded stores.

Phone API/OS, signer, APK hash, native-bound asset manifest and selected SDK
receipt must accompany a passing report. These checks do not qualify private
historical releases, product account policy, endpoint ownership, production
signing, all OEM enrollment/invalidation behavior, natural Doze/reboot delivery
or non-Android backends.
