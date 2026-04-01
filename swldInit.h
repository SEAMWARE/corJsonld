//
// FILE            swldInit.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_INIT_H
#define SWLD_INIT_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "swJsonld/SwldContext.h"                     // SwldContext



// -----------------------------------------------------------------------------
//
// SWLD_CORE_CONTEXT_URL -
//
#define SWLD_CORE_CONTEXT_URL  "https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.9.jsonld"



// -----------------------------------------------------------------------------
//
// SwldDownloadFunction - callback for downloading remote @context URLs
//
// The function receives a URL and must return a malloc'd buffer with the
// response body (JSON), or NULL on error.  The library will free the buffer.
// *statusCodeP should be set to the HTTP status code (200 on success).
//
typedef char* (*SwldDownloadFunction)(const char* url, int* statusCodeP);



// -----------------------------------------------------------------------------
//
// swldInit -
//
extern int swldInit(KAlloc* kaP, const char* coreContextUrl, SwldDownloadFunction downloadFn);



// -----------------------------------------------------------------------------
//
// swldCleanup -
//
extern void swldCleanup(void);



// -----------------------------------------------------------------------------
//
// swldCoreContext -
//
extern SwldContext* swldCoreContext(void);

#endif
