# Runtime Limitations

This file keeps the historical `LEGACY_LIMITATIONS.md` path so older links keep
working after the repository rename from `legacy-runtime` to `runtime`.

Items listed here are current platform and feature limitations for Oro Runtime.
Use this document to track compatibility gaps and follow-up work without
reintroducing stale repository links elsewhere in the docs.

## Release / Validation Scope

- Hosted CI continuously validates Linux desktop flows.
- The release workflow publishes downstream archives for:
  - Linux x64 desktop
  - Linux x64 Android SDK
  - macOS x64 desktop
  - macOS x64 iOS SDK
  - macOS arm64 desktop
  - macOS arm64 iOS SDK
  - Windows x64 desktop
- Hosted runtime validation is still narrower than the packaging matrix. Mobile,
  macOS, and Windows release legs should be treated as partially validated
  targets unless proven otherwise by a dedicated release run.

## TLS

- Linux desktop ships built-in TLS via mbedTLS or OpenSSL.
- Windows desktop ships built-in TLS via Schannel.
- macOS, iOS, and Android do not ship a built-in TLS provider in this legacy
  repository.
- Those targets should still build successfully, but `oro:tls` and related
  TLS-dependent entry points will return `NOT_IMPLEMENTED` when no provider is
  compiled in.
- SecureTransport, Android platform TLS, and GnuTLS are not implemented in this
  repository.

See also:
- [TLS Quickstart](./TLS_QUICKSTART.md)
- [TLS Testing Guide](./TLS_TESTING.md)

## USB / HID / Bluetooth

- WebUSB desktop support targets macOS, Windows, and Linux through the native
  libusb backend.
- Android WebUSB currently supports enumeration and permission flow through the
  foreground USB service, but transfer/configuration methods still return
  `NotSupportedError`.
- iOS WebUSB remains unavailable.
- WebHID is not complete on Android, and iOS/iPadOS remains unsupported because
  the platform does not expose a public generic HID API.
- Web Bluetooth still has parity gaps on Windows and Android, and platform
  device/integration coverage remains partial/manual.

See also:
- [WebUSB in Oro Runtime](./webusb.md)
- [WebHID Runtime Status](./WEB_HID_STATUS.md)
- [Web Bluetooth Status](./WEB_BLUETOOTH_STATUS.md)

## AI

- The embedded LLaMA server exposes OpenAI-compatible chat endpoints, but
  tool/function calling is intentionally stubbed. The runtime does not execute
  tool code or synthesize arbitrary arguments.
- Whisper support is present, but Windows/mobile validation coverage should be
  treated as incomplete validation scope until verified in a dedicated release
  run.

See also:
- [Tool-Calling (Stub) Behavior](./AI_TOOL_CALLING.md)
- [Whisper Speech Integration](./AI_WHISPER.md)

## Maintenance Guidance

- Treat this file as the current summary list for runtime limitations and
  follow-up work.
- Keep `PLAN.md` and `STATUS.md` lean instead of duplicating detailed limitation
  tracking there.
