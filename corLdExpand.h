//
// FILE            corLdExpand.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_EXPAND_H
#define CORLD_EXPAND_H

#include <stdbool.h>                                 // bool
#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// KjNode.flags bits (set by corLdExpandTree, read by NGSI-LD code)
//
// These bits are classified ONCE on the core-context CorLdItems (corLdInit) and
// copied verbatim onto each KjNode during expansion (corLdExpandTree) — so the
// broker decides structure with a bit test, never a strcmp chain.
//
// KJF_CORE_TERM - the term is defined by the core @context (any core term:
//   id, type, entities, notification, value, observedAt, …). Such terms keep
//   their short name (not expanded/compacted).
//
// KJF_ATTR_TERM - the term is a structural ATTRIBUTE member, i.e. NOT a
//   sub-attribute: type, value, object, languageMap, vocab, valueList,
//   objectList, json, observedAt, unitCode, datasetId, valueType. (Subset of
//   core terms; entity/operation-level core terms like id/entities are NOT set.)
//
// KJF_VK_* - for the value-key members only, a 4-bit id (bits 4..7) telling
//   WHICH value-key it is, so a Property can reject a foreign value-key
//   (object/languageMap/…) with one comparison. 0 = not a value-key.
//
#define KJF_CORE_TERM   0x01
#define KJF_ATTR_TERM   0x02

#define KJF_VK_SHIFT    4
#define KJF_VK_MASK     0xF0
#define KJF_VK_ID(f)    (((f) & KJF_VK_MASK) >> KJF_VK_SHIFT)

#define KJF_VK_NONE        0
#define KJF_VK_VALUE       1
#define KJF_VK_OBJECT      2
#define KJF_VK_LANGUAGEMAP 3
#define KJF_VK_VOCAB       4
#define KJF_VK_VALUELIST   5
#define KJF_VK_OBJECTLIST  6
#define KJF_VK_JSON        7



// -----------------------------------------------------------------------------
//
// CorLdVocabExpandCheck - callback to validate a short-name before @vocab expansion
//
// Called with the original short-name. Returns true to allow, false to reject.
//
typedef bool (*CorLdVocabExpandCheck)(const char* shortName);

extern void corLdSetVocabExpandCheck(CorLdVocabExpandCheck fn);



// -----------------------------------------------------------------------------
//
// CorLdValueCheck - callback to validate a term's value(s) when the term's
// context binding declares a non-@id / non-@vocab @type (i.e. a datatype
// like xsd:dateTime, xsd:integer, …). Invoked once per scalar value
// (arrays are iterated before invocation).
//
//   term      - short name as it appeared in the user payload
//   datatype  - the @type string from the term def (may be short form
//               like "xsd:dateTime" or a full IRI)
//   valueP    - the node carrying the value (KjString / KjInt / KjFloat /
//               KjBoolean depending on the JSON shape)
//
// Returns true to accept; false to signal "rejected" — the callback owns
// the error-emission path (typically ldError on the broker side).
//
typedef bool (*CorLdValueCheck)(const char* term, const char* datatype, KjNode* valueP);

extern void corLdSetValueCheck(CorLdValueCheck fn);

// Read accessor for the lib's own expand-tree wiring.
extern CorLdValueCheck corLdGetValueCheck(void);



// -----------------------------------------------------------------------------
//
// corLdExpand -
//
extern char* corLdExpand(CorLdContext* contextP, const char* name, KAlloc* kaP, CorLdItem** itemPP, bool* coreContextP);



// -----------------------------------------------------------------------------
//
// contextItemLookup - look up a term (an "item") by short name in a context
//                     (handles array contexts). Returns the CorLdItem or NULL.
//
extern CorLdItem* contextItemLookup(CorLdContext* contextP, const char* name);



// -----------------------------------------------------------------------------
//
// corLdAlreadyExpanded -
//
extern bool corLdAlreadyExpanded(const char* value);



// -----------------------------------------------------------------------------
//
// contextVocab - return the active @vocab string for a context (handles
// array-of-contexts recursion). NULL if not declared.
//
extern const char* contextVocab(CorLdContext* contextP);



// -----------------------------------------------------------------------------
//
// corLdValueObjectIs    - true if obj is a JSON-LD value object (has @value/@type)
// corLdValueObjectCheck - validate a value object's structure (@value required,
//                        @type optional + string, no other members). Structure
//                        only; *detailP gets a static reason string on failure.
//
extern bool corLdValueObjectIs(KjNode* objP);
extern bool corLdValueObjectCheck(KjNode* objP, char** detailP);

#endif
