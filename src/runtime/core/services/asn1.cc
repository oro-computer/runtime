#include "asn1.hh"

#include "../../string.hh"
#include "../../bytes.hh"
#include "../../queued_response.hh"

#include <fstream>
#include <sstream>
#include <memory>
#include <cerrno>
#include <cstring>

#if ORO_RUNTIME_HAVE_ASN1C
extern "C" {
#include "asn1c_compat.hh"
}

namespace {
  using oro::runtime::String;
  using oro::runtime::JSON::Object;
  using oro::runtime::JSON::Array;
  using oro::runtime::JSON::Any;
  using oro::runtime::JSON::null;
  using ASN1ExprMarker = decltype(((asn1p_expr_t*)nullptr)->marker);
  using ASN1ExprMarkerEnum = ASN1ExprMarker::asn1p_expr_marker_e;

  String toString (asn1p_expr_meta_e meta) {
    switch (meta) {
      case AMT_TYPE: return "type";
      case AMT_TYPEREF: return "typeReference";
      case AMT_VALUE: return "value";
      case AMT_VALUESET: return "valueSet";
      case AMT_OBJECT: return "object";
      case AMT_OBJECTCLASS: return "objectClass";
      case AMT_OBJECTFIELD: return "objectField";
      case AMT_INVALID:
      default:
        return "invalid";
    }
  }

  String toString (asn1p_expr_type_e type) {
    const char* cstr = ASN_EXPR_TYPE2STR(type);
    if (cstr && *cstr) {
      return cstr;
    }
    return "unknown";
  }

  String markerFlagToString (ASN1ExprMarkerEnum flag) {
    switch (flag) {
      case ASN1ExprMarker::EM_INDIRECT: return "indirect";
      case ASN1ExprMarker::EM_OMITABLE: return "omitable";
      case ASN1ExprMarker::EM_OPTIONAL: return "optional";
      case ASN1ExprMarker::EM_DEFAULT: return "default";
      case ASN1ExprMarker::EM_UNRECURSE: return "safeName";
      case ASN1ExprMarker::EM_NOMARK:
      default:
        return "none";
    }
  }

  String tagClassToString (int tagClass) {
    switch (tagClass) {
      case asn1p_type_tag_s::TC_UNIVERSAL: return "universal";
      case asn1p_type_tag_s::TC_APPLICATION: return "application";
      case asn1p_type_tag_s::TC_CONTEXT_SPECIFIC: return "context";
      case asn1p_type_tag_s::TC_PRIVATE: return "private";
      default:
        return "none";
    }
  }

  String tagModeToString (int mode) {
    switch (mode) {
      case asn1p_type_tag_s::TM_IMPLICIT: return "implicit";
      case asn1p_type_tag_s::TM_EXPLICIT: return "explicit";
      case asn1p_type_tag_s::TM_DEFAULT:
      default:
        return "default";
    }
  }

  String constraintPresenceToString (asn1p_constr_pres_e presence) {
    switch (presence) {
      case asn1p_constraint_t::ACPRES_PRESENT: return "present";
      case asn1p_constraint_t::ACPRES_ABSENT: return "absent";
      case asn1p_constraint_t::ACPRES_OPTIONAL: return "optional";
      case asn1p_constraint_t::ACPRES_DEFAULT:
      default:
        return "default";
    }
  }

  String constraintTypeToString (asn1p_constraint_type_e type) {
    char* description = asn1p_constraint_type2str(type);
    if (description && *description) {
      return description;
    }
    return "unknown";
  }

