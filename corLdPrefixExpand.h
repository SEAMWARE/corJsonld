//
// FILE            corLdPrefixExpand.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_PREFIX_EXPAND_H
#define CORLD_PREFIX_EXPAND_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdPrefixExpand -
//
extern char* corLdPrefixExpand(CorLdContext* contextP, const char* name, KAlloc* kaP);

#endif
