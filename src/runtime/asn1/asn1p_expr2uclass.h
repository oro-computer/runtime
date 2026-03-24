#ifndef	ASN1_PARSER_EXPR2UCLASS_H
#define	ASN1_PARSER_EXPR2UCLASS_H

extern "C" {
#include "asn1p_expr.h"
}

#ifndef	__GNUC__
#define	__attribute__(x)	/* unused */
#endif

static int expr_type2uclass_value[ASN_EXPR_TYPE_MAX]
		__attribute__ ((unused)) = {
	[ ASN_BASIC_BOOLEAN ]		= 1,
	[ ASN_BASIC_INTEGER ]		= 2,
	[ ASN_BASIC_BIT_STRING ]	= 3,
	[ ASN_BASIC_OCTET_STRING ]	= 4,
	[ ASN_BASIC_NULL ]		= 5,
	[ ASN_BASIC_OBJECT_IDENTIFIER ]	= 6,
	[ ASN_STRING_ObjectDescriptor ]	= 7,
	[ ASN_BASIC_EXTERNAL ]		= 8,
	[ ASN_BASIC_REAL ]		= 9,
	[ ASN_BASIC_ENUMERATED ]	= 10,
	[ ASN_BASIC_EMBEDDED_PDV ]	= 11,
	[ ASN_STRING_UTF8String ]	= 12,
	[ ASN_BASIC_RELATIVE_OID ]	= 13,
	[ ASN_CONSTR_SEQUENCE ]		= 16,
	[ ASN_CONSTR_SEQUENCE_OF ]	= 16,
	[ ASN_CONSTR_SET ]		= 17,
	[ ASN_CONSTR_SET_OF ]		= 17,
	[ ASN_STRING_NumericString ]	= 18,
	[ ASN_STRING_PrintableString ]	= 19,
	[ ASN_STRING_TeletexString ]	= 20,
	[ ASN_STRING_T61String ]	= 20,
	[ ASN_STRING_VideotexString ]	= 21,
	[ ASN_STRING_IA5String ]	= 22,
	[ ASN_BASIC_UTCTime ]		= 23,
	[ ASN_BASIC_GeneralizedTime ]	= 24,
	[ ASN_STRING_GraphicString ]	= 25,
	[ ASN_STRING_VisibleString ]	= 26,
	[ ASN_STRING_ISO646String ]	= 26,
	[ ASN_STRING_GeneralString ]	= 27,
	[ ASN_STRING_UniversalString ]	= 28,
	[ ASN_BASIC_CHARACTER_STRING ]	= 29,
	[ ASN_STRING_BMPString ]	= 30,
};

static enum asn1p_expr_type expr_utag2type[32] __attribute__ ((unused)) = {
	A1TC_INVALID,
	ASN_BASIC_BOOLEAN,
	ASN_BASIC_INTEGER,
	ASN_BASIC_BIT_STRING,
	ASN_BASIC_OCTET_STRING,
	ASN_BASIC_NULL,
	ASN_BASIC_OBJECT_IDENTIFIER,
	ASN_STRING_ObjectDescriptor,
	ASN_BASIC_EXTERNAL,
	ASN_BASIC_REAL,
	ASN_BASIC_ENUMERATED,
	ASN_BASIC_EMBEDDED_PDV,
	ASN_STRING_UTF8String,
	ASN_BASIC_RELATIVE_OID,
	A1TC_INVALID,
	A1TC_INVALID,
	ASN_CONSTR_SEQUENCE,		/* Or SEQUENCE OF */
	ASN_CONSTR_SET,		/* Or SET OF */
	ASN_STRING_NumericString,	/* " "|"0".."9" */
	ASN_STRING_PrintableString,
	ASN_STRING_TeletexString,
	ASN_STRING_VideotexString,
	ASN_STRING_IA5String,
	ASN_BASIC_UTCTime,
	ASN_BASIC_GeneralizedTime,
	ASN_STRING_GraphicString,
	ASN_STRING_VisibleString,
	ASN_STRING_GeneralString,
	ASN_STRING_UniversalString,	/* 32-bit UCS-4 */
	ASN_BASIC_CHARACTER_STRING,
	ASN_STRING_BMPString,		/* 16-bit UCS-2 */
};

/*
 * Convert the [UNIVERSAL value] into the internal type or a string.
 */
#define	ASN_UNIVERSAL_TAG2TYPE(utag)					\
	(								\
	(((int)(utag)) < 0						\
	|| ((int)(utag)) >= (int)(sizeof(expr_utag2type)		\
		/ sizeof(expr_utag2type[0])))				\
		? 0							\
		: expr_utag2type[(int)(utag)]				\
	)
#define	ASN_UNIVERSAL_TAG2STR(utag)					\
	ASN_EXPR_TYPE2STR(ASN_UNIVERSAL_TAG2TYPE(utag))

#endif	/* ASN1_PARSER_EXPR2UCLASS_H */