  String valueTypeToString (int type) {
    switch (type) {
      case asn1p_value_t::ATV_NOVALUE: return "none";
      case asn1p_value_t::ATV_TYPE: return "type";
      case asn1p_value_t::ATV_NULL: return "null";
      case asn1p_value_t::ATV_REAL: return "real";
      case asn1p_value_t::ATV_INTEGER: return "integer";
      case asn1p_value_t::ATV_MAX: return "max";
      case asn1p_value_t::ATV_MIN: return "min";
      case asn1p_value_t::ATV_TRUE: return "true";
      case asn1p_value_t::ATV_FALSE: return "false";
      case asn1p_value_t::ATV_TUPLE: return "tuple";
      case asn1p_value_t::ATV_QUADRUPLE: return "quadruple";
      case asn1p_value_t::ATV_STRING: return "string";
      case asn1p_value_t::ATV_UNPARSED: return "unparsed";
      case asn1p_value_t::ATV_BITVECTOR: return "bitVector";
      case asn1p_value_t::ATV_VALUESET: return "valueSet";
      case asn1p_value_t::ATV_REFERENCED: return "reference";
      case asn1p_value_t::ATV_CHOICE_IDENTIFIER: return "choiceIdentifier";
      default:
        return "unknown";
    }
  }

  template <typename F>
  void forEach (asn1p_expr_t* head, F&& callback) {
    for (asn1p_expr_t* node = head; node != nullptr; node = node->next.tq_next) {
      callback(node);
    }
  }

  template <typename F>
  void forEachImportExport (asn1p_xports_t* head, F&& callback) {
    for (asn1p_xports_t* node = head; node != nullptr; node = node->xp_next.tq_next) {
      callback(node);
    }
  }
}

namespace oro::runtime::core::services {
  ASN1::ASN1 (const Options& options)
    : core::Service(options)
  {}

