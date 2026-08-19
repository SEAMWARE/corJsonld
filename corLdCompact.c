//
// FILE            corLdCompact.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                  // strncmp, strlen

#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "khash/khash.h"                             // khashItemLookup, KHashListItem
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTCompact
#include "corJsonld/corLdExpand.h"                     // corLdAlreadyExpanded
#include "corJsonld/corLdInit.h"                       // corLdCoreContext, corLdCorePrefixes
#include "corRest/corRest.h"                           // corRest.kalloc for compact-IRI alloc
#include "corJsonld/corLdCompact.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// contextReverseLookup - lookup by IRI in valueHT (handles arrays)
//
static CorLdItem* contextReverseLookup(CorLdContext* contextP, const char* iri)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      CorLdItem* itemP = contextReverseLookup(contextP->contextV[ix], iri);

      if (itemP != NULL)
        return itemP;
    }

    return NULL;
  }

  if (contextP->valueHT == NULL)
    return NULL;

  return (CorLdItem*) khashItemLookup(contextP->valueHT, iri);
}



// -----------------------------------------------------------------------------
//
// contextVocabCompact - strip @vocab prefix if it matches
//
static const char* contextVocabCompact(CorLdContext* contextP, const char* iri)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->ignored == true)
    return NULL;

  if (contextP->vocab != NULL)
  {
    int vocabLen = strlen(contextP->vocab);

    if (strncmp(iri, contextP->vocab, vocabLen) == 0)
      return iri + vocabLen;
  }

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      const char* result = contextVocabCompact(contextP->contextV[ix], iri);

      if (result != NULL)
        return result;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// contextNameLookup - lookup by short name in nameHT (handles arrays)
//
// Mirror of contextReverseLookup but indexed by short name. Used by
// corLdCompact to detect a shadowing conflict: when @vocab-stripping
// would produce a name that's also defined as a term mapping to a
// different IRI, the bare name would be ambiguous on the wire — the
// receiver would expand it back to the term's IRI, not the original.
//
static CorLdItem* contextNameLookup(CorLdContext* contextP, const char* name)
{
  if (contextP == NULL || contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      CorLdItem* itemP = contextNameLookup(contextP->contextV[ix], name);
      if (itemP != NULL)
        return itemP;
    }
    return NULL;
  }

  if (contextP->nameHT == NULL)
    return NULL;

  return (CorLdItem*) khashItemLookup(contextP->nameHT, name);
}



// -----------------------------------------------------------------------------
//
// vocabStripGuarded -
//
// Like contextVocabCompact, but reports the outcome so the caller can
// distinguish three cases:
//
//   *outShadowedP = false, return non-NULL   → safe to use the bare name
//   *outShadowedP = true,  return NULL       → strip would shadow; emit
//                                              compact-IRI form instead
//   *outShadowedP = false, return NULL       → @vocab didn't match at all
//                                              (leave as full IRI)
//
// The shadow distinction matters: only the second case should fall through
// to compact-IRI emission; the third case must keep the full IRI verbatim
// so well-known canonical forms (e.g. ngsi-ld error-type URIs) aren't
// re-shaped on the wire.
//
static const char* vocabStripGuarded(CorLdContext* userP,
                                     CorLdContext* fromP,
                                     const char*  iri,
                                     bool*        outShadowedP)
{
  *outShadowedP = false;

  const char* stripped = contextVocabCompact(fromP, iri);
  if (stripped == NULL)
    return NULL;

  CorLdItem* conflictP = contextNameLookup(userP, stripped);
  if (conflictP == NULL)
    conflictP = contextNameLookup(corLdCoreContext(), stripped);
  if (conflictP != NULL && conflictP->id != NULL && strcmp(conflictP->id, iri) != 0)
  {
    *outShadowedP = true;
    return NULL;
  }

  return stripped;
}



// -----------------------------------------------------------------------------
//
// prefixCompact -
//
// Find the longest prefix-shaped term (id ending with `/`, `#`, or `:`)
// in the active chain whose id is a prefix of `iri`, and return the
// compact-IRI form `<term-name>:<iri-suffix>`. The check applies only
// to non-recursive contexts; isArray walks recursively, longest match
// wins across the entire chain.
//
// Allocated in corRest.kalloc when a match is found; NULL otherwise.
//
static const char* prefixCompactScan(CorLdContext* contextP,
                                     const char*  iri,
                                     CorLdItem**   bestPP,
                                     int*         bestLenP)
{
  if (contextP == NULL || contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
      prefixCompactScan(contextP->contextV[ix], iri, bestPP, bestLenP);
    return NULL;
  }

  if (contextP->nameHT == NULL)
    return NULL;

  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    KHashListItem* lP = contextP->nameHT->array[slot];
    while (lP != NULL)
    {
      CorLdItem* itP = (CorLdItem*) lP->data;
      if (itP->id != NULL && itP->name != NULL && itP->name[0] != '@')
      {
        int idLen = strlen(itP->id);
        // Prefix-shaped: must end with /, #, or : (RFC 3986 sub-delims
        // that are typically reserved as IRI separators). Length > current
        // best to enforce longest-match wins.
        if (idLen > *bestLenP &&
            (itP->id[idLen - 1] == '/' || itP->id[idLen - 1] == '#' || itP->id[idLen - 1] == ':') &&
            strncmp(iri, itP->id, idLen) == 0 &&
            iri[idLen] != 0)
        {
          *bestPP   = itP;
          *bestLenP = idLen;
        }
      }
      lP = lP->next;
    }
  }
  return NULL;
}

