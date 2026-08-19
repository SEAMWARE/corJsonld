//
// FILE            corLdPrefixExpand.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                  // strchr, strncmp, strlen, memcpy

#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "khash/khash.h"                             // khashItemLookup
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
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
  // Build expanded: prefixItem->id + suffix
  //
  int idLen     = strlen(prefixItemP->id);
  int suffixLen = strlen(suffix);
  char* result  = (char*) kaAlloc(kaP, idLen + suffixLen + 1);

  if (result == NULL)
    return NULL;

  memcpy(result, prefixItemP->id, idLen);
  memcpy(result + idLen, suffix, suffixLen + 1);

  return result;
}
