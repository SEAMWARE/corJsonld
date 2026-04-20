//
// FILE            swldCompactTree.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_COMPACT_TREE_H
#define SWLD_COMPACT_TREE_H

#include "kjson/KjNode.h"                            // KjNode
#include "swJsonld/SwldContext.h"                    // SwldContext



// -----------------------------------------------------------------------------
//
// swldCompactTree - recursively compact all expanded names in a tree
//
// Uses the core context to compact IRIs back to short names.
//
extern void swldCompactTree(KjNode* treeP);

// -----------------------------------------------------------------------------
//
// swldCompactTreeWith - compact using a specific context.
//
// Same as swldCompactTree but with a caller-supplied context (e.g. the
// context referenced by a CSR's jsonldContext, so the outbound body
// uses the target broker's vocabulary).
//
extern void swldCompactTreeWith(KjNode* treeP, SwldContext* ctxP);

#endif  // SWLD_COMPACT_TREE_H
