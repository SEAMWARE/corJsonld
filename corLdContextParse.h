//
// FILE            corLdContextParse.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_CONTEXT_PARSE_H
#define CORLD_CONTEXT_PARSE_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "corTree/CorNode.h"                         // CorNode
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdContextFromTree -
//
extern CorLdContext* corLdContextFromTree(CorNode* contextNode, KAlloc* kaP, const char* baseUrl);



// -----------------------------------------------------------------------------
//
// corLdContextFromObject -
//
extern CorLdContext* corLdContextFromObject(CorNode* objectNode, KAlloc* kaP, const char* url);

#endif
