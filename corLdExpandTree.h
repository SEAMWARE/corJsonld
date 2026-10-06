//
// FILE            corLdExpandTree.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_EXPAND_TREE_H
#define CORLD_EXPAND_TREE_H

#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corTree/CorNode.h"                         // CorNode
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
extern CorLdContext* corLdExpandTree(CorNode* treeP, CorLdContext* userContextP, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// corLdExpandEntityTree - corLdExpandTree for a tree of ENTITY data (an entity, a batch of them, an
// attribute fragment), with JSON-LD array reduction: a member whose value is an array of one element
// takes the element, unless its term keeps its arrays - @container @list / @set / @language, @type @json.
// A language map's entries are reduced too.
//
// Not for an API object (Subscription, Registration, ...): the specification gives their members array
// types the core context does not mark @set, and the ETSI test suite expects those arrays back.
//
extern CorLdContext* corLdExpandEntityTree(CorNode* treeP, CorLdContext* userContextP, CorAlloc* kaP);

#endif  // CORLD_EXPAND_TREE_H