  void ASN1::parse (
    const ipc::Message::Seq& seq,
    const String& source,
    const ParseOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      this->executeParse(seq, source, options, callback, "asn1.parse");
    });
  }

  void ASN1::parseFile (
    const ipc::Message::Seq& seq,
    const String& path,
    const ParseOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      std::ifstream file(path, std::ios::in | std::ios::binary);
      if (!file.is_open()) {
        JSON::Object::Entries response;
        response["source"] = String("asn1.parseFile");
        response["err"] = JSON::Object::Entries {
          {"type", String("NotFoundError")},
          {"message", String("Unable to open ASN.1 source file")},
          {"code", std::to_string(errno)},
          {"path", path}
        };
        return callback(seq, response, QueuedResponse{});
      }

      std::ostringstream buffer;
      buffer << file.rdbuf();
      if (!file.good() && !file.eof()) {
        const auto code = errno;
        JSON::Object::Entries response;
        response["source"] = String("asn1.parseFile");
        response["err"] = JSON::Object::Entries {
          {"type", String("IOError")},
          {"message", String("Failed to read ASN.1 file contents")},
          {"code", std::to_string(code)},
          {"path", path}
        };
        return callback(seq, response, QueuedResponse{});
      }

      const String contents = buffer.str();
      this->executeParse(seq, contents, options, callback, "asn1.parseFile");
    });
  }

  void ASN1::executeParse (
    const ipc::Message::Seq& seq,
    String input,
    const ParseOptions& options,
    const Callback callback,
    const String& sourceLabel
  ) const {
    const enum asn1p_flags flags = options.lexerDebug ? A1P_LEXER_DEBUG : A1P_NOFLAGS;
    errno = 0;
    asn1p_t* document = asn1p_parse_buffer(input.c_str(), static_cast<int>(input.size()), flags);

    if (document == nullptr) {
      const int code = errno;
      const char* description = std::strerror(code);
      JSON::Object::Entries response;
      response["source"] = sourceLabel;
      response["err"] = JSON::Object::Entries {
        {"type", String("ASN1ParseError")},
        {"message", description ? String(description) : String("Unable to parse ASN.1 input")},
        {"code", std::to_string(code)}
      };
      return callback(seq, response, QueuedResponse{});
    }

    std::unique_ptr<asn1p_t, decltype(&asn1p_delete)> guard(document, asn1p_delete);
    auto documentJson = this->buildDocumentJson(document, options);

    if (options.includeSourceText) {
      documentJson.set("sourceText", input);
    }

    JSON::Object::Entries response;
    response["source"] = sourceLabel;
    response["data"] = documentJson;
    callback(seq, response, QueuedResponse{});
  }

  Object ASN1::buildDocumentJson (Document* document, const ParseOptions& options) const {
    JSON::Object::Entries root;
    JSON::Array::Entries modules;

    for (asn1p_module_t* mod = document->modules.tq_head; mod != nullptr; mod = mod->mod_next.tq_next) {
      modules.push_back(this->buildModuleJson(mod, options, 0));
    }

    root["modules"] = JSON::Array(modules);
    root["modulesCount"] = static_cast<uint64_t>(modules.size());
    root["lexerDebug"] = options.lexerDebug;
    root["maxDepth"] = static_cast<uint64_t>(options.maxDepth);

    return Object(root);
  }

  Object ASN1::buildModuleJson (Module* module, const ParseOptions& options, size_t depth) const {
    JSON::Object::Entries entry;

    if (module->ModuleName) {
      entry["name"] = String(module->ModuleName);
    }

    if (module->source_file_name) {
      entry["sourceFile"] = String(module->source_file_name);
    }

    const auto oidString = this->buildModuleOidString(module);
    if (!oidString.empty()) {
      entry["oid"] = oidString;
    }

    entry["flags"] = this->buildModuleFlagsJson(module);

    const auto exports = this->buildExportsJson(module);
    if (!exports.empty()) {
      entry["exports"] = JSON::Array(exports);
    }

    const auto imports = this->buildImportsJson(module);
    if (!imports.empty()) {
      entry["imports"] = JSON::Array(imports);
    }

    if (depth >= options.maxDepth) {
      entry["truncated"] = true;
      return Object(entry);
    }

    JSON::Array::Entries members;
    forEach(module->members.tq_head, [&](asn1p_expr_t* expr) {
      members.push_back(this->buildExpressionJson(expr, options, depth + 1));
    });

    entry["members"] = JSON::Array(members);
    return Object(entry);
  }

  Any ASN1::buildExpressionJson (
    Expression* expr,
    const ParseOptions& options,
    size_t depth
  ) const {
    if (expr == nullptr) {
      return null;
    }

    JSON::Object::Entries entry;
    entry["metaType"] = toString(expr->meta_type);
    entry["exprType"] = toString(expr->expr_type);
    entry["line"] = expr->_lineno;
    entry["unique"] = static_cast<bool>(expr->unique);

    if (expr->Identifier) {
      entry["identifier"] = String(expr->Identifier);
    }

    if (expr->reference) {
      entry["reference"] = this->buildReferenceStringRaw(expr->reference);
    }

    if (expr->marker.flags != ASN1ExprMarker::EM_NOMARK) {
      const auto markers = this->buildMarkerJson(expr);
      if (!markers.empty()) {
        entry["markers"] = JSON::Array(markers);
      }
      if (expr->marker.flags & ASN1ExprMarker::EM_DEFAULT) {
        entry["defaultValue"] = this->buildValueJson(expr->marker.default_value);
      }
    }

    const auto tagEntries = this->buildTagJson(expr);
    if (!tagEntries.empty()) {
      entry["tag"] = Object(tagEntries);
    }

    if (expr->value) {
      entry["value"] = this->buildValueJson(expr->value);
    }

    if (expr->constraints) {
      entry["constraints"] = this->buildConstraintJson(expr->constraints, options, depth + 1);
    }

    if (depth >= options.maxDepth) {
      entry["truncated"] = true;
      return Object(entry);
    }

    JSON::Array::Entries children;
    forEach(expr->members.tq_head, [&](asn1p_expr_t* child) {
      children.push_back(this->buildExpressionJson(child, options, depth + 1));
    });

    if (!children.empty()) {
      entry["members"] = JSON::Array(children);
    }

    return Object(entry);
  }

  Any ASN1::buildConstraintJson (
    Constraint* constraint,
    const ParseOptions& options,
    size_t depth
  ) const {
    if (constraint == nullptr) {
      return null;
    }

    JSON::Object::Entries entry;
    entry["type"] = constraintTypeToString(constraint->type);
    entry["presence"] = constraintPresenceToString(constraint->presence);
    entry["line"] = constraint->_lineno;

    if (constraint->value) {
      entry["value"] = this->buildValueJson(constraint->value);
    }

    if (constraint->range_start || constraint->range_stop) {
      JSON::Object::Entries range;
      if (constraint->range_start) {
        range["start"] = this->buildValueJson(constraint->range_start);
      }
      if (constraint->range_stop) {
        range["stop"] = this->buildValueJson(constraint->range_stop);
      }
      entry["range"] = Object(range);
    }

    if (depth >= options.maxDepth) {
      entry["truncated"] = true;
      return Object(entry);
    }

    if (constraint->el_count > 0 && constraint->elements != nullptr) {
      JSON::Array::Entries elements;
      for (unsigned int i = 0; i < constraint->el_count; ++i) {
        if (constraint->elements[i] != nullptr) {
          elements.push_back(this->buildConstraintJson(constraint->elements[i], options, depth + 1));
        }
      }
      entry["elements"] = JSON::Array(elements);
    }

    return Object(entry);
  }

  Any ASN1::buildValueJson (Value* value) const {
    if (value == nullptr) {
      return null;
    }

    JSON::Object::Entries entry;
    entry["type"] = valueTypeToString(value->type);

    const auto representation = this->buildValueRepresentation(value);
    if (!representation.empty()) {
      entry["repr"] = representation;
    }

    switch (value->type) {
      case asn1p_value_t::ATV_INTEGER:
        entry["integer"] = std::to_string(value->value.v_integer);
        break;
      case asn1p_value_t::ATV_REAL:
        entry["real"] = value->value.v_double;
        break;
      case asn1p_value_t::ATV_TUPLE:
      case asn1p_value_t::ATV_QUADRUPLE:
      case asn1p_value_t::ATV_BITVECTOR: {
        JSON::Array::Entries bytes;
        if (value->type == asn1p_value_t::ATV_BITVECTOR) {
          const auto& vector = value->value.binary_vector;
          const size_t byteCount = (vector.size_in_bits + 7) / 8;
          for (size_t i = 0; i < byteCount; ++i) {
            bytes.emplace_back(static_cast<uint32_t>(vector.bits[i]));
          }
          entry["sizeInBits"] = static_cast<uint64_t>(vector.size_in_bits);
        } else {
          const uint8_t* data = value->value.string.buf;
          const auto size = static_cast<size_t>(value->value.string.size);
          for (size_t i = 0; i < size; ++i) {
            bytes.emplace_back(static_cast<uint32_t>(data[i]));
          }
        }
        if (!bytes.empty()) {
          entry["bytes"] = JSON::Array(bytes);
        }
        break;
      }
      case asn1p_value_t::ATV_STRING:
      case asn1p_value_t::ATV_UNPARSED:
        if (value->value.string.buf) {
          entry["string"] = String(reinterpret_cast<const char*>(value->value.string.buf));
        }
        break;
      case asn1p_value_t::ATV_REFERENCED:
        entry["reference"] = this->buildReferenceStringRaw(value->value.reference);
        break;
      case asn1p_value_t::ATV_VALUESET:
        entry["valueSet"] = this->buildConstraintJson(value->value.constraint, ASN1::ParseOptions{}, 0);
        break;
      case asn1p_value_t::ATV_CHOICE_IDENTIFIER:
        if (value->value.choice_identifier.identifier) {
          entry["identifier"] = String(value->value.choice_identifier.identifier);
        }
        if (value->value.choice_identifier.value) {
          entry["choiceValue"] = this->buildValueJson(value->value.choice_identifier.value);
        }
        break;
      default:
        break;
    }

    return Object(entry);
  }

  String ASN1::buildValueRepresentation (Value* value) const {
    if (value == nullptr) {
      return {};
    }

    switch (value->type) {
      case asn1p_value_t::ATV_NOVALUE:
        return "<NO VALUE>";
      case asn1p_value_t::ATV_NULL:
        return "NULL";
      case asn1p_value_t::ATV_REAL:
        return std::to_string(value->value.v_double);
      case asn1p_value_t::ATV_INTEGER:
        return std::to_string(value->value.v_integer);
      case asn1p_value_t::ATV_MIN:
        return "MIN";
      case asn1p_value_t::ATV_MAX:
        return "MAX";
      case asn1p_value_t::ATV_FALSE:
        return "FALSE";
      case asn1p_value_t::ATV_TRUE:
        return "TRUE";
      case asn1p_value_t::ATV_TUPLE:
        return "{" + std::to_string(value->value.v_integer >> 4) + ", " +
          std::to_string(value->value.v_integer & 0xff) + "}";
      case asn1p_value_t::ATV_QUADRUPLE:
        return "{" + std::to_string((value->value.v_integer >> 24) & 0xff) + ", " +
          std::to_string((value->value.v_integer >> 16) & 0xff) + ", " +
          std::to_string((value->value.v_integer >> 8) & 0xff) + ", " +
          std::to_string(value->value.v_integer & 0xff) + "}";
      case asn1p_value_t::ATV_STRING:
      case asn1p_value_t::ATV_UNPARSED:
        if (value->value.string.buf == nullptr || value->value.string.size <= 0) {
          return {};
        }
        return String(
          reinterpret_cast<const char*>(value->value.string.buf),
          static_cast<size_t>(value->value.string.size)
        );
      case asn1p_value_t::ATV_TYPE:
        return "<Type>";
      case asn1p_value_t::ATV_BITVECTOR: {
        const auto& vector = value->value.binary_vector;
        if (vector.bits == nullptr || vector.size_in_bits <= 0) {
          return "''H";
        }

        String result("'");
        if (vector.size_in_bits % 8 != 0) {
          result.reserve(static_cast<size_t>(vector.size_in_bits) + 3);
          for (int i = 0; i < vector.size_in_bits; ++i) {
            const auto byte = vector.bits[i >> 3];
            result.push_back(((byte >> (7 - (i % 8))) & 1) ? '1' : '0');
          }
          result.append("'B");
        } else {
          static constexpr char hex[] = "0123456789ABCDEF";
          const auto byteCount = vector.size_in_bits / 8;
          result.reserve(static_cast<size_t>(byteCount * 2) + 3);
          for (int i = 0; i < byteCount; ++i) {
            result.push_back(hex[vector.bits[i] >> 4]);
            result.push_back(hex[vector.bits[i] & 0x0f]);
          }
          result.append("'H");
        }
        return result;
      }
      case asn1p_value_t::ATV_REFERENCED:
        return this->buildReferenceStringRaw(value->value.reference);
      case asn1p_value_t::ATV_VALUESET:
        return "<ValueSet>";
      case asn1p_value_t::ATV_CHOICE_IDENTIFIER: {
        String result;
        if (value->value.choice_identifier.identifier) {
          result = value->value.choice_identifier.identifier;
        }
        if (value->value.choice_identifier.value) {
          if (!result.empty()) {
            result.append(": ");
          }
          result.append(this->buildValueRepresentation(value->value.choice_identifier.value));
        }
        return result;
      }
      default:
        return "<some complex value>";
    }
  }

  Object ASN1::buildModuleFlagsJson (Module* module) const {
    JSON::Object::Entries flags;
    const auto mf = module->module_flags;

    flags["automaticTags"] = static_cast<bool>(mf & MSF_AUTOMATIC_TAGS);
    flags["implicitTags"] = static_cast<bool>(mf & MSF_IMPLICIT_TAGS);
    flags["explicitTags"] = static_cast<bool>(mf & MSF_EXPLICIT_TAGS);
    flags["extensibilityImplied"] = static_cast<bool>(mf & MSF_EXTENSIBILITY_IMPLIED);
    flags["tagInstructions"] = static_cast<bool>(mf & MSF_TAG_INSTRUCTIONS);
    flags["xerInstructions"] = static_cast<bool>(mf & MSF_XER_INSTRUCTIONS);
    flags["unknownInstructions"] = static_cast<bool>(mf & MSF_unk_INSTRUCTIONS);

    return Object(flags);
  }

  JSON::Array::Entries ASN1::buildExportsJson (Module* module) const {
    JSON::Array::Entries entries;
    forEachImportExport(module->exports.tq_head, [&](asn1p_xports_t* xp) {
      entries.push_back(this->buildXportsEntry(xp));
    });
    return entries;
  }

  JSON::Array::Entries ASN1::buildImportsJson (Module* module) const {
    JSON::Array::Entries entries;
    forEachImportExport(module->imports.tq_head, [&](asn1p_xports_t* xp) {
      entries.push_back(this->buildXportsEntry(xp));
    });
    return entries;
  }

  Object ASN1::buildXportsEntry (asn1p_xports_t* xp) const {
    JSON::Object::Entries result;

    if (xp->fromModuleName) {
      result["module"] = String(xp->fromModuleName);
    }

    if (xp->identifier.oid) {
      JSON::Array::Entries arcs;
      for (int i = 0; i < xp->identifier.oid->arcs_count; ++i) {
        const auto& arc = xp->identifier.oid->arcs[i];
        JSON::Object::Entries arcEntry;
        arcEntry["number"] = std::to_string(arc.number);
        if (arc.name) {
          arcEntry["name"] = String(arc.name);
        }
        arcs.push_back(Object(arcEntry));
      }
      if (!arcs.empty()) {
        result["oid"] = JSON::Array(arcs);
      }
    }

    JSON::Array::Entries symbols;
    forEach(xp->members.tq_head, [&](asn1p_expr_t* expr) {
      JSON::Object::Entries symbol;
      if (expr->Identifier) {
        symbol["identifier"] = String(expr->Identifier);
      }
      symbol["metaType"] = toString(expr->meta_type);
      symbol["exprType"] = toString(expr->expr_type);
      symbols.push_back(Object(symbol));
    });

    result["symbols"] = JSON::Array(symbols);
    result["kind"] = xp->xports_type == asn1p_xports_t::XPT_IMPORTS ? "imports" : "exports";

    return Object(result);
  }

  JSON::Array::Entries ASN1::buildMarkerJson (Expression* expr) const {
    JSON::Array::Entries markers;
    const auto flags = expr->marker.flags;

    if (flags & ASN1ExprMarker::EM_INDIRECT) {
      markers.emplace_back(markerFlagToString(ASN1ExprMarker::EM_INDIRECT));
    }
    if (flags & ASN1ExprMarker::EM_OMITABLE) {
      markers.emplace_back(markerFlagToString(ASN1ExprMarker::EM_OMITABLE));
    }
    if (flags & ASN1ExprMarker::EM_OPTIONAL) {
      markers.emplace_back(markerFlagToString(ASN1ExprMarker::EM_OPTIONAL));
    }
    if (flags & ASN1ExprMarker::EM_DEFAULT) {
      markers.emplace_back(markerFlagToString(ASN1ExprMarker::EM_DEFAULT));
    }
    if (flags & ASN1ExprMarker::EM_UNRECURSE) {
      markers.emplace_back(markerFlagToString(ASN1ExprMarker::EM_UNRECURSE));
    }

    return markers;
  }

  JSON::Object::Entries ASN1::buildTagJson (Expression* expr) const {
    JSON::Object::Entries tag;
    if (expr->tag.tag_class == asn1p_type_tag_s::TC_NOCLASS) {
      return tag;
    }

    char buffer[TAG2STRING_BUFFER_SIZE] = {0};
    const char* description = asn1p_tag2string(&expr->tag, buffer);

    if (description && *description) {
      tag["description"] = String(description);
    }

    tag["class"] = tagClassToString(expr->tag.tag_class);
    tag["mode"] = tagModeToString(expr->tag.tag_mode);
    tag["value"] = std::to_string(expr->tag.tag_value);

    return tag;
  }

  String ASN1::buildReferenceString (Value* value) const {
    if (value == nullptr) {
      return {};
    }

    if (value->type == asn1p_value_t::ATV_REFERENCED) {
      return this->buildReferenceStringRaw(value->value.reference);
    }

    return {};
  }

  String ASN1::buildReferenceStringRaw (asn1p_ref_s* ref) const {
    if (ref == nullptr || ref->comp_count <= 0) {
      return {};
    }

    String result;
    result.reserve(static_cast<size_t>(ref->comp_count) * 8);

    for (int i = 0; i < ref->comp_count; ++i) {
      if (i > 0) {
        result.push_back('.');
      }
      const auto* component = &ref->components[i];
      if (component->name) {
        result.append(component->name);
      }
    }

    return result;
  }

  String ASN1::buildModuleOidString (Module* module) const {
    if (module == nullptr || module->module_oid == nullptr || module->module_oid->arcs_count <= 0) {
      return {};
    }

    String result;
    bool first = true;
    for (int i = 0; i < module->module_oid->arcs_count; ++i) {
      const auto& arc = module->module_oid->arcs[i];
      if (!first) {
        result.push_back('.');
      }
      first = false;
      if (arc.number >= 0) {
        result.append(std::to_string(arc.number));
      }
      if (arc.name) {
        if (arc.number >= 0) {
          result.push_back(' ');
        }
        result.append(arc.name);
      }
    }

    return result;
  }
}
#endif /* ORO_RUNTIME_HAVE_ASN1C */

