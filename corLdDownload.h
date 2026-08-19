//
// FILE            corLdDownload.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_DOWNLOAD_H
#define CORLD_DOWNLOAD_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdContextFromUrl -
//
extern CorLdContext* corLdContextFromUrl(const char* url, KAlloc* kaP);



// -----------------------------------------------------------------------------
//
// corLdIsCoreContextUrl - true if `url` names the NGSI-LD core context:
// the configured core, the canonical unversioned form, or any older
// versioned form (the ignored-stub family). The admin API treats all of
// them as THE core entry.
//
extern bool corLdIsCoreContextUrl(const char* url);

#endif
