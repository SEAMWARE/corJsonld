//
// FILE            corLdInit.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                 // bool, true, false
#include <stdlib.h>                                  // strdup, free
#include <string.h>                                  // memset, strcmp

#include <pthread.h>                                 // pthread_mutex_init, pthread_mutex_destroy

#include "ktrace/kTrace.h"                            // KT_E
#include "kjson/KjNode.h"                            // KjNode
#include "kjson/kjParse.h"                           // kjParse
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBufferCreate.h"                    // kjBufferCreate
#include "khash/khash.h"                             // KHashTable, KHashListItem
#include "corJsonld/corLdTraceLevels.h"                // CorLdTInit
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                    // CorLdContext
#include "corJsonld/corLdContextParse.h"               // corLdContextFromObject
#include "corJsonld/corLdExpand.h"                     // contextVocab
#include "corJsonld/CorLdContextCache.h"               // CorLdContextCache
#include "corJsonld/corLdCache.h"                      // corLdCacheInsert
#include "corJsonld/corLdDownload.h"                   // corLdContextFromUrl
#include "corJsonld/corLdCoreContextBody.h"            // corLdCoreContextBody
#include "corJsonld/corLdInit.h"                       // Own interface



// -----------------------------------------------------------------------------
//
// Global state
//
static CorLdContextCache      corLdGlobalCache;
static CorLdContext*          corLdCoreContextP     = NULL;
static bool                  corLdInitialized      = false;

// Core @vocab — the core context ALWAYS carries @vocab (corLdInit refuses
// to start without one); cached here with its length so the expand hot
// path uses it directly, no per-call context walk.
const char* corLdCoreVocab    = NULL;
int         corLdCoreVocabLen = 0;

// -----------------------------------------------------------------------------
//
// Core-context prefix snapshot — { name, id } pairs captured pre-rewrite
//
// coreContextRewriteToShort flattens every core term's id to its short
// name (for the hot expand/compact path). That destroys the only place
// the broker can read prefix-shaped IRIs like `ngsi-ld → https://uri.etsi.org/ngsi-ld/`,
// which compact-IRI emission needs (e.g. to render
// `ngsi-ld:default-context/almostFull` when @vocab strip would be
// ambiguous). We grab the snapshot pre-rewrite once at init and serve
// it via corLdCorePrefixes() for prefixCompact's longest-match scan.
//
typedef struct CorLdCorePrefix {
  const char* name;   // e.g. "ngsi-ld"
  const char* id;     // e.g. "https://uri.etsi.org/ngsi-ld/"
  int         idLen;
} CorLdCorePrefix;

static CorLdCorePrefix  corePrefixV[16];   // tiny set in practice (NGSI-LD core has 2: ngsi-ld + geojson)
static int             corePrefixN = 0;

// Public accessor (declared in corLdInit.h).
const CorLdCorePrefix* corLdCorePrefixes(int* countP)
{
  if (countP != NULL) *countP = corePrefixN;
  return corePrefixV;
}

// Snapshot prefix-shaped core terms before the rewrite. Prefix shape: id
// ends with '/', '#', or ':'. Recurses for isArray, dedup by name.
static void coreContextPrefixSnapshot(CorLdContext* contextP)
{
  if (contextP == NULL || contextP->ignored == true)
    return;

  if (contextP->isArray == true)
  {
    for (int ix = 0; ix < contextP->contexts; ix++)
      coreContextPrefixSnapshot(contextP->contextV[ix]);
    return;
  }

  if (contextP->nameHT == NULL)
    return;

  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    for (KHashListItem* lP = contextP->nameHT->array[slot]; lP != NULL; lP = lP->next)
    {
      CorLdItem* itP = (CorLdItem*) lP->data;
      if (itP == NULL || itP->name == NULL || itP->id == NULL) continue;
      if (itP->name[0] == '@') continue;

      int idLen = (int) strlen(itP->id);
      if (idLen < 1) continue;
      char last = itP->id[idLen - 1];
      if (last != '/' && last != '#' && last != ':') continue;

      // Dedup by name (compound contexts can re-introduce the same prefix)
      bool dup = false;
      for (int i = 0; i < corePrefixN; i++)
        if (strcmp(corePrefixV[i].name, itP->name) == 0) { dup = true; break; }
      if (dup) continue;

      if (corePrefixN >= (int)(sizeof(corePrefixV)/sizeof(corePrefixV[0]))) return;
      corePrefixV[corePrefixN].name  = itP->name;
      corePrefixV[corePrefixN].id    = itP->id;
      corePrefixV[corePrefixN].idLen = idLen;
      corePrefixN++;
    }
  }
}
static CorLdDownloadFunction  corLdDownloadFn       = NULL;
static CorLdErrorFunction     corLdErrorFn          = NULL;



// -----------------------------------------------------------------------------
//
// corLdCoreContext -
//
CorLdContext* corLdCoreContext(void)
{
  return corLdCoreContextP;
}



