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
#include "corJsonld/corLdInit.h"                        // corLdCorePrefixes
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
  // the prefix NAME to it: `ngsi-ld:Property` became `ngsi-ldProperty`, and
  // `ngsi-ld:speed` was stored as an attribute called `ngsi-ldspeed` with a 201.
  // Silent corruption of any compact IRI built on a core prefix, and there are
  // two of them (ngsi-ld, geojson).
  //
  // corLdCorePrefixes() is the pre-rewrite snapshot, kept for precisely this
  // reason (corLdCompact's prefix step already uses it). A real prefix IRI ends
  // in '/', '#' or ':' - which is the snapshot's own admission test - so an id
  // that does not is either flattened or not a prefix at all. Only a name the
  // snapshot knows is diverted, so a user term legitimately used as a prefix is
  // untouched.
  //
  const char* prefixIri = prefixItemP->id;

  {
    int         iriLen = strlen(prefixIri);
    const char  last   = (iriLen > 0) ? prefixIri[iriLen - 1] : 0;

    if ((last != '/') && (last != '#') && (last != ':'))
    {
      int                     coreN = 0;
      const CorLdCorePrefix*  coreV = corLdCorePrefixes(&coreN);

      for (int ix = 0; ix < coreN; ix++)
      {
        if (strcmp(coreV[ix].name, prefix) == 0)
        {
          prefixIri = coreV[ix].id;
          break;
        }
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
