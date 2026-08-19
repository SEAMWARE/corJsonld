//
// FILE            corLdDownload.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <stdlib.h>                                  // free
#include <string.h>                                  // strlen, memset
#include <unistd.h>                                  // usleep

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "kjson/KjNode.h"                            // KjNode
#include "kjson/kjParse.h"                           // kjParse
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBufferCreate.h"                    // kjBufferCreate
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTDownload
#include "corJsonld/CorLdContextCache.h"               // CorLdContextCache (for corLdCacheGet()->kaP)
#include "corJsonld/corLdCache.h"                      // corLdCacheLookup, corLdCacheInsert
#include "corJsonld/corLdContextParse.h"               // corLdContextFromObject, corLdContextFromTree
#include "corJsonld/corLdUrlResolve.h"                 // corLdUrlResolve
#include "corJsonld/corLdInit.h"                       // CorLdDownloadFunction, CORLD_CORE_CONTEXT_URL
#include "corJsonld/corLdDownload.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// isOlderNgsildCoreUrl -
//
// True when `url` matches a known NGSI-LD core context URL that is NOT
// the broker's configured core. Older core versions (v1.0–v1.8) are
// recognised by URL prefix; the broker treats them as no-ops for
// expansion while still caching the body for GET /jsonldContexts/{id}.
//
static bool isOlderNgsildCoreUrl(const char* url)
{
  if (url == NULL)
    return false;

  // The broker's configured core runs through the normal path (it gets
  // coreContextRewriteToShort applied at corLdInit time).
  if (strcmp(url, CORLD_CORE_CONTEXT_URL) == 0)
    return false;

  // Canonical etsi.org core URLs accepted as the older-core no-op:
  //   https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context.jsonld         (unversioned)
  //   https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v<X>.<Y>.jsonld
  //
  // The earlier `*` wildcard let `ngsi-ld-core-context-non-existing.jsonld`
  // (used by ETSI 043_01 to verify LdContextNotAvailable) slip through and
  // get cached as an empty no-op core, so the request silently succeeded
  // instead of returning 504. Stricter check: require either a clean
  // ".jsonld" right after the prefix, or "-v<digit>+.<digit>+.jsonld".
  static const char* kPrefix = "https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context";
  size_t prefixLen = strlen(kPrefix);

  if (strncmp(url, kPrefix, prefixLen) != 0)
    return false;

  const char* tail = url + prefixLen;
  if (strcmp(tail, ".jsonld") == 0)
    return true;

  if (tail[0] != '-' || tail[1] != 'v')
    return false;
  tail += 2;
  if (*tail < '0' || *tail > '9')
    return false;
  while (*tail >= '0' && *tail <= '9') tail++;
  if (*tail != '.')
    return false;
  tail++;
  if (*tail < '0' || *tail > '9')
    return false;
  while (*tail >= '0' && *tail <= '9') tail++;
  return strcmp(tail, ".jsonld") == 0;
}



// -----------------------------------------------------------------------------
//
// corLdIsCoreContextUrl - see header.
//
bool corLdIsCoreContextUrl(const char* url)
{
  if (url == NULL)
    return false;

  CorLdContext* coreP = corLdCoreContext();
  if (coreP != NULL && coreP->url != NULL && strcmp(url, coreP->url) == 0)
    return true;

  if (strcmp(url, CORLD_CORE_CONTEXT_URL) == 0)
    return true;

  return isOlderNgsildCoreUrl(url);
}



// -----------------------------------------------------------------------------
//
// corLdDownloadGet - defined in corLdInit.c
//
extern CorLdDownloadFunction corLdDownloadGet(void);
extern CorLdErrorFunction    corLdErrorGet(void);



// -----------------------------------------------------------------------------
//
// corLdCacheDownloadingAdd/Remove/Check - defined in corLdCache.c
//
extern int  corLdCacheDownloadingAdd(const char* url);
extern void corLdCacheDownloadingRemove(const char* url);
extern bool corLdCacheDownloadingCheck(const char* url);



