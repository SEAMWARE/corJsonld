//
// FILE            swldExpand.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_EXPAND_H
#define SWLD_EXPAND_H

#include <stdbool.h>                                 // bool
#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// SwldVocabExpandCheck - callback to validate a short-name before @vocab expansion
//
// Called with the original short-name. Returns true to allow, false to reject.
//
typedef bool (*SwldVocabExpandCheck)(const char* shortName);

extern void swldSetVocabExpandCheck(SwldVocabExpandCheck fn);



// -----------------------------------------------------------------------------
//
// SwldValueCheck - callback to validate a term's value(s) when the term's
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
typedef bool (*SwldValueCheck)(const char* term, const char* datatype, KjNode* valueP);

extern void swldSetValueCheck(SwldValueCheck fn);

// Read accessor for the lib's own expand-tree wiring.
extern SwldValueCheck swldGetValueCheck(void);



// -----------------------------------------------------------------------------
//
// swldExpand -
//
extern char* swldExpand(SwldContext* contextP, const char* name, KAlloc* kaP, SwldItem** itemPP, bool* coreContextP);



// -----------------------------------------------------------------------------
//
// contextItemLookup - look up a term (an "item") by short name in a context
//                     (handles array contexts). Returns the SwldItem or NULL.
//
extern SwldItem* contextItemLookup(SwldContext* contextP, const char* name);



// -----------------------------------------------------------------------------
//
// swldAlreadyExpanded -
//
extern bool swldAlreadyExpanded(const char* value);



// -----------------------------------------------------------------------------
//
// contextVocab - return the active @vocab string for a context (handles
// array-of-contexts recursion). NULL if not declared.
//
extern const char* contextVocab(SwldContext* contextP);

#endif
