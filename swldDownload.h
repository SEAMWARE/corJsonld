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



// -----------------------------------------------------------------------------
//
// swldIsCoreContextUrl - true if `url` names the NGSI-LD core context:
// the configured core, the canonical unversioned form, or any older
// versioned form (the ignored-stub family). The admin API treats all of
// them as THE core entry.
//
extern bool swldIsCoreContextUrl(const char* url);

#endif
