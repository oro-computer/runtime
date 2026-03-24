# Legacy Runtime Limitations

This repository is the final legacy snapshot of the runtime. Active feature
development continues in the new Oro Runtime repository with a fresh history.

Items listed here are frozen constraints of the legacy runtime. They are not an
active roadmap for this repository. Use this document to triage follow-up work
into project management for the new repository.

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
  legacy targets unless proven otherwise by a dedicated release run.

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
- [TLS Quickstart](/home/werle/repos/oro-computer/legacy-runtime/docs/TLS_QUICKSTART.md)
- [TLS Testing Guide](/home/werle/repos/oro-computer/legacy-runtime/docs/TLS_TESTING.md)

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
- [WebUSB in Oro Runtime](/home/werle/repos/oro-computer/legacy-runtime/docs/webusb.md)
- [WebHID Runtime Status](/home/werle/repos/oro-computer/legacy-runtime/docs/WEB_HID_STATUS.md)
- [Web Bluetooth Status](/home/werle/repos/oro-computer/legacy-runtime/docs/WEB_BLUETOOTH_STATUS.md)

## AI

- The embedded LLaMA server exposes OpenAI-compatible chat endpoints, but
  tool/function calling is intentionally stubbed. The runtime does not execute
  tool code or synthesize arbitrary arguments.
- Whisper support is present, but Windows/mobile validation coverage should be
  treated as incomplete legacy scope until verified in a dedicated release run.

See also:
- [Tool-Calling (Stub) Behavior](/home/werle/repos/oro-computer/legacy-runtime/docs/AI_TOOL_CALLING.md)
- [Whisper Speech Integration](/home/werle/repos/oro-computer/legacy-runtime/docs/AI_WHISPER.md)

## Archival Guidance

- Treat this file as the final hand-off list for follow-up work in the new Oro
  Runtime repository.
- Do not expand `PLAN.md` or `STATUS.md` with new roadmap detail in this legacy
  repository.
