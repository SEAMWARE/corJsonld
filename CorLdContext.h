//
// FILE            CorLdContext.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_CONTEXT_H
#define CORLD_CONTEXT_H

#include <stdbool.h>                                 // bool
#include "khash/khash.h"                             // KHashTable



// -----------------------------------------------------------------------------
//
// CorLdContextKind - per NGSI-LD § 5.13.1
//
typedef enum CorLdContextKind
{
  CorLdKindImplicit = 0,
  CorLdKindCached,
  CorLdKindHosted
} CorLdContextKind;



// -----------------------------------------------------------------------------
//
// CorLdContext -
//
typedef struct CorLdContext
{
  char*                      url;         // URL of this context (NULL for inline)
  char*                      id;          // Identifier (URL or generated)
  char*                      body;        // Raw JSON body as received (may be NULL)
  CorLdContextKind            kind;
  KHashTable*                nameHT;      // name -> CorLdItem  (expansion)
  KHashTable*                valueHT;     // IRI  -> CorLdItem  (compaction)
  char*                      vocab;       // @vocab value, or NULL
  struct CorLdContext**        contextV;    // child contexts for arrays
  int                        contexts;    // count of child contexts
  bool                       isArray;     // true = array of child contexts
  // ignored = true when URL matches a known NGSI-LD core context that is
  // NOT the broker's configured core (e.g. v1.6 / v1.7 / v1.8 sent by a
  // user payload while the broker runs its embedded v1.9). The body stays
  // cached for echo via GET /jsonldContexts/{id}, but the mappings are
  // skipped during expansion / compaction — the broker's own core wins.
  bool                       ignored;
  // volatileCtx = true for a broker-minted one-shot context hosted only so
  // a Link header (response or distop forward) can reference an inline /
  // multi-element user @context by URL. Never persisted to the DB; served
  // with Cache-Control: no-store and dropped after the first GET; reaped
  // at expiresAt if never fetched. Skipped by GET /jsonldContexts list.
  bool                       volatileCtx;
  double                     expiresAt;   // volatile reap deadline (epoch s); 0 = never
  double                     createdAt;
  double                     usedAt;
  struct CorLdContext*         next;        // cache linked-list linkage
} CorLdContext;

#endif
