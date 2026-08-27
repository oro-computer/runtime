#include "tool.hh"

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>

namespace oro::runtime::mcp {
  struct CompiledJSONSchema {
    using Validator = jsoncons::jsonschema::json_schema<jsoncons::json>;

    explicit CompiledJSONSchema(Validator&& value)
      : validator(std::move(value))
    {}

    Validator validator;
  };

  namespace {
    static constexpr size_t kMaxSchemaValidationDepth = 64;
    static constexpr size_t kMaxSchemaValidationNodes = 10000;

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

    bool isSafeInteger(const nlohmann::json& value) {
      static constexpr int64_t kMaximumSafeInteger = 9007199254740991;

      if (value.is_number_unsigned()) {
        return value.get<uint64_t>() <= static_cast<uint64_t>(kMaximumSafeInteger);
      }
      if (value.is_number_integer()) {
        const auto integer = value.get<int64_t>();
        return integer >= -kMaximumSafeInteger && integer <= kMaximumSafeInteger;
      }
      if (value.is_number_float()) {
        const auto number = value.get<double>();
        return std::isfinite(number) &&
          std::floor(number) == number &&
          number >= -static_cast<double>(kMaximumSafeInteger) &&
          number <= static_cast<double>(kMaximumSafeInteger);
      }
      return false;
    }

    bool matchesType(const nlohmann::json& value, const String& type) {
      if (type == "string") return value.is_string();
      if (type == "boolean") return value.is_boolean();
      if (type == "integer") return isSafeInteger(value);
      return false;
    }

    bool validateComplexity(
      const nlohmann::json& value,
      const String& label,
      String& error,
      size_t depth,
      size_t& nodes
    ) {
      if (depth > kMaxSchemaValidationDepth) {
        error = label + " exceeds the nesting depth limit";
        return false;
      }
      if (++nodes > kMaxSchemaValidationNodes) {
        error = label + " exceeds the value count limit";
        return false;
      }
      if (value.is_array()) {
        for (const auto& item : value) {
          if (!validateComplexity(item, label, error, depth + 1, nodes)) {
            return false;
          }
        }
      } else if (value.is_object()) {
        for (const auto& [key, item] : value.items()) {
          (void)key;
          if (!validateComplexity(item, label, error, depth + 1, nodes)) {
            return false;
          }
        }
      }
      return true;
    }

    bool validateComplexity(
      const nlohmann::json& value,
      const String& label,
      String& error
    ) {
      size_t nodes = 0;
      return validateComplexity(value, label, error, 0, nodes);
    }

    bool compileSchema(
      const JSON::Any& schema,
      const String& label,
      std::shared_ptr<const CompiledJSONSchema>& output,
      String& error
    ) {
      try {
        const auto serialized = schema.str();
        const auto boundedSchema = nlohmann::json::parse(serialized);
        if (!validateComplexity(boundedSchema, label, error)) {
          return false;
        }

        auto parseOptions = jsoncons::json_options {};
        parseOptions.max_nesting_depth(kMaxSchemaValidationDepth);
        auto parsedSchema = jsoncons::json::parse(serialized, parseOptions);
        const auto rejectExternalReference = [](const jsoncons::uri&) {
          return jsoncons::json::null();
        };
        auto dialect = jsoncons::jsonschema::schema_version::draft202012();
        if (boundedSchema.contains("$schema")) {
          if (!boundedSchema["$schema"].is_string()) {
            error = label + " $schema must be a string";
            return false;
          }
          dialect = boundedSchema["$schema"].get<String>();
        }
        auto metaSchema = jsoncons::jsonschema::meta_resolver<jsoncons::json>(
          jsoncons::uri(dialect)
        );
        if (metaSchema.is_null()) {
          error = label + " declares an unsupported JSON Schema dialect: " + dialect;
          return false;
        }
        auto metaValidator = jsoncons::jsonschema::make_json_schema(
          std::move(metaSchema),
          rejectExternalReference,
          jsoncons::jsonschema::evaluation_options {}
        );
        metaValidator.validate(parsedSchema);
        auto validator = jsoncons::jsonschema::make_json_schema(
          std::move(parsedSchema),
          rejectExternalReference,
          jsoncons::jsonschema::evaluation_options {}
        );
        output = std::make_shared<CompiledJSONSchema>(std::move(validator));
        return true;
      } catch (const std::exception& exception) {
        error = label + " is not a supported, valid JSON Schema: " + exception.what();
        return false;
      }
    }

