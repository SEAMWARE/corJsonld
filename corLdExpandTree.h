//
// FILE            corLdExpandTree.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_EXPAND_TREE_H
#define CORLD_EXPAND_TREE_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kjson/KjNode.h"                            // KjNode
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdExpandTree - recursively expand all names in a parsed JSON-LD tree
//
// userContextP is the request's @context (from Link header or already-
// parsed body @context — what corNgsild.contextP holds). It is always
// supplied; the core context is built into the JSON-LD machinery, never
// a "fallback" here. If the tree carries its own @context (root for an
// object body, per-element for a batch array — both per § 4.6.x), it
// overrides userContextP for that subtree only and is stripped after
// use.
//
// Returns the effective top-level context (the tree's own @context if
// present, else userContextP unchanged). Callers parsing a request body
// chain this into corNgsild.contextP so the rest of the pipeline reflects
// any in-body context override.
//
extern CorLdContext* corLdExpandTree(KjNode* treeP, CorLdContext* userContextP, KAlloc* kaP);

#endif  // CORLD_EXPAND_TREE_H
