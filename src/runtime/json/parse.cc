#include "../json.hh"

#include <nlohmann/json.hpp>

namespace {
  using nlohmann::json;
  using oro::runtime::JSON::Any;
  using oro::runtime::JSON::Array;
  using oro::runtime::JSON::Object;
  using oro::runtime::JSON::Raw;

  Any toJSONAny (const json& value) {
    if (value.is_null()) {
      return Any(nullptr);
    }

    if (value.is_boolean()) {
      return Any(value.get<bool>());
    }

    if (value.is_number_unsigned()) {
      return Any(value.get<uint64_t>());
    }

    if (value.is_number_integer()) {
      return Any(value.get<int64_t>());
    }

    if (value.is_number_float()) {
      return Any(value.get<double>());
    }

    if (value.is_string()) {
      return Any(value.get<std::string>());
    }

    if (value.is_array()) {
      Array::Entries entries;
      entries.reserve(value.size());
      for (const auto& element : value) {
        entries.emplace_back(toJSONAny(element));
      }
      return Any(entries);
    }

    if (value.is_object()) {
      Object::Entries entries;
      for (auto it = value.begin(); it != value.end(); ++it) {
        entries.insert_or_assign(it.key(), toJSONAny(it.value()));
      }
      return Any(entries);
    }

    if (value.is_binary()) {
      const auto& binary = value.get_binary();
      return Any(Raw(oro::runtime::String(binary.begin(), binary.end())));
    }

    return Any(Raw(value.dump()));
  }
}

namespace oro::runtime::JSON {
  Any parse (const runtime::String& source) {
    try {
      const auto parsed = nlohmann::json::parse(source);
      return toJSONAny(parsed);
    } catch (const nlohmann::json::parse_error& error) {
      Error result("SyntaxError", error.what());
      result.code = static_cast<int>(error.id);
      runtime::StringStream location;
      location << "byte " << error.byte;
      result.location = location.str();
      throw result;
    } catch (const nlohmann::json::exception& error) {
      Error result("JSONError", error.what());
      result.code = static_cast<int>(error.id);
      throw result;
    }
  }

  Any parse (const char* source) {
    if (source == nullptr) {
      throw Error("TypeError", "cannot parse null JSON source");
    }

    return parse(runtime::String(source));
  }
}
