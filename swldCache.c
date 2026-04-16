//
// FILE            swldCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strcmp
#include <time.h>                                    // time

#include "kalloc/KAlloc.h"                            // KAlloc, kaAlloc
#include "kalloc/kaAlloc.h"                           // kaAlloc

#include "swJsonld/SwldContextCache.h"               // SwldContextCache
#include "swJsonld/swldTraceLevels.h"                // SwldTCache
#include "swJsonld/swldCache.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// swldCacheGet - defined in swldInit.c
//
extern SwldContextCache* swldCacheGet(void);



// -----------------------------------------------------------------------------
//
// swldCacheLookup - lookup by identifier.
//
// For Implicit / Cached contexts the id equals the URL. For Hosted contexts
// the id is broker-assigned. All callers use the same entry point.
//
SwldContext* swldCacheLookup(const char* idOrUrl)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  SwldContext* contextP = cacheP->first;

  while (contextP != NULL)
  {
    if (((contextP->id  != NULL) && (strcmp(contextP->id,  idOrUrl) == 0)) ||
        ((contextP->url != NULL) && (strcmp(contextP->url, idOrUrl) == 0)))
    {
      contextP->usedAt = (double) time(NULL);
      pthread_mutex_unlock(&cacheP->mutex);
      return contextP;
    }

    contextP = contextP->next;
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// swldCacheInsert -
//
// Hosted contexts have no URL (only a broker-assigned id); downloaded ones
// have both and by convention id == url. Either key is acceptable so long as
// at least one is set.
//
void swldCacheInsert(SwldContext* contextP)
{
  if (contextP == NULL || (contextP->url == NULL && contextP->id == NULL))
    return;

  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  //
  // Check for duplicate — match on id if available, fall back to url.
  //
  const char* key = (contextP->id != NULL) ? contextP->id : contextP->url;

  SwldContext* existingP = cacheP->first;

  while (existingP != NULL)
  {
    if (((existingP->id  != NULL) && (strcmp(existingP->id,  key) == 0)) ||
        ((existingP->url != NULL) && (strcmp(existingP->url, key) == 0)))
    {
      pthread_mutex_unlock(&cacheP->mutex);
      return;
    }

    existingP = existingP->next;
  }

  //
  // Evict LRU if cache is full
  //
  if (cacheP->count >= cacheP->maxEntries)
  {
    SwldContext*  lruP     = cacheP->first;
    SwldContext*  lruPrevP = NULL;
    SwldContext*  prevP    = NULL;
    SwldContext*  nodeP    = cacheP->first;

    while (nodeP != NULL)
    {
      if (nodeP->usedAt < lruP->usedAt)
      {
        lruP     = nodeP;
        lruPrevP = prevP;
      }

      prevP = nodeP;
      nodeP = nodeP->next;
    }

    if (lruPrevP != NULL)
      lruPrevP->next = lruP->next;
    else
      cacheP->first = lruP->next;

    cacheP->count -= 1;
  }

  //
  // Insert at head
  //
  contextP->next      = cacheP->first;
  contextP->createdAt = (double) time(NULL);
  contextP->usedAt    = contextP->createdAt;
  cacheP->first       = contextP;
  cacheP->count      += 1;

  pthread_mutex_unlock(&cacheP->mutex);
}



// -----------------------------------------------------------------------------
//
// swldCacheRemove - detach an entry from the cache by id or url.
//
// Returns the detached SwldContext, or NULL if not found. The caller owns
// the returned entry — the cache will not touch it again. Because
// SwldContexts live in the cache's long-lived allocator and that allocator
// has no per-object free, we don't free memory here; the caller can reuse
// or abandon it (small leak, bounded by number of DELETEs per process).
//
SwldContext* swldCacheRemove(const char* idOrUrl)
{
  if (idOrUrl == NULL)
    return NULL;

  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  SwldContext*  prevP = NULL;
  SwldContext*  p     = cacheP->first;

  while (p != NULL)
  {
    if (((p->id  != NULL) && (strcmp(p->id,  idOrUrl) == 0)) ||
        ((p->url != NULL) && (strcmp(p->url, idOrUrl) == 0)))
    {
      if (prevP != NULL)
        prevP->next = p->next;
      else
        cacheP->first = p->next;

      cacheP->count -= 1;
      p->next = NULL;

      pthread_mutex_unlock(&cacheP->mutex);
      return p;
    }

    prevP = p;
    p     = p->next;
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// swldCacheSnapshot -
//
void swldCacheSnapshot(KAlloc* allocP, SwldContext*** arrPP, int* nP)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  int n = cacheP->count;

  if (n == 0)
  {
    *arrPP = NULL;
    *nP    = 0;
    pthread_mutex_unlock(&cacheP->mutex);
    return;
  }

  SwldContext** arr = (SwldContext**) kaAlloc(allocP, n * sizeof(SwldContext*));
  if (arr == NULL)
  {
    *arrPP = NULL;
    *nP    = 0;
    pthread_mutex_unlock(&cacheP->mutex);
    return;
  }

  int ix = 0;
  for (SwldContext* p = cacheP->first; p != NULL && ix < n; p = p->next)
    arr[ix++] = p;

  *arrPP = arr;
  *nP    = ix;

  pthread_mutex_unlock(&cacheP->mutex);
}



// -----------------------------------------------------------------------------
//
// swldCacheDownloadingAdd - returns true if URL was added, false if already downloading
//
bool swldCacheDownloadingAdd(const char* url)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  for (int ix = 0; ix < cacheP->downloadCount; ix++)
  {
    if (strcmp(cacheP->downloading[ix], url) == 0)
    {
      pthread_mutex_unlock(&cacheP->mutex);
      return false;
    }
  }

  if (cacheP->downloadCount < SWLD_MAX_DOWNLOADING)
  {
    cacheP->downloading[cacheP->downloadCount] = (char*) url;
    cacheP->downloadCount += 1;
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return true;
}



// -----------------------------------------------------------------------------
//
// swldCacheDownloadingRemove -
//
void swldCacheDownloadingRemove(const char* url)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  for (int ix = 0; ix < cacheP->downloadCount; ix++)
  {
    if (strcmp(cacheP->downloading[ix], url) == 0)
    {
      for (int jx = ix; jx < cacheP->downloadCount - 1; jx++)
        cacheP->downloading[jx] = cacheP->downloading[jx + 1];

      cacheP->downloadCount -= 1;
      break;
    }
  }

  pthread_mutex_unlock(&cacheP->mutex);
}



// -----------------------------------------------------------------------------
//
// swldCacheDownloadingCheck - returns true if the URL is being downloaded
//
bool swldCacheDownloadingCheck(const char* url)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  for (int ix = 0; ix < cacheP->downloadCount; ix++)
  {
    if (strcmp(cacheP->downloading[ix], url) == 0)
    {
      pthread_mutex_unlock(&cacheP->mutex);
      return true;
    }
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return false;
}
