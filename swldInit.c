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

#include "kjson/KjNode.h"                            // KjNode
#include "kjson/kjParse.h"                           // kjParse
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBufferCreate.h"                    // kjBufferCreate
#include "swJsonld/swldTraceLevels.h"                // SwldTInit
#include "swJsonld/swldContextParse.h"               // swldContextFromObject
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
    swldCacheInsert(contextP);

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

    return 0;
  }

  //
  // Non-default URL: download from network
  //
  swldCoreContextP = swldContextFromUrl(coreContextUrl, kaP);
  if (swldCoreContextP == NULL)
    return -1;

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
