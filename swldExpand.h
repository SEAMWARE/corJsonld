//
// FILE            swldExpand.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_EXPAND_H
#define SWLD_EXPAND_H

#include <stdbool.h>                                 // bool
#include "kalloc/KAlloc.h"                           // KAlloc
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldExpand -
//
extern char* swldExpand(SwldContext* contextP, const char* name, KAlloc* kaP, SwldItem** itemPP, bool* coreContextP);



// -----------------------------------------------------------------------------
//
// swldAlreadyExpanded -
//
extern bool swldAlreadyExpanded(const char* value);

#endif
