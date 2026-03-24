#include "tool.hh"

namespace oro::runtime::mcp {
  namespace {
    JSON::Object makeParameterSchema(ParameterKind kind, const String& description, const JSON::Any& schema) {
      const auto typeString = [&]() -> String {
        switch (kind) {
          case ParameterKind::String: return "string";
          case ParameterKind::Number: return "number";
          case ParameterKind::Integer: return "integer";
          case ParameterKind::Boolean: return "boolean";
          case ParameterKind::Object: return "object";
          case ParameterKind::Array: return "array";
        }
        return "string";
      }();

      JSON::Object::Entries entries {
        {"type", JSON::String(typeString)},
        {"description", JSON::String(description)}
      };

      if (!schema.isNull()) {
        if (kind == ParameterKind::Array) {
          entries.insert({"items", schema});
        } else if (schema.isObject()) {
          for (const auto& keyValue : schema.as<JSON::Object>().value()) {
            entries.insert({keyValue.first, keyValue.second});
          }
        }
      }

      return JSON::Object(entries);
    }
  }

  JSON::Object ToolParameter::toJSON() const {
    JSON::Object::Entries entries {
      {"name", JSON::String(this->name)},
      {"schema", makeParameterSchema(this->kind, this->description, this->schema)}
    };

    entries.insert({"required", JSON::Boolean(this->required)});
    return JSON::Object(entries);
  }

  JSON::Object Tool::toJSON() const {
    JSON::Object::Entries entries {
      {"name", JSON::String(this->name)},
      {"description", JSON::String(this->description)},
      {"inputSchema", this->inputSchema}
    };

    if (this->title.size() > 0) {
      entries.insert({"title", JSON::String(this->title)});
    }

    if (!this->annotations.isNull()) {
      entries.insert({"annotations", this->annotations});
    }

    if (!this->metadata.isNull()) {
      entries.insert({"metadata", this->metadata});
    }

    return JSON::Object(entries);
  }

  ToolBuilder::ToolBuilder(const String& toolName)
    : name(toolName)
  {}

  ToolBuilder& ToolBuilder::title(const String& value) {
    this->titleValue = value;
    return *this;
  }

  ToolBuilder& ToolBuilder::description(const String& value) {
    this->descriptionValue = value;
    return *this;
  }

  ToolBuilder& ToolBuilder::addString(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::String, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addNumber(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::Number, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addInteger(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::Integer, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addBoolean(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::Boolean, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addObject(const String& parameterName, const String& parameterDescription, const JSON::Any& schema, bool required) {
    return this->addParameter(ParameterKind::Object, parameterName, parameterDescription, schema, required);
  }

  ToolBuilder& ToolBuilder::addArray(const String& parameterName, const String& parameterDescription, const JSON::Any& items, bool required) {
    return this->addParameter(ParameterKind::Array, parameterName, parameterDescription, items, required);
  }

  ToolBuilder& ToolBuilder::annotations(const JSON::Any& value) {
    this->annotationsValue = value;
    return *this;
  }

  ToolBuilder& ToolBuilder::metadata(const JSON::Any& value) {
    this->metadataValue = value;
    return *this;
  }

  Tool ToolBuilder::build() const {
    JSON::Object::Entries properties;
    JSON::Array required;

    for (const auto& parameter : this->parameters) {
      const auto schema = makeParameterSchema(parameter.kind, parameter.description, parameter.schema);
      properties.insert({parameter.name, schema});

      if (parameter.required) {
        required.push(JSON::String(parameter.name));
      }
    }

    JSON::Object inputSchema(JSON::Object::Entries {
      {"type", JSON::String("object")},
      {"properties", JSON::Object(properties)}
    });

    if (required.size() > 0) {
      inputSchema.set("required", required);
    }

    Tool tool;
    tool.name = this->name;
    tool.title = this->titleValue;
    tool.description = this->descriptionValue;
    tool.inputSchema = inputSchema;
    tool.annotations = this->annotationsValue;
    tool.metadata = this->metadataValue;
    return tool;
  }

  ToolBuilder& ToolBuilder::addParameter(ParameterKind kind, const String& parameterName, const String& parameterDescription, const JSON::Any& schema, bool required) {
    ToolParameter parameter;
    parameter.name = parameterName;
    parameter.kind = kind;
    parameter.description = parameterDescription;
    parameter.required = required;
    parameter.schema = schema;
    this->parameters.push_back(parameter);
    return *this;
  }
}
