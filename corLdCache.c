//
// FILE            corLdCache.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strcmp, strncmp
#include <time.h>                                    // time

#include "kalloc/KAlloc.h"                            // KAlloc, kaAlloc
#include "kalloc/kaAlloc.h"                           // kaAlloc

#include "corJsonld/CorLdContextCache.h"               // CorLdContextCache
#include "corJsonld/corLdTraceLevels.h"                // CorLdTCache
#include "corJsonld/corLdCache.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// corLdCacheGet - defined in corLdInit.c
//
extern CorLdContextCache* corLdCacheGet(void);



// -----------------------------------------------------------------------------
//
// schemeSkip - skip a leading http:// or https:// scheme
//
static const char* schemeSkip(const char* s)
{
  if      (strncmp(s, "https://", 8) == 0)  return &s[8];
  else if (strncmp(s, "http://",  7) == 0)  return &s[7];

  return s;
}



// -----------------------------------------------------------------------------
//
// idMatch - protocol-agnostic identifier comparison
//
// A JSON-LD @context referenced as http://host/x and as https://host/x is the
// SAME context; the cache keys it without regard to the scheme so only ONE copy
// is ever stored (the first-seen URL wins). Non-URL identifiers (broker-assigned
// Hosted ids, urn:...) carry no http(s) scheme and so compare verbatim.
//
static bool idMatch(const char* a, const char* b)
{
  if ((a == NULL) || (b == NULL))
    return false;

  if (strcmp(a, b) == 0)
    return true;

  return (strcmp(schemeSkip(a), schemeSkip(b)) == 0);
}



