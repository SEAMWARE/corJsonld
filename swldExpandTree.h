//
// FILE            swldExpandTree.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_EXPAND_TREE_H
#define SWLD_EXPAND_TREE_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldExpandTree - recursively expand all names in a parsed JSON-LD tree
//
// Extracts and removes @context from the tree, uses it (or falls back to
// core context) for expansion, and recurses into all objects.
//
extern SwldContext* swldExpandTree(KjNode* treeP, KAlloc* kaP);

#endif  // SWLD_EXPAND_TREE_H