// -----------------------------------------------------------------------------
//
// corLdCacheGet - internal, used by corLdCache.c via extern
//
CorLdContextCache* corLdCacheGet(void)
{
  return &corLdGlobalCache;
}



// -----------------------------------------------------------------------------
//
// corLdDownloadGet - internal, used by corLdDownload.c via extern
//
CorLdDownloadFunction corLdDownloadGet(void)
{
  return corLdDownloadFn;
}



// -----------------------------------------------------------------------------
//
// corLdErrorGet - internal, used by corLdDownload.c via extern
//
CorLdErrorFunction corLdErrorGet(void)
{
  return corLdErrorFn;
}



// -----------------------------------------------------------------------------
//
// coreContextRewriteToShort - rewrite each core item's id to its name
//
// The core-context shortcut keeps NGSI-LD core terms in their short form
// throughout the broker pipeline (parse, cache, DB, render). Done here at
// init time, exactly once: corLdExpand naturally returns the short name for
// any core term thereafter, with no per-call branching.
//
// Items whose id is a JSON-LD keyword (e.g. "id" -> "@id", "type" -> "@type")
// must keep their id intact so the @-keyword bypass in expandObject and the
// @type-value branch continue to fire.
//
static void coreContextRewriteToShort(CorLdContext* contextP)
{
  if (contextP == NULL)
    return;

  if (contextP->isArray == true)
  {
    for (int ix = 0; ix < contextP->contexts; ix++)
      coreContextRewriteToShort(contextP->contextV[ix]);
    return;
  }

  if (contextP->nameHT == NULL)
    return;

  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    for (KHashListItem* lP = contextP->nameHT->array[slot]; lP != NULL; lP = lP->next)
    {
      CorLdItem* itemP = (CorLdItem*) lP->data;

      if (itemP == NULL || itemP->id == NULL || itemP->name == NULL)
        continue;

      if (itemP->id[0] == '@')
        continue;

      itemP->id = itemP->name;
    }
  }
}



// -----------------------------------------------------------------------------
//
// coreTermFlags - the KJF_* bits for a core-context term, by short name
//
// Classified once (at core-context load) and copied onto every KjNode whose
// term resolves to this item — so the broker tells structural members from
// sub-attributes (and which value-key) with a bit test, never a strcmp chain.
//
static unsigned char coreTermFlags(const char* name)
{
  unsigned char flags = KJF_CORE_TERM;   // every core-context term

  unsigned char vk = KJF_VK_NONE;
  if      (strcmp(name, "value")       == 0)  vk = KJF_VK_VALUE;
  else if (strcmp(name, "object")      == 0)  vk = KJF_VK_OBJECT;
  else if (strcmp(name, "languageMap") == 0)  vk = KJF_VK_LANGUAGEMAP;
  else if (strcmp(name, "vocab")       == 0)  vk = KJF_VK_VOCAB;
  else if (strcmp(name, "valueList")   == 0)  vk = KJF_VK_VALUELIST;
  else if (strcmp(name, "objectList")  == 0)  vk = KJF_VK_OBJECTLIST;
  else if (strcmp(name, "json")        == 0)  vk = KJF_VK_JSON;

  if (vk != KJF_VK_NONE)
    flags |= KJF_ATTR_TERM | (vk << KJF_VK_SHIFT);

  // Structural attribute members that are not value-keys. valueType and
  // objectType are @vocab-coerced (their value is a type term, not a
  // sub-Property) — without KJF_ATTR_TERM they would be wrongly reified as
  // a { "type":"Property", "value":... } sub-attribute on input-normalize.
  // (Classification runs once at core-context load, not per request.)
  if ((strcmp(name, "type")       == 0) ||
      (strcmp(name, "observedAt") == 0) ||
      (strcmp(name, "expiresAt")  == 0) ||
      (strcmp(name, "unitCode")   == 0) ||
      (strcmp(name, "datasetId")  == 0) ||
      (strcmp(name, "valueType")  == 0) ||
      (strcmp(name, "objectType") == 0))
    flags |= KJF_ATTR_TERM;

  return flags;
}



// -----------------------------------------------------------------------------
//
// coreContextClassifyFlags - set CorLdItem.flags on every core-context item
//
static void coreContextClassifyFlags(CorLdContext* contextP)
{
  if (contextP == NULL)
    return;

  if (contextP->isArray == true)
  {
    for (int ix = 0; ix < contextP->contexts; ix++)
      coreContextClassifyFlags(contextP->contextV[ix]);
    return;
  }

  if (contextP->nameHT == NULL)
    return;

  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    for (KHashListItem* lP = contextP->nameHT->array[slot]; lP != NULL; lP = lP->next)
    {
      CorLdItem* itemP = (CorLdItem*) lP->data;

      if (itemP == NULL || itemP->name == NULL)
        continue;

      itemP->flags = coreTermFlags(itemP->name);
    }
  }
}



