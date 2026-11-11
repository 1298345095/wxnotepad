// Original Notepad4 89b8b36 src/EditAutoC_Data0.h; licenses/Notepad4.txt.
#define NP2_KEYWORDS_COUNT	4
typedef const char * const NP2_KEYWORDS[NP2_KEYWORDS_COUNT];

// Doxygen
static const char * const kwDoxyDoc[NP2_KEYWORDS_COUNT] = {
"a addindex addtogroup anchor arg attention author authors b brief bug c callgraph callergraph category cite class code cond copybrief copydetails copydoc copyright date def defgroup deprecated details dir docbookonly dontinclude dot dotfile e else elseif em endcode endcond enddocbookonly enddot endhtmlonly endif endinternal endlatexonly endlink endmanonly endmsc endrtfonly endsecreflist endverbatim endxmlonly enum example exception extends file fn headerfile hideinitializer htmlinclude htmlonly idlexcept if ifnot image implements include includelineno ingroup internal invariant interface latexonly li line link mainpage manonly memberof msc mscfile n name namespace nosubgrouping note overload p package page par paragraph param post pre private privatesection property protected protectedsection protocol public publicsection pure ref refitem related relates relatedalso relatesalso remark remarks result return returns retval rtfonly sa secreflist section see short showinitializer since skip skipline snippet struct subpage subsection subsubsection tableofcontents test throw throws todo tparam typedef union until var verbatim verbinclude version vhdlflow warning weakgroup xmlonly xrefitem "
, // C#
"inheritdoc list description item term listheader para paramref permission seealso summary typeparam typeparamref value "
, // C# Attribute
"type name cref "
,
nullptr
};

// XML Schema
static const char * const kwXmlSchema[NP2_KEYWORDS_COUNT] = {
"xs xsd schema simpleType complexType complexContent simpleContent extension restriction element unique key keyref "
"selector field group all choice sequence any anyAttribute attribute attributeGroup include import redefine notation "
"annotation appinfo documentation "
//
"atomic enumeration list union pattern whiteSpace length minLength maxLength totalDigits fractionDigits "
"minInclusive maxInclusive minExclusive maxExclusive "
, // attribute
"ref targetNamespace finalDefault blockDefault elementFormDefault attributeFormDefault "
"abstract final block mixed base minOccurs maxOccurs nillable nil substitutionGroup default fixed form refer "
"xpath namespace processContents use schemaLocation public system source itemType memberTypes "
, // value
"anyType anySimpleType duration dateTime time date gYear gYearMonth gMonth gMonthDay gDay "
"boolean base64Binary hexBinary float double anyURI QName "
"decimal integer long int short byte nonPositiveInteger negativeInteger nonNegativeInteger positiveInteger "
"unsignedLong unsignedInt unsignedShort unsignedByte "
"string normalizedString token language Name NCName "
"equal ordered bounded cardinality numeric preserve replace collapse "
"unqualified qualified strict skip lax prohibited optional required "
//"blockSet complexDerivationSet XPathExpr URIref "
, nullptr
};

// XML Stylesheet
static const char * const kwXmlStylesheet[NP2_KEYWORDS_COUNT] = {
"xsl stylesheet transform attribute-set character-map output-character decimal-format import import-schema include "
"key namespace-alias output param preserve-space strip-space template variable "
// instruction
"analyze-string apply-imports apply-templates attribute call-template choose when comment copy copy-of document element "
"fallback for-each for-each-group function if matching-substring message namespace next-match non-matching-substring "
"number otherwise perform-sort processing-instruction result-document sequence sort text value-of with-param "
, // attribute
"extension-element-prefixes exclude-result-prefixes xpath-default-namespace default-validation default-collation "
"input-type-annotations use-attribute-sets use-character-maps character string decimal-separator grouping-separator "
"infinity minus-sign NaN percent per-mille zero-digit digit pattern-separator href namespace schema-location match "
"collation stylesheet-prefix result-prefix method byte-order-mark cdata-section-elements doctype-public doctype-system "
"escape-uri-attributes include-content-type indent media-type normalization-form omit-xml-declaration undeclare-prefixes "
"select as required tunnel elements priority regex flags mode separator test copy-namespaces inherit-namespaces "
"group-by group-adjacent group-starting-with group-ending-with override terminate level count from format letter-value "
"ordinal grouping-size output-version order stable case-order data-type disable-output-escaping "
, // value
"preserve strip unspecified default omit lax single multiple any alphabetic traditional ascending descending "
"upper-first lower-first text number xml html xhtml NFC NFD NFKC NFKD fully-normalized none "
, nullptr
};

// Apple Property List
static const char * const kwPList[NP2_KEYWORDS_COUNT] = {
"plist array data date dict key real integer string "
, nullptr, nullptr, nullptr
};


// SVG Document


static NP2_KEYWORDS const *np2_LexKeyword;

