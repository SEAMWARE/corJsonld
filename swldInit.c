//
// FILE            swldInit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <stdlib.h>                                  // strdup, free
#include <string.h>                                  // memset, strcmp

#include <pthread.h>                                 // pthread_mutex_init, pthread_mutex_destroy

#include "ktrace/kTrace.h"                            // KT_E
#include "kjson/KjNode.h"                            // KjNode
#include "kjson/kjParse.h"                           // kjParse
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBufferCreate.h"                    // kjBufferCreate
#include "khash/khash.h"                             // KHashTable, KHashListItem
#include "swJsonld/swldTraceLevels.h"                // SwldTInit
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                    // SwldContext
#include "swJsonld/swldContextParse.h"               // swldContextFromObject
#include "swJsonld/swldExpand.h"                     // contextVocab
#include "swJsonld/SwldContextCache.h"               // SwldContextCache
#include "swJsonld/swldCache.h"                      // swldCacheInsert
#include "swJsonld/swldDownload.h"                   // swldContextFromUrl
#include "swJsonld/swldCoreContextBody.h"            // swldCoreContextBody
#include "swJsonld/swldInit.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// Global state
//
static SwldContextCache      swldGlobalCache;
static SwldContext*          swldCoreContextP     = NULL;
static bool                  swldInitialized      = false;
static SwldDownloadFunction  swldDownloadFn       = NULL;



// -----------------------------------------------------------------------------
//
// swldCoreContext -
//
SwldContext* swldCoreContext(void)
{
  return swldCoreContextP;
}



// -----------------------------------------------------------------------------
//
// swldCacheGet - internal, used by swldCache.c via extern
//
SwldContextCache* swldCacheGet(void)
{
  return &swldGlobalCache;
}



// -----------------------------------------------------------------------------
//
// swldDownloadGet - internal, used by swldDownload.c via extern
//
SwldDownloadFunction swldDownloadGet(void)
{
  return swldDownloadFn;
}



// -----------------------------------------------------------------------------
//
// coreContextRewriteToShort - rewrite each core item's id to its name
//
// The core-context shortcut keeps NGSI-LD core terms in their short form
// throughout the broker pipeline (parse, cache, DB, render). Done here at
// init time, exactly once: swldExpand naturally returns the short name for
// any core term thereafter, with no per-call branching.
//
// Items whose id is a JSON-LD keyword (e.g. "id" -> "@id", "type" -> "@type")
// must keep their id intact so the @-keyword bypass in expandObject and the
// @type-value branch continue to fire.
//
static void coreContextRewriteToShort(SwldContext* contextP)
{
  if (contextP == NULL)
    return;

  if (contextP->isArray == true)
  {
    for (int ix = 0; ix < contextP->contexts; ix++)
      coreContextRewriteToShort(contextP->contextV[ix]);
    return;
  }

  if (contextP->nameHT == NULL)
    return;

  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    for (KHashListItem* lP = contextP->nameHT->array[slot]; lP != NULL; lP = lP->next)
    {
      SwldItem* itemP = (SwldItem*) lP->data;

      if (itemP == NULL || itemP->id == NULL || itemP->name == NULL)
        continue;

      if (itemP->id[0] == '@')
        continue;

      itemP->id = itemP->name;
    }
  }
}



// -----------------------------------------------------------------------------
//
// coreContextFromEmbedded - parse the compiled-in core context body
//
static SwldContext* coreContextFromEmbedded(KAlloc* kaP)
{
  //
  // strdup because kjParse is destructive
  //
  char* body = strdup(swldCoreContextBody);

  if (body == NULL)
    return NULL;

  Kjson  kjson;
  Kjson* kjsonP = kjBufferCreate(&kjson, kaP);

  KjNode* treeP = kjParse(kjsonP, body);

  if (treeP == NULL)
  {
    free(body);
    return NULL;
  }

  KjNode* atContextP = kjLookup(treeP, "@context");

  if (atContextP == NULL)
  {
    free(body);
    return NULL;
  }

  SwldContext* contextP = swldContextFromObject(atContextP, kaP, SWLD_CORE_CONTEXT_URL);

  if (contextP != NULL)
  {
    //
    // Core-context shortcut: rewrite each item's id to its name so the
    // expander returns short forms for core terms with zero per-call work.
    //
    coreContextRewriteToShort(contextP);

    //
    // Preserve the compiled-in body for GET /jsonldContexts/{id}.
    // swldCoreContextBody lives for the process lifetime, so we point at
    // it directly rather than duplicating.
    //
    contextP->body = (char*) swldCoreContextBody;
    contextP->kind = SwldKindImplicit;
    swldCacheInsert(contextP);
  }

  free(body);
  return contextP;
}



// -----------------------------------------------------------------------------
//
// swldInit -
//
int swldInit(KAlloc* kaP, const char* coreContextUrl, SwldDownloadFunction downloadFn)
{
  if (swldInitialized == true)
    return 0;

  memset(&swldGlobalCache, 0, sizeof(swldGlobalCache));
  pthread_mutex_init(&swldGlobalCache.mutex, NULL);
  swldGlobalCache.maxEntries = 100;
  swldGlobalCache.kaP        = kaP;
  swldDownloadFn             = downloadFn;
  swldInitialized            = true;

  //
  // If no URL given, or it matches the default, use the embedded body
  //
  if (coreContextUrl == NULL || strcmp(coreContextUrl, SWLD_CORE_CONTEXT_URL) == 0)
  {
    swldCoreContextP = coreContextFromEmbedded(kaP);
    if (swldCoreContextP == NULL)
      return -1;
  }
  else
  {
    //
    // Non-default URL: download from network
    //
    swldCoreContextP = swldContextFromUrl(coreContextUrl, kaP);
    if (swldCoreContextP == NULL)
      return -1;

    coreContextRewriteToShort(swldCoreContextP);
  }

  //
  // The core context MUST declare @vocab — it's the unconditional fallback
  // for any short term no context defined explicitly (JSON-LD § 4.1.2).
  // Without it, swldExpand has no way to construct an IRI for unknown user
  // terms and would silently leak them downstream, corrupting cache / DB /
  // distop comparisons that all assume fully-expanded IRIs.
  //
  // Refuse to start rather than discover this on the Nth request after
  // some entity has already been persisted with bare names.
  //
  if (contextVocab(swldCoreContextP) == NULL)
  {
    KT_E("Core context has no @vocab member — broker cannot expand unknown user terms. Refusing to start.");
    return -1;
  }

  return 0;
}



// -----------------------------------------------------------------------------
//
// swldCleanup -
//
void swldCleanup(void)
{
  if (swldInitialized == false)
    return;

  pthread_mutex_destroy(&swldGlobalCache.mutex);
  memset(&swldGlobalCache, 0, sizeof(swldGlobalCache));
  swldCoreContextP = NULL;
  swldDownloadFn   = NULL;
  swldInitialized  = false;
}
