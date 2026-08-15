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
// SwldErrorFunction - callback for reporting WHY an @context could not be used
//
// swldContextFromUrl answers NULL for every kind of failure, and the caller then
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
typedef void (*SwldErrorFunction)(int status, const char* title, const char* detail);



// -----------------------------------------------------------------------------
//
// swldInit -
//
extern int swldInit(KAlloc* kaP, const char* coreContextUrl, SwldDownloadFunction downloadFn, SwldErrorFunction errorFn);



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



// -----------------------------------------------------------------------------
//
// swldCoreVocab / swldCoreVocabLen -
//
// The core context's @vocab (the default context URI) and its length.
// Set once by swldInit — the core context ALWAYS carries @vocab; init
// fails otherwise — so the expand hot path reads these directly.
//
extern const char* swldCoreVocab;
extern int         swldCoreVocabLen;



// -----------------------------------------------------------------------------
//
// SwldCorePrefix / swldCorePrefixes -
//
// Snapshot of the core context's prefix-shaped terms (id ends with /, #,
// or :), captured at init BEFORE coreContextRewriteToShort flattens id
// to name. Used by swldCompact's compact-IRI step to emit forms like
// `ngsi-ld:default-context/almostFull` when @vocab strip would be
// ambiguous.
//
typedef struct SwldCorePrefix {
  const char* name;
  const char* id;
  int         idLen;
} SwldCorePrefix;

extern const SwldCorePrefix* swldCorePrefixes(int* countP);

#endif
