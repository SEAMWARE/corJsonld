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
// swldCacheLookup -
//
SwldContext* swldCacheLookup(const char* url)
{
  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  SwldContext* contextP = cacheP->first;

  while (contextP != NULL)
  {
    if ((contextP->url != NULL) && (strcmp(contextP->url, url) == 0))
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
void swldCacheInsert(SwldContext* contextP)
{
  if (contextP == NULL || contextP->url == NULL)
    return;

  SwldContextCache* cacheP = swldCacheGet();

  pthread_mutex_lock(&cacheP->mutex);

  //
  // Check for duplicate
  //
  SwldContext* existingP = cacheP->first;

  while (existingP != NULL)
  {
    if ((existingP->url != NULL) && (strcmp(existingP->url, contextP->url) == 0))
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