static const char* prefixCompactBuild(const char* iri, const char* name, int idLen)
{
  const char* suffix = iri + idLen;
  int nameLen   = strlen(name);
  int suffixLen = strlen(suffix);
  char* out = (char*) kaAlloc(&corRest.kalloc, nameLen + 1 + suffixLen + 1);
  memcpy(out, name, nameLen);
  out[nameLen] = ':';
  memcpy(out + nameLen + 1, suffix, suffixLen + 1);
  return out;
}

static const char* prefixCompact(CorLdContext* contextP, const char* iri)
{
  // Phase 1: scan user-context nameHT (not subject to the core-context
  // rewrite — prefix terms there still hold their full IRIs).
  CorLdItem* bestP   = NULL;
  int       bestLen = 0;
  prefixCompactScan(contextP, iri, &bestP, &bestLen);

  // Phase 2: scan the pre-rewrite snapshot of the core context. Pick
  // longest-match between user and core combined.
  int          coreN = 0;
  const CorLdCorePrefix* coreV = corLdCorePrefixes(&coreN);
  const CorLdCorePrefix* coreBest = NULL;
  for (int i = 0; i < coreN; i++)
  {
    if (coreV[i].idLen > bestLen &&
        strncmp(iri, coreV[i].id, coreV[i].idLen) == 0 &&
        iri[coreV[i].idLen] != 0)
    {
      coreBest = &coreV[i];
      bestLen  = coreV[i].idLen;
      bestP    = NULL;   // core wins
    }
  }

  if (bestP != NULL)
    return prefixCompactBuild(iri, bestP->name, bestLen);
  if (coreBest != NULL)
    return prefixCompactBuild(iri, coreBest->name, bestLen);
  return NULL;
}



// -----------------------------------------------------------------------------
//
// corLdCompact -
//
const char* corLdCompact(CorLdContext* contextP, const char* iri)
{
  //
  // Step 1: Not a full IRI - return as-is
  //
  if (corLdAlreadyExpanded(iri) == false)
    return iri;

  CorLdContext* coreP = corLdCoreContext();

  //
  // Step 2: Reverse lookup in user context (exact term match wins —
  // strongest binding the spec gives us)
  //
  CorLdItem* itemP = contextReverseLookup(contextP, iri);
  if (itemP != NULL)
    return itemP->name;

  //
  // Step 3: Reverse lookup in core context (same idea, core fallback)
  //
  itemP = contextReverseLookup(coreP, iri);
  if (itemP != NULL)
    return itemP->name;

  //
  // Step 4: @vocab stripping — CORE ONLY. @vocab is core-context-only on
  // expansion (a user-context @vocab is ignored), so stripping a user
  // @vocab here would emit a short name that does NOT expand back to the
  // same IRI — a round-trip corruption. Guarded: record whether the strip
  // was BLOCKED by a shadowing user term (vs. just not applicable), so
  // step 5 below only fires for shadowed cases.
  //
  bool shadowed = false;
  const char* result = vocabStripGuarded(contextP, coreP, iri, &shadowed);
  if (result != NULL)
    return result;

  //
  // Step 5: Compact-IRI prefix match — ONLY when step 4 was blocked by
  // shadowing. Without this guard, the prefix-compact would fire for
  // any IRI that happens to share a defined prefix (e.g. NGSI-LD error
  // type URIs would re-shape to `ngsi-ld:errors/…`), which changes
  // wire shapes for non-buggy cases. The narrow guard keeps the fix
  // scoped to the actual ambiguity the bug introduced.
  //
  if (shadowed)
  {
    result = prefixCompact(contextP, iri);
    if (result != NULL)
      return result;
  }

  //
  // Step 6: Return unchanged. Caller (compactForUrl for query strings,
  // or the rendering layer for body emission) will keep the full IRI
  // when nothing safer applies — a verbose but unambiguous wire form.
  //
  return iri;
}
