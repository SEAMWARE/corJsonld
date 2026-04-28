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

#endif
