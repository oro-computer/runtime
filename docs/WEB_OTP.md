# Web OTP Support

## Overview

- The Oro Runtime implements the [WICG Web OTP](https://wicg.github.io/web-otp/) API via `navigator.credentials.get({ otp: { ... } })`.
- Android requests are brokered by a native OTP service backed by Google Play Services’ SMS Retriever API.
- iOS falls back to the system WebView implementation (WKWebView). Desktop platforms currently return `NotSupportedError`.

## SMS Format Requirements

- Incoming SMS messages must follow the Web OTP format:
  - Include the application origin, e.g. `@example.com` or `@example.com:443`.
  - Provide the OTP immediately after a `#` delimiter, e.g. `123456` in `Your code is: 123456 @example.com #123456`.
- The runtime validates the origin in the SMS before resolving the request.

## Configuration

- Toggle runtime access via `[permissions] allow_otp = true|false` in `oro.toml`.
  - When set to `false`, Android requests are rejected with `NotAllowedError` and the SMS receiver is never registered.

## Platform Notes

- **Android**
  - Requires Play Services on the device or emulator.
  - The runtime automatically registers and unregisters the SMS Retriever broadcast receiver per request.
  - No SMS permissions are requested; the Retriever API does not require `READ_SMS`.
- **iOS**
  - The runtime now presents an in-app one-time-code field (with `UITextContentTypeOneTimeCode`) so OTP suggestions surface inside the app.
  - No additional entitlements are required; the system QuickType bar provides the SMS code consent prompt.
- **Desktop**
  - Not supported today; requests are rejected with `NotSupportedError`.

## iOS Integration Roadmap

Even with the text-field-based implementation in place, we can explore deeper integration layers:

1. Evaluate `ASAuthorizationController` once Apple exposes an official Web OTP API to apps, allowing fully headless retrieval.
2. Consider an entitlements helper (`entitlements.ios.associated_domains`) if associated domains become a requirement for SMS code autofill.
3. Provide UI affordances for users to retry or dismiss OTP prompts when no SMS suggestion is offered.
4. Add XCTests that automate the QuickType OTP suggestion flow to guard against regressions across iOS releases.

## JavaScript Polyfill Behavior

- The runtime injects `navigator.credentials.get` support when the platform lacks a native implementation.
- The polyfill accepts `signal` and `timeout` options, propagating aborts to the native layer.
- Responses resolve with an `OTPCredential` object exposing `type`, `code`, `transports`, and `origin`.

### Quick start

```js
const controller = new AbortController()

try {
  const credential = await navigator.credentials.get({
    otp: { transport: ['sms'], hint: 'example-app' },
    signal: controller.signal,
    timeout: 60_000,
  })

  console.log('OTP code', credential.code)
} catch (err) {
  console.error('Unable to retrieve OTP', err)
}
```

## Error Handling

- Android rejects requests with DOMException names aligned to the Web OTP spec:
  - `AbortError` – configuration disabled, or request cancelled.
  - `InvalidStateError` – empty codes or malformed SMS payloads.
  - `NotAllowedError` – permission denied by config.
  - `NotSupportedError` – platform/features missing (e.g., Play Services absent).
  - `TimeoutError` – no matching SMS received before the request timeout.
