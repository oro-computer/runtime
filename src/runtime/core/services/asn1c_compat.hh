#ifndef ORO_RUNTIME_CORE_SERVICES_ASN1C_COMPAT_H
#define ORO_RUNTIME_CORE_SERVICES_ASN1C_COMPAT_H

#include "../../asn1/config.h"
#include <stddef.h>
#include <stdint.h>

extern "C" {
#if defined(__cplusplus)
#  define ORO_RUNTIME_ASN1C_RESTORE_TEMPLATE
#  define template template_
#endif

struct asn1p_value_s;
using asn1p_value_t = struct asn1p_value_s;
struct asn1p_ioc_row_s;
using asn1p_ioc_row_t = struct asn1p_ioc_row_s;
struct asn1p_wsyntx_s;
using asn1p_wsyntx_t = struct asn1p_wsyntx_s;
typedef intmax_t asn1c_integer_t;

#ifndef ASN1_PARSER_CONSTRAINT_H
#define ASN1_PARSER_CONSTRAINT_H

struct asn1p_constraint_s {
  enum asn1p_constraint_type_e {
    ACT_INVALID,
    ACT_EL_TYPE,
    ACT_EL_VALUE,
    ACT_EL_RANGE,
    ACT_EL_LLRANGE,
    ACT_EL_RLRANGE,
    ACT_EL_ULRANGE,
    ACT_EL_EXT,
    ACT_CT_SIZE,
    ACT_CT_FROM,
    ACT_CT_WCOMP,
    ACT_CT_WCOMPS,
    ACT_CT_CTDBY,
    ACT_CT_CTNG,
    ACT_CT_PATTERN,
    ACT_CA_SET,
    ACT_CA_CRC,
    ACT_CA_CSV,
    ACT_CA_UNI,
    ACT_CA_INT,
    ACT_CA_EXC,
    ACT_CA_AEX
  } type;

  enum asn1p_constr_pres_e {
    ACPRES_DEFAULT,
    ACPRES_PRESENT,
    ACPRES_ABSENT,
    ACPRES_OPTIONAL
  } presence;

  struct asn1p_value_s* containedSubtype;
  struct asn1p_value_s* value;
  struct asn1p_value_s* range_start;
  struct asn1p_value_s* range_stop;

  struct asn1p_constraint_s** elements;
  unsigned int el_count;
  unsigned int el_size;
  int _lineno;
};

using asn1p_constraint_t = struct asn1p_constraint_s;

char* asn1p_constraint_type2str(
  asn1p_constraint_t::asn1p_constraint_type_e
);
asn1p_constraint_t* asn1p_constraint_new(int _lineno);
void asn1p_constraint_free(asn1p_constraint_t*);
asn1p_constraint_t* asn1p_constraint_clone(asn1p_constraint_t* source_to_clone);
asn1p_constraint_t* asn1p_constraint_clone_with_resolver(
  asn1p_constraint_t* source_to_clone,
  struct asn1p_value_s* (*resolver)(struct asn1p_value_s*, void*),
  void*
);
int asn1p_constraint_insert(asn1p_constraint_t* into, asn1p_constraint_t* what);
int asn1p_constraint_prepend(asn1p_constraint_t* before, asn1p_constraint_t* what);

#endif /* ASN1_PARSER_CONSTRAINT_H */

/* asn1p_ref.h */
#ifndef ASN1_PARSER_REFERENCE_H
#define ASN1_PARSER_REFERENCE_H
struct asn1p_ref_s {
  struct asn1p_ref_component_s {
    enum asn1p_ref_lex_type_e {
      RLT_UNKNOWN,
      RLT_CAPITALS,
      RLT_Uppercase,
      RLT_lowercase,
      RLT_AmpUppercase,
      RLT_Amplowercase,
      RLT_Atlowercase,
      RLT_AtDotlowercase,
      RLT_MAX
    } lex_type;
    char* name;
  } *components;
  int comp_count;
  int comp_size;
  int _lineno;
};

using asn1p_ref_t = struct asn1p_ref_s;

asn1p_ref_t* asn1p_ref_new(int _lineno);
void asn1p_ref_free(asn1p_ref_t*);
asn1p_ref_t* asn1p_ref_clone(asn1p_ref_t* ref);
int asn1p_ref_add_component(
  asn1p_ref_t*,
  char* name,
  asn1p_ref_s::asn1p_ref_component_s::asn1p_ref_lex_type_e
);
#endif /* ASN1_PARSER_REFERENCE_H */

#include <asn1c/libasn1parser/asn1p_list.h>
#include <asn1c/libasn1parser/asn1p_param.h>
#include "../../asn1/asn1p_expr.h"
#include <asn1c/libasn1parser/asn1parser.h>
#include <asn1c/libasn1parser/asn1p_module.h>
#include <asn1c/libasn1parser/asn1p_xports.h>
#include <asn1c/libasn1parser/asn1p_value.h>
#include <asn1c/libasn1parser/asn1p_class.h>
#include <asn1c/libasn1parser/asn1p_oid.h>
#include <asn1c/libasn1print/asn1print.h>
#include <asn1c/libasn1fix/asn1fix_export.h>

/*
 * Provide C++ friendly replacements for a few ASN.1 parser headers.
 * The originals rely on C-specific constructs (e.g., designated initializers,
 * enum scoping differences, or reserved identifiers). We reproduce only the
 * declarations we need so the runtime can consume the parser from C++.
 */

/* asn1p_expr_str.h */
#ifndef ASN1_PARSER_EXPR_STR_H
#define ASN1_PARSER_EXPR_STR_H
static inline const char* _asn1p_expr_type2string(asn1p_expr_type_e type) {
  switch (type) {
    case ASN_CONSTR_SEQUENCE: return "SEQUENCE";
    case ASN_CONSTR_CHOICE: return "CHOICE";
    case ASN_CONSTR_SET: return "SET";
    case ASN_CONSTR_SEQUENCE_OF: return "SEQUENCE OF";
    case ASN_CONSTR_SET_OF: return "SET OF";
    case ASN_TYPE_ANY: return "ANY";
    case ASN_BASIC_BOOLEAN: return "BOOLEAN";
    case ASN_BASIC_NULL: return "NULL";
    case ASN_BASIC_INTEGER: return "INTEGER";
    case ASN_BASIC_REAL: return "REAL";
    case ASN_BASIC_ENUMERATED: return "ENUMERATED";
    case ASN_BASIC_BIT_STRING: return "BIT STRING";
    case ASN_BASIC_OCTET_STRING: return "OCTET STRING";
    case ASN_BASIC_OBJECT_IDENTIFIER: return "OBJECT IDENTIFIER";
    case ASN_BASIC_RELATIVE_OID: return "RELATIVE-OID";
    case ASN_BASIC_EXTERNAL: return "EXTERNAL";
    case ASN_BASIC_EMBEDDED_PDV: return "EMBEDDED PDV";
    case ASN_BASIC_CHARACTER_STRING: return "CHARACTER STRING";
    case ASN_BASIC_UTCTime: return "UTCTime";
    case ASN_BASIC_GeneralizedTime: return "GeneralizedTime";
    case ASN_STRING_IA5String: return "IA5String";
    case ASN_STRING_PrintableString: return "PrintableString";
    case ASN_STRING_VisibleString: return "VisibleString";
    case ASN_STRING_ISO646String: return "ISO646String";
    case ASN_STRING_NumericString: return "NumericString";
    case ASN_STRING_UniversalString: return "UniversalString";
    case ASN_STRING_BMPString: return "BMPString";
    case ASN_STRING_UTF8String: return "UTF8String";
    case ASN_STRING_GeneralString: return "GeneralString";
    case ASN_STRING_GraphicString: return "GraphicString";
    case ASN_STRING_TeletexString: return "TeletexString";
    case ASN_STRING_T61String: return "T61String";
    case ASN_STRING_VideotexString: return "VideotexString";
    case ASN_STRING_ObjectDescriptor: return "ObjectDescriptor";
    default: return nullptr;
  }
}
#define ASN_EXPR_TYPE2STR(type) _asn1p_expr_type2string(type)
#endif /* ASN1_PARSER_EXPR_STR_H */

/* asn1p_expr2uclass.h */
#ifndef ASN1_PARSER_EXPR2UCLASS_H
#define ASN1_PARSER_EXPR2UCLASS_H
static inline int _asn1p_expr_type2uclass_value(asn1p_expr_type_e type) {
  switch (type) {
    case ASN_BASIC_BOOLEAN: return 1;
    case ASN_BASIC_INTEGER: return 2;
    case ASN_BASIC_BIT_STRING: return 3;
    case ASN_BASIC_OCTET_STRING: return 4;
    case ASN_BASIC_NULL: return 5;
    case ASN_BASIC_OBJECT_IDENTIFIER: return 6;
    case ASN_STRING_ObjectDescriptor: return 7;
    case ASN_BASIC_EXTERNAL: return 8;
    case ASN_BASIC_REAL: return 9;
    case ASN_BASIC_ENUMERATED: return 10;
    case ASN_BASIC_EMBEDDED_PDV: return 11;
    case ASN_STRING_UTF8String: return 12;
    case ASN_BASIC_RELATIVE_OID: return 13;
    case ASN_CONSTR_SEQUENCE:
    case ASN_CONSTR_SEQUENCE_OF: return 16;
    case ASN_CONSTR_SET:
    case ASN_CONSTR_SET_OF: return 17;
    case ASN_STRING_NumericString: return 18;
    case ASN_STRING_PrintableString: return 19;
    case ASN_STRING_TeletexString:
    case ASN_STRING_T61String: return 20;
    case ASN_STRING_VideotexString: return 21;
    case ASN_STRING_IA5String: return 22;
    case ASN_BASIC_UTCTime: return 23;
    case ASN_BASIC_GeneralizedTime: return 24;
    case ASN_STRING_GraphicString: return 25;
    case ASN_STRING_VisibleString:
    case ASN_STRING_ISO646String: return 26;
    case ASN_STRING_GeneralString: return 27;
    case ASN_STRING_UniversalString: return 28;
    case ASN_BASIC_CHARACTER_STRING: return 29;
    case ASN_STRING_BMPString: return 30;
    default: return 0;
  }
}

static inline asn1p_expr_type_e _asn1p_expr_utag2type(int utag) {
  switch (utag) {
    case 1: return ASN_BASIC_BOOLEAN;
    case 2: return ASN_BASIC_INTEGER;
    case 3: return ASN_BASIC_BIT_STRING;
    case 4: return ASN_BASIC_OCTET_STRING;
    case 5: return ASN_BASIC_NULL;
    case 6: return ASN_BASIC_OBJECT_IDENTIFIER;
    case 7: return ASN_STRING_ObjectDescriptor;
    case 8: return ASN_BASIC_EXTERNAL;
    case 9: return ASN_BASIC_REAL;
    case 10: return ASN_BASIC_ENUMERATED;
    case 11: return ASN_BASIC_EMBEDDED_PDV;
    case 12: return ASN_STRING_UTF8String;
    case 13: return ASN_BASIC_RELATIVE_OID;
    case 16: return ASN_CONSTR_SEQUENCE;
    case 17: return ASN_CONSTR_SET;
    case 18: return ASN_STRING_NumericString;
    case 19: return ASN_STRING_PrintableString;
    case 20: return ASN_STRING_TeletexString;
    case 21: return ASN_STRING_VideotexString;
    case 22: return ASN_STRING_IA5String;
    case 23: return ASN_BASIC_UTCTime;
    case 24: return ASN_BASIC_GeneralizedTime;
    case 25: return ASN_STRING_GraphicString;
    case 26: return ASN_STRING_VisibleString;
    case 27: return ASN_STRING_GeneralString;
    case 28: return ASN_STRING_UniversalString;
    case 29: return ASN_BASIC_CHARACTER_STRING;
    case 30: return ASN_STRING_BMPString;
    default: return static_cast<asn1p_expr_type_e>(0);
  }
}

#define ASN_UNIVERSAL_TAG2TYPE(utag) _asn1p_expr_utag2type(static_cast<int>(utag))
#define ASN_UNIVERSAL_TAG2STR(utag) ASN_EXPR_TYPE2STR(ASN_UNIVERSAL_TAG2TYPE(utag))
#endif /* ASN1_PARSER_EXPR2UCLASS_H */

/* asn1p_oid.h */
#ifndef ASN1_PARSER_OID_H
#define ASN1_PARSER_OID_H
struct asn1p_oid_arc_s {
  asn1c_integer_t number;
  char* name;
};

using asn1p_oid_arc_t = struct asn1p_oid_arc_s;

asn1p_oid_arc_t* asn1p_oid_arc_new(const char* optName, asn1c_integer_t optNumber);
void asn1p_oid_arc_free(asn1p_oid_arc_t*);

struct asn1p_oid_s {
  asn1p_oid_arc_t* arcs;
  int arcs_count;
};

using asn1p_oid_t = struct asn1p_oid_s;

asn1p_oid_t* asn1p_oid_new(void);
asn1p_oid_t* asn1p_oid_construct(asn1p_oid_arc_t*, int narcs);
int asn1p_oid_add_arc(asn1p_oid_t*, asn1p_oid_arc_t* tmpl);
void asn1p_oid_free(asn1p_oid_t*);
int asn1p_oid_compare(asn1p_oid_t* a, asn1p_oid_t* b);
#endif /* ASN1_PARSER_OID_H */

typedef struct asn1p_expr_marker_s asn1p_expr_marker_s;

#if defined(ORO_RUNTIME_ASN1C_RESTORE_TEMPLATE)
#  undef template
#  undef ORO_RUNTIME_ASN1C_RESTORE_TEMPLATE
#endif
} // extern "C"

using asn1p_constraint_type_e =
  asn1p_constraint_t::asn1p_constraint_type_e;
using asn1p_constr_pres_e =
  asn1p_constraint_t::asn1p_constr_pres_e;
using asn1p_ref_lex_type_e =
  asn1p_ref_s::asn1p_ref_component_s::asn1p_ref_lex_type_e;
#endif
