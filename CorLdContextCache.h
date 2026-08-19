//
// FILE            CorLdContextCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_CONTEXT_CACHE_H
#define CORLD_CONTEXT_CACHE_H

#include <pthread.h>                                 // pthread_mutex_t

#include "kalloc/KAlloc.h"                           // KAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// CORLD_MAX_DOWNLOADING -
//
#define CORLD_MAX_DOWNLOADING  16



// -----------------------------------------------------------------------------
//
// CORLD_DOWNLOAD_* - what corLdCacheDownloadingAdd found (see corLdCache.c)
//
#define CORLD_DOWNLOAD_MINE   0   // nobody had it - this thread downloads it
#define CORLD_DOWNLOAD_OTHER  1   // another thread is downloading it - wait for it
#define CORLD_DOWNLOAD_CYCLE  2   // THIS thread is downloading it - a cyclic @context



// -----------------------------------------------------------------------------
//
// CorLdContextCache -
//
typedef struct CorLdContextCache
{
  CorLdContext*     first;
  int              count;
  int              maxEntries;
  pthread_mutex_t  mutex;
  KAlloc*          kaP;
  char*            downloading[CORLD_MAX_DOWNLOADING];
  // The thread that put each URL in 'downloading'. A thread that finds its OWN
  // url there has recursed back into a download it is itself in the middle of -
  // a cyclic @context - and waiting for it would be waiting for itself.
  pthread_t        downloadOwner[CORLD_MAX_DOWNLOADING];
  int              downloadCount;
} CorLdContextCache;

#endif
