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



// -----------------------------------------------------------------------------
//
// swldCompactTree - recursively compact all expanded names in a tree
//
// Uses the core context to compact IRIs back to short names.
//
extern void swldCompactTree(KjNode* treeP);

#endif  // SWLD_COMPACT_TREE_H
