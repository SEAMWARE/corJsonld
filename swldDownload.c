//
// FILE            swldDownload.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <stdlib.h>                                  // free
#include <string.h>                                  // strlen
#include <unistd.h>                                  // usleep

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "kjson/KjNode.h"                            // KjNode
#include "kjson/kjParse.h"                           // kjParse
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBufferCreate.h"                    // kjBufferCreate
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTDownload
#include "swJsonld/SwldContextCache.h"               // SwldContextCache (for swldCacheGet()->kaP)
#include "swJsonld/swldCache.h"                      // swldCacheLookup, swldCacheInsert
#include "swJsonld/swldContextParse.h"               // swldContextFromObject, swldContextFromTree
#include "swJsonld/swldInit.h"                       // SwldDownloadFunction
#include "swJsonld/swldDownload.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// swldDownloadGet - defined in swldInit.c
//
extern SwldDownloadFunction swldDownloadGet(void);



// -----------------------------------------------------------------------------
//
// swldCacheDownloadingAdd/Remove/Check - defined in swldCache.c
//
extern bool swldCacheDownloadingAdd(const char* url);
extern void swldCacheDownloadingRemove(const char* url);
extern bool swldCacheDownloadingCheck(const char* url);



// -----------------------------------------------------------------------------
//
// swldContextFromUrl -
//
SwldContext* swldContextFromUrl(const char* url, KAlloc* kaP)
{
  //
  // Cached contexts need to outlive the request — each request arena gets
  // reset/reused, so storing SwldContext pointers that reference request-
  // scoped memory leaves the cache with dangling fields. Use the
  // process-lifetime cache allocator for anything that will be inserted.
  // The caller's kaP is still used locally to build the parse tree (freed
  // when the arena resets; we only need it long enough to populate the
  // SwldContext).
  //
  extern SwldContextCache* swldCacheGet(void);
  KAlloc* storeP = swldCacheGet()->kaP;
  if (storeP == NULL)
    storeP = kaP;

  //
  // Step 1: Check cache
  //
  SwldContext* contextP = swldCacheLookup(url);

  if (contextP != NULL)
    return contextP;

  //
  // Step 2: Check if another thread is downloading this URL
  //
  if (swldCacheDownloadingAdd(url) == false)
  {
    //
    // Another thread is downloading - poll the cache
    //
    for (int tries = 0; tries < 150; tries++)
    {
      usleep(20000);  // 20ms

      contextP = swldCacheLookup(url);
      if (contextP != NULL)
        return contextP;
    }

    return NULL;
  }

  //
  // Step 3: Download via callback
  //
  SwldDownloadFunction downloadFn = swldDownloadGet();

  if (downloadFn == NULL)
  {
    swldCacheDownloadingRemove(url);
    return NULL;
  }

  int   statusCode = 0;
  char* body       = downloadFn(url, &statusCode);

  if (body == NULL || statusCode != 200)
  {
    if (body != NULL)
      free(body);
    swldCacheDownloadingRemove(url);
    return NULL;
  }

  //
  // Preserve the downloaded body for GET /jsonldContexts/{id}.
  // kjParse is destructive — capture a pristine copy in the long-lived
  // cache allocator before parsing.
  //
  char* bodyCopy = kaStrdup(storeP, body);

  //
  // Step 4: Parse the JSON body
  //
  // The parse tree is only needed while we build the SwldContext; the
  // caller's kaP (request arena) is fine for that.
  //
  Kjson  kjson;
  Kjson* kjsonP = kjBufferCreate(&kjson, kaP);

  KjNode* responseP = kjParse(kjsonP, body);

  if (responseP == NULL)
  {
    free(body);
    swldCacheDownloadingRemove(url);
    return NULL;
  }

  //
  // Step 5: Find @context in the parsed tree
  //
  KjNode* atContextP = kjLookup(responseP, "@context");

  if (atContextP == NULL)
  {
    free(body);
    swldCacheDownloadingRemove(url);
    return NULL;
  }

  //
  // Step 6: Build context from @context node
  //
  contextP = NULL;

  if (atContextP->type == KjObject)
  {
    contextP = swldContextFromObject(atContextP, storeP, url);
  }
  else if (atContextP->type == KjArray)
  {
    contextP = swldContextFromTree(atContextP, storeP);

    if (contextP != NULL)
      contextP->url = kaStrdup(storeP, url);
  }
  else if (atContextP->type == KjString)
  {
    //
    // Redirect: @context is a URL string - follow it
    //
    swldCacheDownloadingRemove(url);
    free(body);
    return swldContextFromUrl(atContextP->value.s, kaP);
  }

  //
  // Step 7: Cache the result
  //
  if (contextP != NULL)
  {
    if (contextP->url == NULL)
      contextP->url = kaStrdup(storeP, url);

    if (contextP->body == NULL)
      contextP->body = bodyCopy;

    contextP->kind = SwldKindImplicit;

    swldCacheInsert(contextP);
  }

  free(body);
  swldCacheDownloadingRemove(url);
  return contextP;
}
