//
// FILE            swldContextParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_CONTEXT_PARSE_H
#define SWLD_CONTEXT_PARSE_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldContextFromTree -
//
extern SwldContext* swldContextFromTree(KjNode* contextNode, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// swldContextFromObject -
//
extern SwldContext* swldContextFromObject(KjNode* objectNode, KAlloc* kaP, const char* url);

#endif
