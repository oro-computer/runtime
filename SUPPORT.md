# Support

Use [GitHub Issues](https://github.com/oro-computer/runtime/issues) for reproducible bugs and
feature requests. Search existing issues first and include the Oro Runtime version, operating
system and architecture, relevant configuration, minimal reproduction steps, expected behavior,
and actual behavior. Remove credentials and sensitive application data from logs.

Use GitHub Discussions for design questions and general usage help when available. The generated
API, CLI, configuration, and manpage references shipped with the runtime are the primary technical
documentation; start with `oroc help <query>` for task-oriented CLI discovery.

For source-build questions, include whether `NO_ANDROID` and `NO_IOS` were each set. They are
independent presence flags: non-empty `NO_ANDROID` disables only Android bootstrap/artifacts, while
non-empty `NO_IOS` disables only iOS/iOS Simulator work on macOS. `0` and `false` still count as
set. See [Source-build environment](docs/BUILD_ENVIRONMENT.md) for the exact contract.

Security vulnerabilities must not be posted publicly. Follow the private disclosure process in
[SECURITY.md](SECURITY.md).
