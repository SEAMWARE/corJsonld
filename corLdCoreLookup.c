//
// FILE            corLdCoreLookup.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                  // strchr, strncmp, strlen, memcpy

#include "corHash/corHash.h"                         // corHashItemLookup
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdInit.h"                        // corLdCoreContext, corLdCorePristine, corLdCoreItemByIri
#include "corJsonld/corLdExpand.h"                      // contextItemLookup, corLdAlreadyExpanded
#include "corJsonld/corLdCoreLookup.h"                  // Own interface



// -----------------------------------------------------------------------------
//
// compactIriLookup - "pfx:suffix" -> the core item its expansion names, or NULL
//
// The core half of corLdPrefixExpand without the allocation: the prefix's real IRI
// comes from the pristine core (the rewritten one holds id = name), the expansion is
// built on the stack. The same exclusions as corLdPrefixExpand: a real URI scheme is
// not a prefix, nor is a suffix starting "//".
//
static CorLdItem* compactIriLookup(const char* name, const char* colonP)
{
  int prefixLen = colonP - name;

  if ((prefixLen == 4) && (strncmp(name, "http", 4) == 0))   return NULL;
  if ((prefixLen == 5) && (strncmp(name, "https", 5) == 0))  return NULL;
  if ((prefixLen == 3) && (strncmp(name, "urn", 3) == 0))    return NULL;

  if ((colonP[1] == '/') && (colonP[2] == '/'))
    return NULL;

  CorLdContext* pristineP = corLdCorePristine();

  if ((pristineP == NULL) || (pristineP->nameHT == NULL))
    return NULL;

  char prefix[256];

  if (prefixLen >= (int) sizeof(prefix))
    return NULL;

  memcpy(prefix, name, prefixLen);
  prefix[prefixLen] = 0;

  CorLdItem* prefixItemP = (CorLdItem*) corHashItemLookup(pristineP->nameHT, prefix);

  if ((prefixItemP == NULL) || (prefixItemP->id == NULL))
    return NULL;

  const char* suffix    = colonP + 1;
  int         idLen     = strlen(prefixItemP->id);
  int         suffixLen = strlen(suffix);
  char        iri[512];

  if (idLen + suffixLen >= (int) sizeof(iri))
    return NULL;   // longer than any core IRI - so not one

  memcpy(iri, prefixItemP->id, idLen);
  memcpy(&iri[idLen], suffix, suffixLen + 1);

  return corLdCoreItemByIri(iri);
}



// -----------------------------------------------------------------------------
//
// corLdCoreLookup -
//
CorLdItem* corLdCoreLookup(const char* name)
{
  if (name == NULL)
    return NULL;

  //
  // A JSON-LD keyword counts as expanded too (corLdAlreadyExpanded), exactly as in
  // corLdExpand: "@id" and "@type" are the IRIs of the core terms id and type, so
  // they resolve to those items; any other keyword resolves to nothing.
  //
  if (corLdAlreadyExpanded(name) == true)
    return corLdCoreItemByIri(name);

  const char* colonP = strchr(name, ':');

  if (colonP != NULL)
  {
    CorLdItem* itemP = compactIriLookup(name, colonP);

    if (itemP != NULL)
      return itemP;
  }

  return contextItemLookup(corLdCoreContext(), name);
}
