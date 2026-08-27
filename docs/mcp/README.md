# MCP specification references

Oro Runtime implements the final MCP `2026-07-28` protocol and keeps a separate compatibility
path for MCP `2025-06-18` clients. The runtime protocol constants are defined in
`src/runtime/mcp/protocol.hh`.

Authoritative references:

- [MCP 2026-07-28 specification](https://modelcontextprotocol.io/specification/2026-07-28)
- [MCP 2026-07-28 schema](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/schema/2026-07-28/schema.ts)
- [MCP 2026-07-28 changelog](https://modelcontextprotocol.io/specification/2026-07-28/changelog)
- [MCP 2025-06-18 compatibility specification](https://modelcontextprotocol.io/specification/2025-06-18)

Run `node docs/mcp/download-mcp-spec.js` to regenerate an index for the current protocol. Add
`--fetch` to cache the published HTML, or `--version=2025-06-18` to target the legacy protocol.
Downloaded pages and generated indexes are ignored local reference material; do not commit them.
The official schema remains the source of truth.