// -----------------------------------------------------------------------------
//
// corLdCacheLookup - lookup by identifier.
//
// For Implicit / Cached contexts the id equals the URL. For Hosted contexts
// the id is broker-assigned. All callers use the same entry point.
//
CorLdContext* corLdCacheLookup(const char* idOrUrl)
{
  CorLdContextCache* cacheP = corLdCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  CorLdContext* contextP = cacheP->first;

  while (contextP != NULL)
  {
    if (idMatch(contextP->id, idOrUrl) || idMatch(contextP->url, idOrUrl))
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
// corLdCacheInsert -
//
// Hosted contexts have no URL (only a broker-assigned id); downloaded ones
// have both and by convention id == url. Either key is acceptable so long as
// at least one is set.
//
void corLdCacheInsert(CorLdContext* contextP)
{
  if (contextP == NULL || (contextP->url == NULL && contextP->id == NULL))
    return;

  CorLdContextCache* cacheP = corLdCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  //
  // Check for duplicate — match on id if available, fall back to url.
  //
  const char* key = (contextP->id != NULL) ? contextP->id : contextP->url;

  CorLdContext* existingP = cacheP->first;

  while (existingP != NULL)
  {
    if (idMatch(existingP->id, key) || idMatch(existingP->url, key))
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
    CorLdContext*  lruP     = cacheP->first;
    CorLdContext*  lruPrevP = NULL;
    CorLdContext*  prevP    = NULL;
    CorLdContext*  nodeP    = cacheP->first;

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
// corLdCacheRemove - detach an entry from the cache by id or url.
//
// Returns the detached CorLdContext, or NULL if not found. The caller owns
// the returned entry — the cache will not touch it again. Because
// CorLdContexts live in the cache's long-lived allocator and that allocator
// has no per-object free, we don't free memory here; the caller can reuse
// or abandon it (small leak, bounded by number of DELETEs per process).
//
CorLdContext* corLdCacheRemove(const char* idOrUrl)
{
  if (idOrUrl == NULL)
    return NULL;

  CorLdContextCache* cacheP = corLdCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  CorLdContext*  prevP = NULL;
  CorLdContext*  p     = cacheP->first;

  while (p != NULL)
  {
    if (idMatch(p->id, idOrUrl) || idMatch(p->url, idOrUrl))
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
// corLdCacheReapVolatile - drop volatile contexts whose expiresAt has passed.
//
// Volatile contexts (broker-minted, one-shot Link targets) are normally
// removed on their first GET. This is the backstop for the ones nobody ever
// fetched: walk the list under the lock and unlink any volatile entry past
// its deadline. Like corLdCacheRemove, no per-object free (cache allocator).
// Returns the number reaped.
//
int corLdCacheReapVolatile(double now)
{
  CorLdContextCache* cacheP = corLdCacheGet();
  int               reaped = 0;

  pthread_mutex_lock(&cacheP->mutex);

  CorLdContext* prevP = NULL;
  CorLdContext* p     = cacheP->first;

  while (p != NULL)
  {
    if (p->volatileCtx && p->expiresAt != 0 && p->expiresAt <= now)
    {
      CorLdContext* deadP = p;

      if (prevP != NULL)
        prevP->next = p->next;
      else
        cacheP->first = p->next;

      cacheP->count -= 1;
      p = p->next;
      deadP->next = NULL;
      reaped++;
      continue;
    }

    prevP = p;
    p     = p->next;
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return reaped;
}



// -----------------------------------------------------------------------------
//
// corLdCacheSnapshot -
//
void corLdCacheSnapshot(KAlloc* allocP, CorLdContext*** arrPP, int* nP)
{
  CorLdContextCache* cacheP = corLdCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  int n = cacheP->count;

  if (n == 0)
  {
    *arrPP = NULL;
    *nP    = 0;
    pthread_mutex_unlock(&cacheP->mutex);
    return;
  }

  CorLdContext** arr = (CorLdContext**) kaAlloc(allocP, n * sizeof(CorLdContext*));
  if (arr == NULL)
  {
    *arrPP = NULL;
    *nP    = 0;
    pthread_mutex_unlock(&cacheP->mutex);
    return;
  }

  int ix = 0;
  for (CorLdContext* p = cacheP->first; p != NULL && ix < n; p = p->next)
    arr[ix++] = p;

  *arrPP = arr;
  *nP    = ix;

  pthread_mutex_unlock(&cacheP->mutex);
}



// -----------------------------------------------------------------------------
//
// corLdCacheDownloadingAdd - claim the URL for downloading, or say who else has it
//
// Returns:
//   CORLD_DOWNLOAD_MINE    - nobody was downloading it; the caller downloads it
//   CORLD_DOWNLOAD_OTHER   - another thread is downloading it; the caller waits
//   CORLD_DOWNLOAD_CYCLE   - THIS thread is already downloading it: the @context
//                           references itself, directly or through a chain, and
//                           the download that would end the wait is the very call
//                           that is now asking. Waiting would burn the full
//                           deadline and fail anyway.
//
// The lookup and the add are one critical section on purpose: two threads that
// both looked first and then added would both download.
//
int corLdCacheDownloadingAdd(const char* url)
{
  CorLdContextCache* cacheP = corLdCacheGet();
  pthread_t         me     = pthread_self();

  pthread_mutex_lock(&cacheP->mutex);

  for (int ix = 0; ix < cacheP->downloadCount; ix++)
  {
    if (strcmp(cacheP->downloading[ix], url) == 0)
    {
      int result = (pthread_equal(cacheP->downloadOwner[ix], me) != 0)? CORLD_DOWNLOAD_CYCLE : CORLD_DOWNLOAD_OTHER;

      pthread_mutex_unlock(&cacheP->mutex);
      return result;
    }
  }

  //
  // A full list means more than CORLD_MAX_DOWNLOADING downloads are in flight at
  // once. The caller is told to go ahead: an untracked download costs at worst a
  // duplicate fetch, while answering "somebody else has it" would make it wait
  // for a download nobody is doing.
  //
  if (cacheP->downloadCount < CORLD_MAX_DOWNLOADING)
  {
    cacheP->downloading[cacheP->downloadCount]   = (char*) url;
    cacheP->downloadOwner[cacheP->downloadCount] = me;
    cacheP->downloadCount += 1;
  }

  pthread_mutex_unlock(&cacheP->mutex);
  return CORLD_DOWNLOAD_MINE;
}



// -----------------------------------------------------------------------------
//
// corLdCacheDownloadingRemove -
//
void corLdCacheDownloadingRemove(const char* url)
{
  CorLdContextCache* cacheP = corLdCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  for (int ix = 0; ix < cacheP->downloadCount; ix++)
  {
    if (strcmp(cacheP->downloading[ix], url) == 0)
    {
      //
      // The owner moves with its URL: compacting one array and not the other
      // would hand an entry somebody else's owner, and the cycle check would
      // then answer for the wrong thread in both directions.
      //
      for (int jx = ix; jx < cacheP->downloadCount - 1; jx++)
      {
        cacheP->downloading[jx]   = cacheP->downloading[jx + 1];
        cacheP->downloadOwner[jx] = cacheP->downloadOwner[jx + 1];
      }

      cacheP->downloadCount -= 1;
      break;
    }
  }

  pthread_mutex_unlock(&cacheP->mutex);
}



// -----------------------------------------------------------------------------
//
// corLdCacheDownloadingCheck - returns true if the URL is being downloaded
//
bool corLdCacheDownloadingCheck(const char* url)
{
  CorLdContextCache* cacheP = corLdCacheGet();

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
