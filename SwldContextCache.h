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
  int              downloadCount;
} SwldContextCache;

#endif