    bool validateValue(
      const nlohmann::json& value,
      const std::shared_ptr<const CompiledJSONSchema>& schema,
      const String& label,
      String& error
    ) {
      if (!validateComplexity(value, label, error)) {
        return false;
      }

      try {
        auto parseOptions = jsoncons::json_options {};
        parseOptions.max_nesting_depth(kMaxSchemaValidationDepth);
        const auto instance = jsoncons::json::parse(value.dump(), parseOptions);
        bool valid = true;
        const auto reporter = [&](
          const jsoncons::jsonschema::validation_message& message
        ) {
          valid = false;
          const auto location = message.instance_location().string();
          error = label + (location.empty() ? "" : " at " + location) +
            ": " + message.message();
          return jsoncons::jsonschema::walk_state::abort;
        };
        schema->validator.validate(instance, reporter);
        return valid;
      } catch (const std::exception& exception) {
        error = "Unable to validate " + label + ": " + exception.what();
        return false;
      }
    }

    bool isHTTPToken(const String& value) {
      if (value.empty()) return false;
      static const String punctuation = "!#$%&'*+-.^_`|~";
      for (const unsigned char character : value) {
        const bool isASCIIAlphaNumeric =
          (character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9');
        if (!isASCIIAlphaNumeric && punctuation.find(character) == String::npos) {
          return false;
        }
      }
      return true;
    }

    bool validateHeaderDeclaration(
      const nlohmann::json& schema,
      Vector<String>& names,
      String& annotation,
      String& type,
      String& error
    ) {
      if (!schema["x-mcp-header"].is_string()) {
        error = "Tool inputSchema x-mcp-header must be a string";
        return false;
      }
      annotation = schema["x-mcp-header"].get<String>();
      if (!isHTTPToken(annotation)) {
        error = "Tool inputSchema x-mcp-header must be a valid HTTP field-name token";
        return false;
      }

      auto lowered = annotation;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
      if (std::find(names.begin(), names.end(), lowered) != names.end()) {
        error = "Tool inputSchema x-mcp-header values must be case-insensitively unique";
        return false;
      }
      names.push_back(lowered);

      if (!schema.contains("type") || !schema["type"].is_string()) {
        error = "Tool inputSchema x-mcp-header requires a single primitive property type";
        return false;
      }
      type = schema["type"].get<String>();
      if (type != "string" && type != "integer" && type != "boolean") {
        error = "Tool inputSchema x-mcp-header may only annotate string, integer, or boolean properties";
        return false;
      }
      return true;
    }

    bool validateHeaderAnnotations(
      const nlohmann::json& schema,
      bool staticallyReachable,
      bool isPropertySchema,
      Vector<String>& names,
      String& error,
      size_t depth
    ) {
      if (depth > kMaxSchemaValidationDepth) {
        error = "Tool inputSchema exceeds the HTTP header annotation depth limit";
        return false;
      }
      if (!schema.is_object()) {
        return true;
      }

      if (schema.contains("x-mcp-header")) {
        if (!staticallyReachable || !isPropertySchema) {
          error = "Tool inputSchema x-mcp-header must be reachable only through properties keys";
          return false;
        }
        String annotation;
        String type;
        if (!validateHeaderDeclaration(schema, names, annotation, type, error)) {
          return false;
        }
      }

      if (schema.contains("properties") && schema["properties"].is_object()) {
        for (const auto& [name, propertySchema] : schema["properties"].items()) {
          (void)name;
          if (!validateHeaderAnnotations(
                propertySchema,
                staticallyReachable,
                true,
                names,
                error,
                depth + 1)) {
            return false;
          }
        }
      }

      static const Vector<String> schemaMapKeywords = {
        "$defs",
        "definitions",
        "patternProperties",
        "dependentSchemas"
      };
      for (const auto& keyword : schemaMapKeywords) {
        if (!schema.contains(keyword) || !schema[keyword].is_object()) {
          continue;
        }
        for (const auto& [name, child] : schema[keyword].items()) {
          (void)name;
          if (!validateHeaderAnnotations(
                child, false, false, names, error, depth + 1)) {
            return false;
          }
        }
      }

      if (schema.contains("dependencies") && schema["dependencies"].is_object()) {
        for (const auto& [name, child] : schema["dependencies"].items()) {
          (void)name;
          if ((child.is_object() || child.is_boolean()) &&
              !validateHeaderAnnotations(
                child, false, false, names, error, depth + 1)) {
            return false;
          }
        }
      }

      static const Vector<String> schemaKeywords = {
        "additionalProperties",
        "unevaluatedProperties",
        "propertyNames",
        "additionalItems",
        "unevaluatedItems",
        "contains",
        "not",
        "if",
        "then",
        "else",
        "contentSchema"
      };
      for (const auto& keyword : schemaKeywords) {
        if (schema.contains(keyword) &&
            !validateHeaderAnnotations(
              schema[keyword], false, false, names, error, depth + 1)) {
          return false;
        }
      }

      static const Vector<String> schemaArrayKeywords = {
        "prefixItems",
        "allOf",
        "anyOf",
        "oneOf"
      };
      for (const auto& keyword : schemaArrayKeywords) {
        if (!schema.contains(keyword) || !schema[keyword].is_array()) {
          continue;
        }
        for (const auto& child : schema[keyword]) {
          if (!validateHeaderAnnotations(
                child, false, false, names, error, depth + 1)) {
            return false;
          }
        }
      }

      if (schema.contains("items")) {
        if (schema["items"].is_array()) {
          for (const auto& child : schema["items"]) {
            if (!validateHeaderAnnotations(
                  child, false, false, names, error, depth + 1)) {
              return false;
            }
          }
        } else if (!validateHeaderAnnotations(
                     schema["items"], false, false, names, error, depth + 1)) {
          return false;
        }
      }

      return true;
    }

    bool collectExpectedHeaders(
      const nlohmann::json& schema,
      const nlohmann::json* value,
      Vector<ToolHeader>& headers,
      Vector<String>& names,
      String& error,
      size_t depth
    ) {
      if (depth > kMaxSchemaValidationDepth) {
        error = "Tool inputSchema exceeds the HTTP header annotation depth limit";
        return false;
      }
      if (!schema.is_object() || !schema.contains("properties") || !schema["properties"].is_object()) {
        return true;
      }

      for (const auto& [propertyName, propertySchema] : schema["properties"].items()) {
        if (!propertySchema.is_object()) {
          continue;
        }
        const nlohmann::json* propertyValue = nullptr;
        if (value != nullptr && value->is_object() && value->contains(propertyName)) {
          propertyValue = &value->at(propertyName);
        }

        if (propertySchema.contains("x-mcp-header")) {
          String annotation;
          String type;
          if (!validateHeaderDeclaration(
                propertySchema, names, annotation, type, error)) {
            return false;
          }

          ToolHeader header;
          header.name = "Mcp-Param-" + annotation;
          header.integer = type == "integer";
          header.present = propertyValue != nullptr && !propertyValue->is_null();
          if (header.present) {
            if (!matchesType(*propertyValue, type)) {
              error = "Tool argument for " + propertyName + " does not match its x-mcp-header type";
              return false;
            }
            if (type == "string") {
              header.value = propertyValue->get<String>();
            } else if (type == "boolean") {
              header.value = propertyValue->get<bool>() ? "true" : "false";
            } else {
              header.value = propertyValue->dump();
            }
          }
          headers.push_back(header);
        }

        if (!collectExpectedHeaders(
              propertySchema,
              propertyValue,
              headers,
              names,
              error,
              depth + 1)) {
          return false;
        }
      }
      return true;
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

  JSON::Object Tool::toJSON(bool modern) const {
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

    bool legacyObjectOutputSchema = false;
    if (this->outputSchema.isObject()) {
      const auto& schema = this->outputSchema.as<JSON::Object>();
      legacyObjectOutputSchema = schema.contains("type") &&
        schema.get("type").isString() &&
        schema.get("type").as<JSON::String>().value() == "object";
    }
    if (!this->outputSchema.isNull() && (modern || legacyObjectOutputSchema)) {
      entries.insert({"outputSchema", this->outputSchema});
    }

    if (!this->icons.isNull()) {
      entries.insert({"icons", this->icons});
    }

    if (!this->metadata.isNull()) {
      entries.insert({"_meta", this->metadata});
    }

    return JSON::Object(entries);
  }

  bool Tool::prepareSchemas(String& error) {
    if (!this->inputSchema.contains("type") ||
        !this->inputSchema.get("type").isString() ||
        this->inputSchema.get("type").as<JSON::String>().value() != "object") {
      error = "Tool inputSchema must declare type 'object' at its root";
      return false;
    }
    if (!compileSchema(
          this->inputSchema,
          "Tool inputSchema",
          this->compiledInputSchema,
          error)) {
      return false;
    }
    try {
      const auto schema = nlohmann::json::parse(this->inputSchema.str());
      Vector<String> names;
      if (!validateHeaderAnnotations(
            schema, true, false, names, error, 0)) {
        return false;
      }
    } catch (const nlohmann::json::exception& exception) {
      error = String("Invalid tool inputSchema: ") + exception.what();
      return false;
    }
    if (!this->outputSchema.isNull() &&
        !compileSchema(
          this->outputSchema,
          "Tool outputSchema",
          this->compiledOutputSchema,
          error)) {
      return false;
    }
    return true;
  }

  bool Tool::validateArguments(const String& value, String& error) const {
    try {
      const auto arguments = nlohmann::json::parse(value.empty() ? "{}" : value);
      if (!arguments.is_object()) {
        error = "Tool arguments must be a JSON object";
        return false;
      }
      auto schema = this->compiledInputSchema;
      if (schema == nullptr &&
          !compileSchema(this->inputSchema, "Tool inputSchema", schema, error)) {
        return false;
      }
      return validateValue(arguments, schema, "Tool arguments", error);
    } catch (const nlohmann::json::exception& exception) {
      error = String("Invalid tool arguments: ") + exception.what();
      return false;
    }
  }

  bool Tool::validateResult(const String& value, String& error) const {
    if (this->outputSchema.isNull()) {
      return true;
    }

    try {
      const auto result = nlohmann::json::parse(value.empty() ? "{}" : value);
      if (!result.is_object()) {
        error = "Tool result must be an MCP tool result object";
        return false;
      }
      if (result.value("isError", false)) {
        return true;
      }
      if (!result.contains("structuredContent")) {
        error = "Tool result must include structuredContent when outputSchema is declared";
        return false;
      }

      auto schema = this->compiledOutputSchema;
      if (schema == nullptr &&
          !compileSchema(this->outputSchema, "Tool outputSchema", schema, error)) {
        return false;
      }
      return validateValue(
        result["structuredContent"],
        schema,
        "Tool result structuredContent",
        error
      );
    } catch (const nlohmann::json::exception& exception) {
      error = String("Invalid tool result: ") + exception.what();
      return false;
    }
  }

  bool Tool::getExpectedHTTPHeaders(
    const String& arguments,
    Vector<ToolHeader>& headers,
    String& error
  ) const {
    headers.clear();
    try {
      const auto value = nlohmann::json::parse(arguments.empty() ? "{}" : arguments);
      const auto schema = nlohmann::json::parse(this->inputSchema.str());
      Vector<String> names;
      if (!validateHeaderAnnotations(
            schema, true, false, names, error, 0)) {
        return false;
      }
      names.clear();
      return collectExpectedHeaders(schema, &value, headers, names, error, 0);
    } catch (const nlohmann::json::exception& exception) {
      error = String("Invalid tool arguments or schema: ") + exception.what();
      return false;
    }
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

  ToolBuilder& ToolBuilder::addStringMaxLength(const String& parameterName, const String& parameterDescription, size_t maximum, bool required) {
    return this->addParameter(
      ParameterKind::String,
      parameterName,
      parameterDescription,
      JSON::Object(JSON::Object::Entries {
        {"maxLength", JSON::Number(static_cast<double>(maximum))}
      }),
      required
    );
  }

  ToolBuilder& ToolBuilder::addNumber(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::Number, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addInteger(const String& parameterName, const String& parameterDescription, bool required) {
    return this->addParameter(ParameterKind::Integer, parameterName, parameterDescription, JSON::Null(), required);
  }

  ToolBuilder& ToolBuilder::addIntegerRange(const String& parameterName, const String& parameterDescription, int64_t minimum, int64_t maximum, bool required) {
    return this->addParameter(
      ParameterKind::Integer,
      parameterName,
      parameterDescription,
      JSON::Object(JSON::Object::Entries {
        {"minimum", JSON::Number(static_cast<double>(minimum))},
        {"maximum", JSON::Number(static_cast<double>(maximum))}
      }),
      required
    );
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

  ToolBuilder& ToolBuilder::outputSchema(const JSON::Any& value) {
    this->outputSchemaValue = value;
    return *this;
  }

  ToolBuilder& ToolBuilder::icons(const JSON::Any& value) {
    this->iconsValue = value;
    return *this;
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
      {"properties", JSON::Object(properties)},
      {"additionalProperties", JSON::Boolean(false)}
    });

    if (required.size() > 0) {
      inputSchema.set("required", required);
    }

    Tool tool;
    tool.name = this->name;
    tool.title = this->titleValue;
    tool.description = this->descriptionValue;
    tool.inputSchema = inputSchema;
    tool.outputSchema = this->outputSchemaValue;
    tool.icons = this->iconsValue;
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
