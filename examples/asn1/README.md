# ASN.1 playground

This example provides an interactive workspace for the `oro:asn1` runtime service. You can load one of the bundled schema samples or paste your own ASN.1 module, configure parser options (depth limits, lexer diagnostics, source echoing), and inspect the JSON document that the native parser emits.

## Running the example

1. Build or relink the runtime so the examples bundle is up to date (`npm run relink` or `npm run gen && npm test`).
2. Launch the Oro app with `examples/oro.toml`.
3. Open the `asn1` entry from the examples index.
4. Choose a built-in sample or paste ASN.1 text, tweak the options, and press **Parse ASN.1**.

## What it demonstrates

- Calling `parse()` from `oro:asn1` with custom options (`includeSourceText`, `lexerDebug`, `maxDepth`).
- Mapping the service response into rich summaries: module metadata, meta-type counts, constraint/value tallies, and per-module trees.
- Visualising the depth-limited traversal returned by the native parser, including defaulted values, tags, constraints, and markers.
- Importing `.asn1` files from disk and feeding them directly into the runtime parser.

## Included samples

The UI ships with three representative schema fragments:

- **Device profile schema** – nested sequences with defaults, optional `SET OF` collections, and enumerations.
- **Telemetry payloads** – `CHOICE` value unions, constrained strings, and repeated metric payloads.
- **Certificate template** – imports, object identifiers, nested choices, and extension handling reminiscent of X.509.

You can use these as a starting point or replace them entirely with your own modules. When you enable _Include source text in response_, the example surfaces the exact text the runtime processed alongside the parsed JSON tree.
