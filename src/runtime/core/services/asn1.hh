#ifndef ORO_RUNTIME_CORE_SERVICES_ASN1_H
#define ORO_RUNTIME_CORE_SERVICES_ASN1_H

#include "../../core.hh"
#include "../../ipc.hh"
#include "../../json.hh"

struct asn1p_s;
struct asn1p_module_s;
struct asn1p_expr_s;
struct asn1p_constraint_s;
struct asn1p_value_s;
struct asn1p_ref_s;
struct asn1p_xports_s;

namespace oro::runtime::core::services {
  class ASN1 : public core::Service {
    public:
      struct ParseOptions {
        bool lexerDebug = false;
        bool includeSourceText = false;
        size_t maxDepth = 64;
      };

      using Module = asn1p_module_s;
      using Expression = asn1p_expr_s;
      using Constraint = asn1p_constraint_s;
      using Value = asn1p_value_s;
      using Document = asn1p_s;

      ASN1 (const Options&);

      void parse (
        const ipc::Message::Seq&,
        const String&,
        const ParseOptions&,
        const Callback
      );

      void parseFile (
        const ipc::Message::Seq&,
        const String&,
        const ParseOptions&,
        const Callback
      );

    private:
      JSON::Object buildDocumentJson (Document*, const ParseOptions&) const;
      JSON::Object buildModuleJson (Module*, const ParseOptions&, size_t depth) const;
      JSON::Any buildExpressionJson (Expression*, const ParseOptions&, size_t depth) const;
      JSON::Any buildConstraintJson (Constraint*, const ParseOptions&, size_t depth) const;
      JSON::Any buildValueJson (Value*) const;
      JSON::Object buildModuleFlagsJson (Module*) const;
      JSON::Array::Entries buildExportsJson (Module*) const;
      JSON::Array::Entries buildImportsJson (Module*) const;
      JSON::Object buildXportsEntry (struct asn1p_xports_s*) const;
      JSON::Array::Entries buildMarkerJson (Expression*) const;
      JSON::Object::Entries buildTagJson (Expression*) const;
      String buildValueRepresentation (Value*) const;
      String buildReferenceString (Value*) const;
      String buildReferenceStringRaw (struct asn1p_ref_s*) const;
      String buildModuleOidString (Module*) const;
      void executeParse (
        const ipc::Message::Seq&,
        String,
        const ParseOptions&,
        const Callback,
        const String&
      ) const;
  };
}

#endif

#ifndef ORO_RUNTIME_HAVE_ASN1C
#define ORO_RUNTIME_HAVE_ASN1C 0
#endif
