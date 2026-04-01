#ifndef SWJSONLD_SWJSONLD_H_
#define SWJSONLD_SWJSONLD_H_

//
// FILE            swJsonld.h - umbrella header for the swJsonld library
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTInit, SwldTExpand, ...
#include "swJsonld/swldInit.h"                       // swldInit, swldCleanup, swldCoreContext, SwldDownloadFunction
#include "swJsonld/swldContextParse.h"               // swldContextFromTree, swldContextFromObject
#include "swJsonld/swldExpand.h"                     // swldExpand, swldAlreadyExpanded
#include "swJsonld/swldCompact.h"                    // swldCompact
#include "swJsonld/swldExpandTree.h"                 // swldExpandTree
#include "swJsonld/swldCompactTree.h"                // swldCompactTree
#include "swJsonld/swldPrefixExpand.h"               // swldPrefixExpand
#include "swJsonld/SwldContextCache.h"               // SwldContextCache
#include "swJsonld/swldCache.h"                      // swldCacheLookup, swldCacheInsert
#include "swJsonld/swldDownload.h"                   // swldContextFromUrl
#include "swJsonld/version.h"                        // SWJSONLD_VERSION



// -----------------------------------------------------------------------------
//
// swJsonldVersion -
//
extern const char* swJsonldVersion;

#endif  // SWJSONLD_SWJSONLD_H_
