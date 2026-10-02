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

#include <stdint.h>                                  // uint16_t

#include "corAlloc/CorAlloc.h"                       // CorAlloc
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
// CorLdOwnerFunction / CorLdSleepFunction / corLdConcurrencySet - who is asking, and how to wait
//
// A download in progress is owned by whoever started it: another asker for the same @context waits for
// it, and the SAME asker asking again means a cyclic @context. By default the asker is the thread
// (pthread_self) and a wait is usleep - right while one thread runs one request. An application that
// runs several requests on one thread (coroutines) says who the asker is - the request - and how to
// wait without stopping the thread's other requests: a sleep that yields.
//
// Either may be NULL: the default. Set before the first request.
//
typedef uintptr_t (*CorLdOwnerFunction)(void);
typedef void      (*CorLdSleepFunction)(int ms);

extern void      corLdConcurrencySet(CorLdOwnerFunction ownerFn, CorLdSleepFunction sleepFn);
extern uintptr_t corLdOwner(void);
extern void      corLdSleepMs(int ms);



// -----------------------------------------------------------------------------
//
// corLdInit -
//
extern int corLdInit(CorAlloc* kaP, const char* coreContextUrl, CorLdDownloadFunction downloadFn, CorLdErrorFunction errorFn);



// -----------------------------------------------------------------------------
//
// corLdCleanup -
//
extern void corLdCleanup(void);



// -----------------------------------------------------------------------------
//
// CorLdCoreTerm - a term the broker adds to the core context
//
typedef struct CorLdCoreTerm
{
  const char* name;   // the term - and, as for every core term, its own expansion
  const char* type;   // "@id", "@vocab", ... or NULL
} CorLdCoreTerm;



// -----------------------------------------------------------------------------
//
// corLdCoreTermsAdd - make terms of the broker's own payloads CORE terms
//
// A payload the broker defines beyond the published core context - coraine's
// ContextBridge and Channel, proposed to ETSI for the core - uses terms that
// must behave exactly as core terms do: never expanded, never overridden by a
// user @context. Added after corLdInit, into the rewritten core (id = name),
// flagged KJF_CORE_TERM. A term the core already defines is left as it is.
//
// ⚠ The core overrides EVERY other context, so a term added here takes its
// name from every vocabulary a client may load - choose names absent from the
// well-known ones (Smart Data Models, schema.org, SOSA/SSN, SAREF).
//
// termV ends with a { NULL, NULL } entry. Returns the number added, -1 on error.
//
extern int corLdCoreTermsAdd(const CorLdCoreTerm* termV, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// corLdCoreTermIdsSet - give every core term its id
//
// nameV[id] is the name of the term whose id is 'id', for 1 <= id < count; nameV[0]
// is unused, as 0 means "not a core term". The ids are the caller's (for NGSI-LD, an
// enum in corNgsild) - corJsonld does not know what they mean.
//
// Stamps the item in the core context and its twin in the pristine copy (both are
// handed out by corLdExpand), and fills the table behind corLdCoreItemById. Call it
// once, after corLdInit and after any corLdCoreTermsAdd.
//
// The core context is fixed at startup, so every core term must have an id: a core
// item left without one is a startup error - -1, with *missingP naming it. A name in
// nameV that the core does not define is skipped.
//
extern int corLdCoreTermIdsSet(const char* const* nameV, int count, CorAlloc* kaP, const char** missingP);



// -----------------------------------------------------------------------------
//
// corLdCoreItemById - the core item whose termId is 'termId' - an array index, no lookup
//
// NULL for 0, for an id out of range, and before corLdCoreTermIdsSet.
//
extern struct CorLdItem* corLdCoreItemById(uint16_t termId);



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



// -----------------------------------------------------------------------------
//
// corLdCoreItemByIri - the core term a fully-expanded IRI names, or NULL
//
// The expanded spelling of every core term, snapshotted at init BEFORE
// coreContextRewriteToShort flattens id to name. Lets corLdExpand recognise a
// core term sent as its IRI - which the spec permits and clients do.
//
// The core context's own valueHT cannot answer this: its compare function
// dereferences itemP->id at LOOKUP time, and the rewrite has by then set
// id = name, so every core reverse lookup compares an IRI against a short name
// and fails. This table keeps its own copy of the IRI instead.
//
extern struct CorLdItem* corLdCoreItemByIri(const char* iri);



// -----------------------------------------------------------------------------
//
// corLdCorePristine - the core context as parsed, before the short-name rewrite
//
// coreContextRewriteToShort() flattens every core term's id to its own name, so
// the working core context knows none of its own IRIs. This second parse keeps
// them. Ask it for anything that needs a real core IRI - reverse lookups,
// prefix IRIs - and never read itemP->id off the working copy for that.
//
extern CorLdContext* corLdCorePristine(void);

#endif