// -----------------------------------------------------------------------------
//
// coreContextFromEmbedded - parse the compiled-in core context body
//
static CorLdContext* coreContextFromEmbedded(KAlloc* kaP)
{
  //
  // strdup because kjParse is destructive
  //
  char* body = strdup(corLdCoreContextBody);

  if (body == NULL)
    return NULL;

  Kjson  kjson;
  Kjson* kjsonP = kjBufferCreate(&kjson, kaP);

  KjNode* treeP = kjParse(kjsonP, body);

  if (treeP == NULL)
  {
    free(body);
    return NULL;
  }

  KjNode* atContextP = kjLookup(treeP, "@context");

  if (atContextP == NULL)
  {
    free(body);
    return NULL;
  }

  CorLdContext* contextP = corLdContextFromObject(atContextP, kaP, CORLD_CORE_CONTEXT_URL);

  if (contextP != NULL)
  {
    //
    // Snapshot prefix-shaped terms (e.g. ngsi-ld → https://uri.etsi.org/ngsi-ld/)
    // BEFORE the rewrite — the rewrite flattens id to name and the prefix
    // URL is gone after. corLdCorePrefixes() serves the snapshot for
    // compact-IRI emission in corLdCompact step 5.
    //
    coreContextPrefixSnapshot(contextP);

    //
    // Core-context shortcut: rewrite each item's id to its name so the
    // expander returns short forms for core terms with zero per-call work.
    //
    coreContextRewriteToShort(contextP);
    coreContextClassifyFlags(contextP);

    //
    // Preserve the compiled-in body for GET /jsonldContexts/{id}.
    // corLdCoreContextBody lives for the process lifetime, so we point at
    // it directly rather than duplicating.
    //
    // Kind stays "ImplicitlyCreated": "Cached" would drag the core into
    // ?kind=Cached list filters and § 13.4.4's serve-content 422 for
    // Cached entries — all blast radius, no conformance gain (the suite
    // never asserts the core's kind).
    //
    contextP->body = (char*) corLdCoreContextBody;
    contextP->kind = CorLdKindImplicit;
    corLdCacheInsert(contextP);
  }

  free(body);
  return contextP;
}



// -----------------------------------------------------------------------------
//
// corLdInit -
//
int corLdInit(KAlloc* kaP, const char* coreContextUrl, CorLdDownloadFunction downloadFn, CorLdErrorFunction errorFn)
{
  if (corLdInitialized == true)
    return 0;

  memset(&corLdGlobalCache, 0, sizeof(corLdGlobalCache));
  pthread_mutex_init(&corLdGlobalCache.mutex, NULL);
  corLdGlobalCache.maxEntries = 100;
  corLdGlobalCache.kaP        = kaP;
  corLdDownloadFn             = downloadFn;
  corLdErrorFn                = errorFn;
  corLdInitialized            = true;

  //
  // If no URL given, or it matches the default, use the embedded body
  //
  if (coreContextUrl == NULL || strcmp(coreContextUrl, CORLD_CORE_CONTEXT_URL) == 0)
  {
    corLdCoreContextP = coreContextFromEmbedded(kaP);
    if (corLdCoreContextP == NULL)
      return -1;
  }
  else
  {
    //
    // Non-default URL: download from network
    //
    corLdCoreContextP = corLdContextFromUrl(coreContextUrl, kaP);
    if (corLdCoreContextP == NULL)
      return -1;

    coreContextPrefixSnapshot(corLdCoreContextP);
    coreContextRewriteToShort(corLdCoreContextP);
    coreContextClassifyFlags(corLdCoreContextP);
  }

  //
  // The core context MUST declare @vocab — it's the unconditional fallback
  // for any short term no context defined explicitly (JSON-LD § 4.1.2).
  // Without it, corLdExpand has no way to construct an IRI for unknown user
  // terms and would silently leak them downstream, corrupting cache / DB /
  // distop comparisons that all assume fully-expanded IRIs.
  //
  // Refuse to start rather than discover this on the Nth request after
  // some entity has already been persisted with bare names.
  //
  corLdCoreVocab = contextVocab(corLdCoreContextP);
  if (corLdCoreVocab == NULL)
  {
    KT_E("Core context has no @vocab member — broker cannot expand unknown user terms. Refusing to start.");
    return -1;
  }
  corLdCoreVocabLen = (int) strlen(corLdCoreVocab);

  return 0;
}



// -----------------------------------------------------------------------------
//
// corLdCleanup -
//
void corLdCleanup(void)
{
  if (corLdInitialized == false)
    return;

  pthread_mutex_destroy(&corLdGlobalCache.mutex);
  memset(&corLdGlobalCache, 0, sizeof(corLdGlobalCache));
  corLdCoreContextP = NULL;
  corLdDownloadFn   = NULL;
  corLdInitialized  = false;
  corLdCoreVocab    = NULL;
  corLdCoreVocabLen = 0;
}
