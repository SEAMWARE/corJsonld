//
// FILE            swldPrefixExpand.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_PREFIX_EXPAND_H
#define SWLD_PREFIX_EXPAND_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldPrefixExpand -
//
extern char* swldPrefixExpand(SwldContext* contextP, const char* name, KAlloc* kaP);

#endif
