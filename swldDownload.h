//
// FILE            swldDownload.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_DOWNLOAD_H
#define SWLD_DOWNLOAD_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// swldContextFromUrl -
//
extern SwldContext* swldContextFromUrl(const char* url, KAlloc* kaP);

#endif