#if !ORO_RUNTIME_HAVE_ASN1C
namespace oro::runtime::core::services {
  ASN1::ASN1 (const Options& options)
    : core::Service(options)
  {}

  void ASN1::parse (
    const ipc::Message::Seq& seq,
    const String&,
    const ParseOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      JSON::Object::Entries response;
      response["source"] = String("asn1.parse");
      response["err"] = JSON::Object::Entries {
        {"type", String("NotSupportedError")},
        {"message", String("ASN.1 parsing is not available (asn1c sources missing)")}
      };
      callback(seq, response, QueuedResponse{});
    });
  }

  void ASN1::parseFile (
    const ipc::Message::Seq& seq,
    const String&,
    const ParseOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      JSON::Object::Entries response;
      response["source"] = String("asn1.parseFile");
      response["err"] = JSON::Object::Entries {
        {"type", String("NotSupportedError")},
        {"message", String("ASN.1 parsing is not available (asn1c sources missing)")}
      };
      callback(seq, response, QueuedResponse{});
    });
  }

  oro::runtime::JSON::Object ASN1::buildDocumentJson (Document*, const ParseOptions&) const {
    return oro::runtime::JSON::Object();
  }

  oro::runtime::JSON::Object ASN1::buildModuleJson (Module*, const ParseOptions&, size_t) const {
    return oro::runtime::JSON::Object();
  }

  oro::runtime::JSON::Any ASN1::buildExpressionJson (Expression*, const ParseOptions&, size_t) const {
    return oro::runtime::JSON::null;
  }

  oro::runtime::JSON::Any ASN1::buildConstraintJson (Constraint*, const ParseOptions&, size_t) const {
    return oro::runtime::JSON::null;
  }

  oro::runtime::JSON::Any ASN1::buildValueJson (Value*) const {
    return oro::runtime::JSON::null;
  }

  oro::runtime::JSON::Object ASN1::buildModuleFlagsJson (Module*) const {
    return oro::runtime::JSON::Object();
  }

  oro::runtime::JSON::Array::Entries ASN1::buildExportsJson (Module*) const {
    return {};
  }

  oro::runtime::JSON::Array::Entries ASN1::buildImportsJson (Module*) const {
    return {};
  }

  oro::runtime::JSON::Object ASN1::buildXportsEntry (asn1p_xports_s*) const {
    return oro::runtime::JSON::Object();
  }

  oro::runtime::JSON::Array::Entries ASN1::buildMarkerJson (Expression*) const {
    return {};
  }

  oro::runtime::JSON::Object::Entries ASN1::buildTagJson (Expression*) const {
    return {};
  }

  String ASN1::buildValueRepresentation (Value*) const {
    return {};
  }

  String ASN1::buildReferenceString (Value*) const {
    return {};
  }

  String ASN1::buildReferenceStringRaw (asn1p_ref_s*) const {
    return {};
  }

  String ASN1::buildModuleOidString (Module*) const {
    return {};
  }

  void ASN1::executeParse (
    const ipc::Message::Seq& seq,
    String,
    const ParseOptions&,
    const Callback callback,
    const String& sourceLabel
  ) const {
    JSON::Object::Entries response;
    response["source"] = sourceLabel;
    response["err"] = JSON::Object::Entries {
      {"type", String("NotSupportedError")},
      {"message", String("ASN.1 parsing is not available (asn1c sources missing)")}
    };
    callback(seq, response, QueuedResponse{});
  }
}
#endif /* !ORO_RUNTIME_HAVE_ASN1C */
