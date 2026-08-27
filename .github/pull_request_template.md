## Purpose

Describe the problem and the outcome of this change.

## Scope

- API/runtime/platform surfaces:
- Compatibility or migration impact:

## Validation

- Commands run:
- Platforms checked:
- Source bootstrap controls (`NO_ANDROID` and `NO_IOS`, reported separately):
- Checks not run and why:

## Checklist

- [ ] Public APIs include JSDoc and regenerated declarations.
- [ ] User-visible changes are documented in the changelog or relevant guide.
- [ ] No secrets, build outputs, or unrelated changes are included.
- [ ] Security and platform implications were reviewed.
- [ ] Source-build documentation and CI commands keep `NO_ANDROID` (Android only) distinct from
      `NO_IOS` (iOS/iOS Simulator only); desktop-only commands name both.