// -----------------------------------------------------------------------------
//
// corLdContextFromUrl -
//
CorLdContext* corLdContextFromUrl(const char* url, KAlloc* kaP)
{
  //
  // Cached contexts need to outlive the request — each request arena gets
  // reset/reused, so storing CorLdContext pointers that reference request-
  // scoped memory leaves the cache with dangling fields. Use the
  // process-lifetime cache allocator for anything that will be inserted.
  // The caller's kaP is still used locally to build the parse tree (freed
  // when the arena resets; we only need it long enough to populate the
  // CorLdContext).
  //
  extern CorLdContextCache* corLdCacheGet(void);
  KAlloc* storeP = corLdCacheGet()->kaP;
  if (storeP == NULL)
    storeP = kaP;

  //
  // Step 1: Check cache
  //
  CorLdContext* contextP = corLdCacheLookup(url);

  if (contextP != NULL)
    return contextP;

  //
  // Older NGSI-LD core context URL (v1.0–v1.8 / unversioned) — do NOT
  // download. The broker runs its own embedded core; older variants
  // would otherwise fight it for ownership of "value" / "object" / etc.
  // and break the canonical short-form invariant. Insert a stub
  // CorLdContext flagged as `ignored`, with no body — `GET
  // /jsonldContexts/{id}` will still find it (the URL is what
  // identifies it), expansion / compaction skip it.
  //
  if (isOlderNgsildCoreUrl(url))
  {
    contextP        = (CorLdContext*) kaAlloc(storeP, sizeof(CorLdContext));
    if (contextP == NULL)
      return NULL;
    memset(contextP, 0, sizeof(CorLdContext));
    contextP->url     = kaStrdup(storeP, url);
    contextP->id      = contextP->url;
    contextP->kind    = CorLdKindImplicit;
    contextP->ignored = true;
    corLdCacheInsert(contextP);
    return contextP;
  }

  //
  // Step 2: Check if another thread is downloading this URL
  //
  int downloadState = corLdCacheDownloadingAdd(url);

  //
  // A cyclic @context - this very thread is already downloading this URL, and has
  // recursed back into it through the members of an array @context. There is
  // nothing to wait for: the download that would end the wait is the call that is
  // now asking. Waiting anyway costs the full deadline (three seconds of a request
  // thread) and then fails just the same.
  //
  if (downloadState == CORLD_DOWNLOAD_CYCLE)
  {
    //
    // Reported as 400 and not as "could not be retrieved": this @context WAS
    // retrieved, and so was the one that references it back - they are simply
    // not usable together. JSON-LD 1.1 keeps the two apart the same way (a
    // 'remote contexts' array to detect cyclical inclusions, distinct from
    // 'loading remote context failed'), and only one of the two answers tells
    // the client something it can act on.
    //
    CorLdErrorFunction errorFn = corLdErrorGet();

    if (errorFn != NULL)
      errorFn(400, "Cyclic @context", url);

    return NULL;
  }

  if (downloadState == CORLD_DOWNLOAD_OTHER)
  {
    //
    // Another thread is downloading it - poll the cache until it lands.
    //
    for (int tries = 0; tries < 150; tries++)
    {
      usleep(20000);  // 20ms

      contextP = corLdCacheLookup(url);
      if (contextP != NULL)
        return contextP;
    }

    return NULL;
  }

  //
  // Step 3: Download via callback
  //
  CorLdDownloadFunction downloadFn = corLdDownloadGet();

  if (downloadFn == NULL)
  {
    corLdCacheDownloadingRemove(url);
    return NULL;
  }

  int   statusCode = 0;
  char* body       = downloadFn(url, &statusCode);

  if (body == NULL || statusCode != 200)
  {
    if (body != NULL)
      free(body);
    corLdCacheDownloadingRemove(url);
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
  // The parse tree is only needed while we build the CorLdContext; the
  // caller's kaP (request arena) is fine for that.
  //
  Kjson  kjson;
  Kjson* kjsonP = kjBufferCreate(&kjson, kaP);

  KjNode* responseP = kjParse(kjsonP, body);

  if (responseP == NULL)
  {
    free(body);
    corLdCacheDownloadingRemove(url);
    return NULL;
  }

  //
  // Step 5: Find @context in the parsed tree
  //
  KjNode* atContextP = kjLookup(responseP, "@context");

  if (atContextP == NULL)
  {
    free(body);
    corLdCacheDownloadingRemove(url);
    return NULL;
  }

  //
  // Step 6: Build context from @context node
  //
  contextP = NULL;

  if (atContextP->type == KjObject)
  {
    contextP = corLdContextFromObject(atContextP, storeP, url);
  }
  else if (atContextP->type == KjArray)
  {
    contextP = corLdContextFromTree(atContextP, storeP, url);

    if (contextP != NULL)
      contextP->url = kaStrdup(storeP, url);
  }
  else if (atContextP->type == KjString)
  {
    //
    // Redirect: @context is a URL string - follow it
    //
    corLdCacheDownloadingRemove(url);

    //
    // The string is an IRI reference and may be relative - it resolves against the URL this very
    // @context was downloaded from
    //
    const char* redirectUrl = corLdUrlResolve(url, atContextP->value.s, kaP);

    free(body);
    return corLdContextFromUrl(redirectUrl, kaP);
  }

  //
  // Step 7: Cache the result
  //
  // A context downloaded via URL is Cached (§ 5.13.1: "contexts that
  // have been cached as a side effect of processing API operations").
  // ImplicitlyCreated is reserved for inline-object @contexts the
  // broker wraps and assigns its own URL — those flows set kind on
  // their own.
  //
  if (contextP != NULL)
  {
    if (contextP->url == NULL)
      contextP->url = kaStrdup(storeP, url);

    if (contextP->body == NULL)
      contextP->body = bodyCopy;

    contextP->kind = CorLdKindCached;

    corLdCacheInsert(contextP);
  }

  free(body);
  corLdCacheDownloadingRemove(url);
  return contextP;
}
