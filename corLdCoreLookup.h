//
// FILE            corLdCoreLookup.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_CORE_LOOKUP_H
#define CORLD_CORE_LOOKUP_H

#include "corJsonld/CorLdItem.h"                       // CorLdItem



// -----------------------------------------------------------------------------
//
// corLdCoreLookup - the core term a name denotes, or NULL - lookup only, no allocation
//
// For a parser that must know, the moment a member name is final, whether it is a
// CORE term - before any user @context is known, and without paying for the
// expansion of a name that turns out not to be one. corLdExpand answers the same
// question, but with the core context alone every user term falls through to the
// @vocab step (an allocation and two copies) only to be thrown away.
//
// It resolves a name exactly as corLdExpand does on the core context, and returns
// the item corLdExpand would report:
//
//   short name ("observedAt")                     one probe of the core
//   full IRI ("https://uri.etsi.org/ngsi-ld/...") one probe of the pristine core
//   compact IRI ("ngsi-ld:observedAt")            the IRI built in a stack buffer, one probe
//   JSON-LD keyword                               "@id" -> id, "@type" -> type (their IRIs), others NULL
//   anything else                                 NULL
//
// NULL means "not a core term as far as the core alone can tell". A compact IRI whose
// prefix only a USER context defines can still expand to a core term - that name
// must be expanded again once the user @context is known.
//
extern CorLdItem* corLdCoreLookup(const char* name);

#endif  // CORLD_CORE_LOOKUP_H
