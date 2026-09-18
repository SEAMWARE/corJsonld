//
// FILE            corLdPrefixExpand.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                  // strchr, strncmp, strlen, memcpy

#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "khash/khash.h"                             // khashItemLookup
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdInit.h"                        // corLdCorePristine
#include "corJsonld/corLdTraceLevels.h"                // CorLdTPrefix
#include "corJsonld/corLdPrefixExpand.h"               // Own interface



// -----------------------------------------------------------------------------
//
// corLdPrefixExpand -
//
char* corLdPrefixExpand(CorLdContext* contextP, const char* name, KAlloc* kaP)
{
  if (contextP == NULL || name == NULL)
    return NULL;

  char* colonP = (char*) strchr(name, ':');

  if (colonP == NULL)
    return NULL;

  int prefixLen = colonP - name;

  //
  // Skip real URI schemes: http:, https:, urn:
  //
  if ((prefixLen == 4) && (strncmp(name, "http", 4) == 0))
    return NULL;
  if ((prefixLen == 5) && (strncmp(name, "https", 5) == 0))
    return NULL;
  if ((prefixLen == 3) && (strncmp(name, "urn", 3) == 0))
    return NULL;

  //
  // Skip if suffix starts with "//" (bare scheme)
  //
  if ((colonP[1] == '/') && (colonP[2] == '/'))
    return NULL;

  //
  // Extract prefix into a local buffer
  //
  char prefix[256];

  if (prefixLen >= (int) sizeof(prefix))
    return NULL;

  memcpy(prefix, name, prefixLen);
  prefix[prefixLen] = 0;

  const char* suffix = colonP + 1;

  //
  // Lookup prefix in context (handle arrays: last-to-first)
  //
  CorLdItem* prefixItemP = NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      if (contextP->contextV[ix] != NULL && contextP->contextV[ix]->nameHT != NULL)
      {
        prefixItemP = (CorLdItem*) khashItemLookup(contextP->contextV[ix]->nameHT, prefix);
        if (prefixItemP != NULL)
          break;
      }
    }
  }
  else if (contextP->nameHT != NULL)
  {
    prefixItemP = (CorLdItem*) khashItemLookup(contextP->nameHT, prefix);
  }

  if (prefixItemP == NULL)
    return NULL;

  //
  // The prefix's IRI - but NOT straight off prefixItemP->id when the match came
  // from the core context.
  //
  // coreContextRewriteToShort flattens every core item's id to its name, so the
  // core prefix `ngsi-ld` no longer holds "https://uri.etsi.org/ngsi-ld/" - it
  // holds "ngsi-ld". Concatenating that with the suffix ate the colon and glued
  // the prefix NAME to it: `ngsi-ld:speed` was stored as an attribute called
  // `ngsi-ldspeed`, with a 201 and no error.
  //
  // The pristine core context (corLdCorePristine) still has the real ids, so
  // this is a plain O(1) lookup by prefix name. A real prefix IRI ends in '/',
  // '#' or ':', so an id that does not is either flattened or not a prefix at
  // all - and only a name the CORE defines is diverted, leaving a user term
  // legitimately used as a prefix untouched.
  //
  const char* prefixIri = prefixItemP->id;

  {
    int         iriLen = strlen(prefixIri);
    const char  last   = (iriLen > 0) ? prefixIri[iriLen - 1] : 0;

    if ((last != '/') && (last != '#') && (last != ':'))
    {
      CorLdContext* pristineP = corLdCorePristine();

      if ((pristineP != NULL) && (pristineP->nameHT != NULL))
      {
        CorLdItem* cleanP = (CorLdItem*) khashItemLookup(pristineP->nameHT, prefix);

        if ((cleanP != NULL) && (cleanP->id != NULL))
          prefixIri = cleanP->id;
      }
    }
  }

  //
  // Build expanded: prefix IRI + suffix
  //
  int idLen     = strlen(prefixIri);
  int suffixLen = strlen(suffix);
  char* result  = (char*) kaAlloc(kaP, idLen + suffixLen + 1);

  if (result == NULL)
    return NULL;

  memcpy(result, prefixIri, idLen);
  memcpy(result + idLen, suffix, suffixLen + 1);

  return result;
}
