# synopsis

The official Node.js adapter for the Oro Runtime IPC protocol.

# description

[Oro Runtime][0] uses a simple URI-based protocol for brokering messages
between the renderer process and the native runtime. Messages stream over
`stdin`/`stdout`. This module is an optional, higher-level adapter around
that protocol so Node.js developers can interact with Oro IPC primitives
using familiar EventEmitter semantics.

# documentation

You can find the full documentation in the [API.md](./API.md) file.
