#ifndef ORO_RUNTIME_MCP_TOOL_H
#define ORO_RUNTIME_MCP_TOOL_H

#include "protocol.hh"

#include <memory>
#include <vector>

namespace oro::runtime::mcp {
  struct CompiledJSONSchema;

  enum class ParameterKind {
    String,
    Number,
    Integer,
    Boolean,
    Object,
    Array
  };

  struct ToolParameter {
    String name;
    ParameterKind kind = ParameterKind::String;
    String description;
    bool required = true;
    JSON::Any schema = JSON::Null();

    JSON::Object toJSON() const;
  };

  struct ToolHeader {
    String name;
    String value;
    bool present = false;
    bool integer = false;
  };

  struct Tool {
    String name;
    String title;
    String description;
    JSON::Object inputSchema = JSON::Object(JSON::Object::Entries {
      {"type", JSON::String("object")}
    });
    JSON::Any outputSchema = JSON::Null();
    JSON::Any icons = JSON::Null();
    JSON::Any annotations = JSON::Null();
    JSON::Any metadata = JSON::Null();
    std::shared_ptr<const CompiledJSONSchema> compiledInputSchema;
    std::shared_ptr<const CompiledJSONSchema> compiledOutputSchema;

    JSON::Object toJSON(bool currentProtocol = true) const;
    bool prepareSchemas(String& error);
    bool validateArguments(const String& value, String& error) const;
    bool validateResult(const String& value, String& error) const;
    bool getExpectedHTTPHeaders(
      const String& arguments,
      Vector<ToolHeader>& headers,
      String& error
    ) const;
  };

  class ToolBuilder {
    public:
      explicit ToolBuilder(const String& name);

      ToolBuilder& title(const String& value);
      ToolBuilder& description(const String& value);
      ToolBuilder& addString(const String& name, const String& description, bool required = true);
      ToolBuilder& addStringMaxLength(const String& name, const String& description, size_t maximum, bool required = true);
      ToolBuilder& addNumber(const String& name, const String& description, bool required = true);
      ToolBuilder& addInteger(const String& name, const String& description, bool required = true);
      ToolBuilder& addIntegerRange(const String& name, const String& description, int64_t minimum, int64_t maximum, bool required = true);
      ToolBuilder& addBoolean(const String& name, const String& description, bool required = true);
      ToolBuilder& addObject(const String& name, const String& description, const JSON::Any& schema = JSON::Null(), bool required = true);
      ToolBuilder& addArray(const String& name, const String& description, const JSON::Any& items = JSON::Null(), bool required = true);
      ToolBuilder& outputSchema(const JSON::Any& value);
      ToolBuilder& icons(const JSON::Any& value);
      ToolBuilder& annotations(const JSON::Any& value);
      ToolBuilder& metadata(const JSON::Any& value);

      Tool build() const;

    private:
      String name;
      String titleValue;
      String descriptionValue;
      Vector<ToolParameter> parameters;
      JSON::Any outputSchemaValue = JSON::Null();
      JSON::Any iconsValue = JSON::Null();
      JSON::Any annotationsValue = JSON::Null();
      JSON::Any metadataValue = JSON::Null();

      ToolBuilder& addParameter(ParameterKind kind, const String& name, const String& description, const JSON::Any& schema, bool required);
  };
}

#endif
