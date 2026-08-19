//
// FILE            corLdCache.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_CACHE_H
#define CORLD_CACHE_H

#include <stdbool.h>                                 // bool

#include "kalloc/KAlloc.h"                            // KAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdCacheLookup -
//
extern CorLdContext* corLdCacheLookup(const char* url);



// -----------------------------------------------------------------------------
//
// corLdCacheInsert -
//
extern void corLdCacheInsert(CorLdContext* contextP);



// -----------------------------------------------------------------------------
//
// corLdCacheRemove - detach an entry by id or url (returns it, or NULL).
//
extern CorLdContext* corLdCacheRemove(const char* idOrUrl);



// -----------------------------------------------------------------------------
//
// corLdCacheReapVolatile - drop volatile contexts past expiresAt (returns count).
//
extern int corLdCacheReapVolatile(double now);



// -----------------------------------------------------------------------------
//
// corLdCacheSnapshot - thread-safe snapshot of current cache entries.
//
// Allocates an array of (CorLdContext*) pointers in allocP, in cache insertion
// order (head-first). The CorLdContext objects themselves are not copied —
// callers must not mutate them. Returned pointers remain valid as long as
// the cache does not evict them (LRU eviction is possible if the cache fills
// up during a long response serialization). For short-lived GET handling
// this is not a real concern; if it becomes one, snapshot should deep-copy
// into caller storage.
//
// *arrPP is set to the array; *nP is set to the count. Both are zero on
// empty cache.
//
extern void corLdCacheSnapshot(KAlloc* allocP, CorLdContext*** arrPP, int* nP);

#endif
