//
// FILE            corLdInit.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_INIT_H
#define CORLD_INIT_H

#include "kalloc/KAlloc.h"                           // KAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// CORLD_CORE_CONTEXT_URL -
//
#define CORLD_CORE_CONTEXT_URL  "https://uri.etsi.org/ngsi-ld/v1/ngsi-ld-core-context-v1.9.jsonld"



// -----------------------------------------------------------------------------
//
// CorLdDownloadFunction - callback for downloading remote @context URLs
//
// The function receives a URL and must return a malloc'd buffer with the
// response body (JSON), or NULL on error.  The library will free the buffer.
// *statusCodeP should be set to the HTTP status code (200 on success).
//
typedef char* (*CorLdDownloadFunction)(const char* url, int* statusCodeP);



// -----------------------------------------------------------------------------
//
// CorLdErrorFunction - callback for reporting WHY an @context could not be used
//
// corLdContextFromUrl answers NULL for every kind of failure, and the caller then
// has to guess - it guesses "could not be retrieved", which is right for a
// download that failed and wrong for an @context that was retrieved perfectly
// well and is simply unusable. A cyclic @context is the second kind: both
// documents download, they just reference each other.
//
// The library reports the ones it can name through this callback, in the terms
// an API layer needs: an HTTP status, a title and a detail. Whether that becomes
// a ProblemDetails, a log line or nothing at all is the caller's business - which
// is why the JSON-LD layer does not reach for one itself.
//
typedef void (*CorLdErrorFunction)(int status, const char* title, const char* detail);



// -----------------------------------------------------------------------------
//
// corLdInit -
//
extern int corLdInit(KAlloc* kaP, const char* coreContextUrl, CorLdDownloadFunction downloadFn, CorLdErrorFunction errorFn);



// -----------------------------------------------------------------------------
//
// corLdCleanup -
//
extern void corLdCleanup(void);



// -----------------------------------------------------------------------------
//
// corLdCoreContext -
//
extern CorLdContext* corLdCoreContext(void);



// -----------------------------------------------------------------------------
//
// corLdCoreVocab / corLdCoreVocabLen -
//
// The core context's @vocab (the default context URI) and its length.
// Set once by corLdInit — the core context ALWAYS carries @vocab; init
// fails otherwise — so the expand hot path reads these directly.
//
extern const char* corLdCoreVocab;
extern int         corLdCoreVocabLen;



// -----------------------------------------------------------------------------
//
// CorLdCorePrefix / corLdCorePrefixes -
//
// Snapshot of the core context's prefix-shaped terms (id ends with /, #,
// or :), captured at init BEFORE coreContextRewriteToShort flattens id
// to name. Used by corLdCompact's compact-IRI step to emit forms like
// `ngsi-ld:default-context/almostFull` when @vocab strip would be
// ambiguous.
//
typedef struct CorLdCorePrefix {
  const char* name;
  const char* id;
  int         idLen;
} CorLdCorePrefix;

extern const CorLdCorePrefix* corLdCorePrefixes(int* countP);

#endif
