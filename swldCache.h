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

#endif
