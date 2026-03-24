# Android Storage Defaults

Oro Runtime scopes all Android file access to the app’s private storage (legacy Socket behavior is still honored during the transition) unless
you explicitly broker additional locations. The native bootstrap wires the
following directories when the activity starts:

- **Root directory** – `Context.getExternalFilesDir(null)`; falls back to
  `Context.getFilesDir()` when the scoped “external” directory is unavailable.
- **Cache directory** – `Context.getExternalCacheDir()` with an internal cache
  fallback.
- **Media directory** – `Context.getExternalMediaDirs().first()` when present,
  otherwise `externalFilesDir/media`.
- **Temporary directory** – subdirectory of the scoped cache:
  `externalCacheDir/__BUNDLE_IDENTIFIER__`.

All directories are created eagerly so IPC consumers can assume they exist.

### Implications

- Path lookups that historically resolved to `/sdcard/...` now land inside the
  app sandbox (`~/` maps to the scoped root above). Code that needs to expose
  files outside the sandbox must go through the Storage Access Framework (SAF)
  or an explicit document tree grant.
- Build scripts and tests should avoid hardcoding `/sdcard` paths; rely on
  `App.getRootDirectory()` or the well-known directories exposed by the runtime.
- No additional runtime permissions are required for these scoped locations on
  Android 10+; broader media access still depends on the config-driven
  `READ_MEDIA_*`/`READ_EXTERNAL_STORAGE` permissions.
