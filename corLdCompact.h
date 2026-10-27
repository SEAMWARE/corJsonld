//
// FILE            corLdCompact.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_COMPACT_H
#define CORLD_COMPACT_H

#include "corAlloc/CorAlloc.h"                         // CorAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdCompact - an IRI to the short name contextP (and the core context) give it
//
// Mostly a name the contexts already hold, or a part of iri itself. Only a compact IRI
// (prefix:suffix, when a user term shadows the core @vocab strip) is built, in kaP - so the
// result lives as long as kaP and iri do.
//
extern const char* corLdCompact(CorLdContext* contextP, const char* iri, CorAlloc* kaP);

#endif
