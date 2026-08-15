//
// FILE            SwldContextCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_CONTEXT_CACHE_H
#define SWLD_CONTEXT_CACHE_H

#include <pthread.h>                                 // pthread_mutex_t

#include "kalloc/KAlloc.h"                           // KAlloc
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// SWLD_MAX_DOWNLOADING -
//
#define SWLD_MAX_DOWNLOADING  16



// -----------------------------------------------------------------------------
//
// SWLD_DOWNLOAD_* - what swldCacheDownloadingAdd found (see swldCache.c)
//
#define SWLD_DOWNLOAD_MINE   0   // nobody had it - this thread downloads it
#define SWLD_DOWNLOAD_OTHER  1   // another thread is downloading it - wait for it
#define SWLD_DOWNLOAD_CYCLE  2   // THIS thread is downloading it - a cyclic @context



// -----------------------------------------------------------------------------
//
// SwldContextCache -
//
typedef struct SwldContextCache
{
  SwldContext*     first;
  int              count;
  int              maxEntries;
  pthread_mutex_t  mutex;
  KAlloc*          kaP;
  char*            downloading[SWLD_MAX_DOWNLOADING];
  // The thread that put each URL in 'downloading'. A thread that finds its OWN
  // url there has recursed back into a download it is itself in the middle of -
  // a cyclic @context - and waiting for it would be waiting for itself.
  pthread_t        downloadOwner[SWLD_MAX_DOWNLOADING];
  int              downloadCount;
} SwldContextCache;

#endif
