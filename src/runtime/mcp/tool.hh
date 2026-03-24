#ifndef ORO_RUNTIME_MCP_TOOL_H
#define ORO_RUNTIME_MCP_TOOL_H

#include "protocol.hh"

#include <vector>

namespace oro::runtime::mcp {
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

  struct Tool {
    String name;
    String title;
    String description;
    JSON::Object inputSchema = JSON::Object(JSON::Object::Entries{});
    JSON::Any annotations = JSON::Null();
    JSON::Any metadata = JSON::Null();

    JSON::Object toJSON() const;
  };

  class ToolBuilder {
    public:
      explicit ToolBuilder(const String& name);

      ToolBuilder& title(const String& value);
      ToolBuilder& description(const String& value);
      ToolBuilder& addString(const String& name, const String& description, bool required = true);
      ToolBuilder& addNumber(const String& name, const String& description, bool required = true);
      ToolBuilder& addInteger(const String& name, const String& description, bool required = true);
      ToolBuilder& addBoolean(const String& name, const String& description, bool required = true);
      ToolBuilder& addObject(const String& name, const String& description, const JSON::Any& schema = JSON::Null(), bool required = true);
      ToolBuilder& addArray(const String& name, const String& description, const JSON::Any& items = JSON::Null(), bool required = true);
      ToolBuilder& annotations(const JSON::Any& value);
      ToolBuilder& metadata(const JSON::Any& value);

      Tool build() const;

    private:
      String name;
      String titleValue;
      String descriptionValue;
      Vector<ToolParameter> parameters;
      JSON::Any annotationsValue = JSON::Null();
      JSON::Any metadataValue = JSON::Null();

      ToolBuilder& addParameter(ParameterKind kind, const String& name, const String& description, const JSON::Any& schema, bool required);
  };
}

#endif
