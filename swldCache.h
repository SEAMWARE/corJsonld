//
// FILE            swldCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_CACHE_H
#define SWLD_CACHE_H

#include <stdbool.h>                                 // bool

#include "kalloc/KAlloc.h"                            // KAlloc
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldCacheLookup -
//
extern SwldContext* swldCacheLookup(const char* url);



// -----------------------------------------------------------------------------
//
// swldCacheInsert -
//
extern void swldCacheInsert(SwldContext* contextP);



// -----------------------------------------------------------------------------
//
// swldCacheRemove - detach an entry by id or url (returns it, or NULL).
//
extern SwldContext* swldCacheRemove(const char* idOrUrl);



// -----------------------------------------------------------------------------
//
// swldCacheSnapshot - thread-safe snapshot of current cache entries.
//
// Allocates an array of (SwldContext*) pointers in allocP, in cache insertion
// order (head-first). The SwldContext objects themselves are not copied —
// callers must not mutate them. Returned pointers remain valid as long as
// the cache does not evict them (LRU eviction is possible if the cache fills
// up during a long response serialization). For short-lived GET handling
// this is not a real concern; if it becomes one, snapshot should deep-copy
// into caller storage.
//
// *arrPP is set to the array; *nP is set to the count. Both are zero on
// empty cache.
//
extern void swldCacheSnapshot(KAlloc* allocP, SwldContext*** arrPP, int* nP);

#endif
