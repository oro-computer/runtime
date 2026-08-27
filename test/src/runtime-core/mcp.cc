#include "tests.hh"
#include "src/runtime/mcp/tool.hh"

namespace oro::Tests {
  void mcp (Harness& t) {
    namespace JSON = runtime::JSON;

    runtime::mcp::Tool tool;
    tool.outputSchema = JSON::parse(R"({
      "$schema":"https://json-schema.org/draft/2020-12/schema",
      "oneOf": [
        {
          "type":"array",
          "prefixItems":[{"const":"value"},{"type":"number"}],
          "items":false,
          "minItems":2,
          "maxItems":2
        },
        {"type":"null"}
      ]
    })");

    String error;
    t.assert(
      tool.prepareSchemas(error),
      "MCP tool schemas compile as JSON Schema 2020-12"
    );

    t.assert(
      tool.validateResult(
        R"({"content":[],"structuredContent":["value",1]})",
        error
      ),
      "MCP output schemas accept matching array structured content"
    );

    error.clear();
    t.assert(
      tool.validateResult(
        R"({"content":[],"structuredContent":null})",
        error
      ),
      "MCP output schemas accept matching null structured content"
    );

    error.clear();
    t.assert(
      !tool.validateResult(
        R"({"content":[],"structuredContent":["wrong",1]})",
        error
      ),
      "MCP output schemas reject non-matching structured content"
    );

    error.clear();
    t.assert(
      !tool.validateResult(R"({"content":[]})", error),
      "MCP output schemas require structured content on successful results"
    );

    t.assert(
      tool.toJSON().contains("outputSchema"),
      "modern MCP descriptors retain unrestricted output schemas"
    );
    t.assert(
      !tool.toJSON(false).contains("outputSchema"),
      "legacy MCP descriptors omit non-object output schemas"
    );

    runtime::mcp::Tool legacyObjectTool;
    legacyObjectTool.outputSchema = JSON::parse(R"({"type":"object"})");
    t.assert(
      legacyObjectTool.toJSON(false).contains("outputSchema"),
      "legacy MCP descriptors retain object output schemas"
    );

    runtime::mcp::Tool invalidSchemaTool;
    invalidSchemaTool.inputSchema = JSON::parse(R"({"type":"object"})").as<JSON::Object>();
    invalidSchemaTool.outputSchema = JSON::parse(R"({"type":7})");
    error.clear();
    t.assert(
      !invalidSchemaTool.prepareSchemas(error),
      "MCP registration rejects invalid JSON Schemas"
    );

    runtime::mcp::Tool externalReferenceTool;
    externalReferenceTool.inputSchema = JSON::parse(R"({"type":"object"})").as<JSON::Object>();
    externalReferenceTool.outputSchema = JSON::parse(
      R"({"$ref":"https://example.invalid/schema.json"})"
    );
    error.clear();
    t.assert(
      !externalReferenceTool.prepareSchemas(error),
      "MCP registration does not dereference external JSON Schemas"
    );

    const Vector<String> supportedDialects = {
      "http://json-schema.org/draft-04/schema#",
      "http://json-schema.org/draft-06/schema#",
      "http://json-schema.org/draft-07/schema#",
      "https://json-schema.org/draft/2019-09/schema",
      "https://json-schema.org/draft/2020-12/schema"
    };
    for (const auto& dialect : supportedDialects) {
      runtime::mcp::Tool dialectTool;
      dialectTool.outputSchema = JSON::parse(
        String("{\"$schema\":\"") + dialect + "\",\"type\":\"string\"}"
      );
      error.clear();
      t.assert(
        dialectTool.prepareSchemas(error),
        "MCP tool schemas support the advertised JSON Schema dialects"
      );
    }

    runtime::mcp::Tool unsupportedDialectTool;
    unsupportedDialectTool.outputSchema = JSON::parse(
      R"({"$schema":"https://example.invalid/unsupported","type":"string"})"
    );
    error.clear();
    t.assert(
      !unsupportedDialectTool.prepareSchemas(error),
      "MCP registration rejects unsupported JSON Schema dialects"
    );

    runtime::mcp::Tool nestedHeaderTool;
    nestedHeaderTool.inputSchema = JSON::parse(R"({
      "type":"object",
      "properties":{
        "context":{
          "type":"object",
          "properties":{
            "tenant":{"type":"integer","x-mcp-header":"Tenant"}
          }
        }
      }
    })").as<JSON::Object>();
    error.clear();
    t.assert(
      nestedHeaderTool.prepareSchemas(error),
      "MCP header annotations may be nested through properties"
    );

    Vector<runtime::mcp::ToolHeader> headers;
    t.assert(
      nestedHeaderTool.getExpectedHTTPHeaders(
        R"({"context":{"tenant":9007199254740991}})",
        headers,
        error
      ) &&
        headers.size() == 1 &&
        headers[0].name == "Mcp-Param-Tenant" &&
        headers[0].value == "9007199254740991",
      "MCP nested header annotations extract values at their exact path"
    );

    error.clear();
    t.assert(
      !nestedHeaderTool.getExpectedHTTPHeaders(
        R"({"context":{"tenant":9007199254740992}})",
        headers,
        error
      ),
      "MCP integer headers reject values outside the JavaScript safe range"
    );

    runtime::mcp::Tool unreachableHeaderTool;
    unreachableHeaderTool.inputSchema = JSON::parse(R"({
      "type":"object",
      "allOf":[{
        "properties":{
          "region":{"type":"string","x-mcp-header":"Region"}
        }
      }]
    })").as<JSON::Object>();
    error.clear();
    t.assert(
      !unreachableHeaderTool.prepareSchemas(error),
      "MCP header annotations reject paths through composition keywords"
    );

    runtime::mcp::Tool nonASCIIHeaderTool;
    nonASCIIHeaderTool.inputSchema = JSON::parse(R"({
      "type":"object",
      "properties":{
        "region":{"type":"string","x-mcp-header":"Région"}
      }
    })").as<JSON::Object>();
    error.clear();
    t.assert(
      !nonASCIIHeaderTool.prepareSchemas(error),
      "MCP header annotation names reject non-ASCII field-name characters"
    );
  }
}
