//
// FILE            corLdContextParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_CONTEXT_PARSE_H
#define CORLD_CONTEXT_PARSE_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdContextFromTree -
//
extern CorLdContext* corLdContextFromTree(KjNode* contextNode, KAlloc* kaP, const char* baseUrl);



// -----------------------------------------------------------------------------
//
// corLdContextFromObject -
//
extern CorLdContext* corLdContextFromObject(KjNode* objectNode, KAlloc* kaP, const char* url);

#endif
