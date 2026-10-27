//
// FILE            corLdCompactTree.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_COMPACT_TREE_H
#define CORLD_COMPACT_TREE_H

#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corTree/CorNode.h"                         // CorNode
#include "corJsonld/CorLdContext.h"                    // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdCompactTree - recursively compact all expanded names in a tree
//
// Uses the core context to compact IRIs back to short names. A name that has to be
// built (corLdCompact's compact IRI) is allocated in kaP.
//
extern void corLdCompactTree(CorNode* treeP, CorAlloc* kaP);

// -----------------------------------------------------------------------------
//
// corLdCompactTreeWith - compact using a specific context.
//
// Same as corLdCompactTree but with a caller-supplied context (e.g. the
// context referenced by a CSR's jsonldContext, so the outbound body
// uses the target broker's vocabulary).
//
extern void corLdCompactTreeWith(CorNode* treeP, CorLdContext* ctxP, CorAlloc* kaP);

#endif  // CORLD_COMPACT_TREE_H
