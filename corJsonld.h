#ifndef CORJSONLD_H_
#define CORJSONLD_H_

//
// FILE            corJsonld.h - umbrella header for the corJsonld library
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTInit, CorLdTExpand, ...
#include "corJsonld/corLdInit.h"                       // corLdInit, corLdCleanup, corLdCoreContext, CorLdDownloadFunction
#include "corJsonld/corLdContextParse.h"               // corLdContextFromTree, corLdContextFromObject
#include "corJsonld/corLdExpand.h"                     // corLdExpand, corLdAlreadyExpanded
#include "corJsonld/corLdCompact.h"                    // corLdCompact
#include "corJsonld/corLdExpandTree.h"                 // corLdExpandTree
#include "corJsonld/corLdCompactTree.h"                // corLdCompactTree
#include "corJsonld/corLdPrefixExpand.h"               // corLdPrefixExpand
#include "corJsonld/CorLdContextCache.h"               // CorLdContextCache
#include "corJsonld/corLdCache.h"                      // corLdCacheLookup, corLdCacheInsert
#include "corJsonld/corLdDownload.h"                   // corLdContextFromUrl
#include "corJsonld/version.h"                        // CORJSONLD_VERSION



// -----------------------------------------------------------------------------
//
// corJsonldVersion -
//
extern const char* corJsonldVersion;

#endif  // CORJSONLD_H_
