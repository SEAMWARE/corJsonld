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
#include "kalloc/kaAlloc.h"                           // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "khash/khash.h"                             // KHashTable, KHashListItem, khashItemAdd, khashItemLookup
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


// -----------------------------------------------------------------------------
//
// The PRISTINE core context — a second parse, never rewritten
//
// coreContextRewriteToShort() sets every core term's id to its own name, so the
// working core context knows NONE of its own IRIs. That is a good optimisation
// for the hot expand path and a trap for everything else: four separate places
// needed the real IRIs and each grew its own workaround —
//
//   - valueCompare() dereferences itemP->id at LOOKUP time, so the core
//     valueHT compares an IRI against a short name and never matches. That is
//     why corLdCompact's core reverse lookup was dead code, and why a core term
//     sent in its expanded spelling was unrecognised on input.
//   - corLdPrefixExpand() concatenated prefixItemP->id with the suffix, i.e.
//     the prefix NAME — `ngsi-ld:speed` was stored as `ngsi-ldspeed`, with a
//     201 and no error.
//   - the prefix snapshot below existed ONLY to capture ids before the rewrite.
//
// So: parse it twice. The rewritten copy stays exactly as it was; this one is
// never touched, and anything needing a real IRI asks it. Costs one extra parse
// of ~100 terms at init and nothing per request.
//
static CorLdContext* corLdCorePristineP = NULL;

static void coreContextClassifyFlags(CorLdContext* contextP);   // defined below


// -----------------------------------------------------------------------------
//
// corLdCorePristine -
//
CorLdContext* corLdCorePristine(void)
{
  return corLdCorePristineP;
}



// -----------------------------------------------------------------------------
//
// coreContextPristineBuild - the second parse
//
// Built from the context's own body, which both init paths keep (the embedded
// one points at the compiled-in string, a downloaded one at its copy), so this
// is one mechanism rather than one per path.
//
// Deliberately NOT cache-inserted and NOT given a body: it is an internal
// lookup table, and inserting it would collide with the real core context on
// the same URL.
//
static void coreContextPristineBuild(const char* bodyStr, KAlloc* kaP)
{
  if (bodyStr == NULL)
    return;

  char* body = strdup(bodyStr);      // kjParse is destructive

  if (body == NULL)
    return;

  Kjson   kjson;
  Kjson*  kjsonP = kjBufferCreate(&kjson, kaP);
  KjNode* treeP  = kjParse(kjsonP, body);

  if (treeP != NULL)
  {
    KjNode* atContextP = kjLookup(treeP, "@context");

    if (atContextP != NULL)
    {
      corLdCorePristineP = corLdContextFromObject(atContextP, kaP, CORLD_CORE_CONTEXT_URL);

      //
      // ⚠️ Classify it too. These items are handed OUT - corLdExpand returns one
      // as *itemPP and corLdExpandTree then ORs itemP->flags onto the node, which
      // is how a structural member is told from a sub-attribute. Unclassified
      // items OR in zero, so `observedAt` sent in its expanded spelling stopped
      // being structural and was normalized into a Property ("'observedAt' must
      // be a string"). Costs one pass over ~100 terms at init.
      //
      coreContextClassifyFlags(corLdCorePristineP);
    }
  }

  free(body);
}



// -----------------------------------------------------------------------------
//
// corLdCoreItemByIri - the core term a fully-expanded IRI names, or NULL
//
// A plain reverse lookup in the pristine copy, where valueHT's keys and
// itemP->id agree because nothing flattened them. This used to be a dedicated
// pre-rewrite hash table; the pristine context makes it an ordinary lookup.
//
static struct CorLdItem* pristineReverseLookup(CorLdContext* contextP, const char* iri)
{
  if (contextP == NULL || contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      struct CorLdItem* itemP = pristineReverseLookup(contextP->contextV[ix], iri);

      if (itemP != NULL)
        return itemP;
    }

    return NULL;
  }

  if (contextP->valueHT == NULL)
    return NULL;

  return (struct CorLdItem*) khashItemLookup(contextP->valueHT, iri);
}


struct CorLdItem* corLdCoreItemByIri(const char* iri)
{
  if (iri == NULL)
    return NULL;

  return pristineReverseLookup(corLdCorePristineP, iri);
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
// coreNameTable - the context of the core that holds the terms (the core is one object context)
//
static CorLdContext* coreNameTable(CorLdContext* contextP)
{
  while ((contextP != NULL) && (contextP->isArray == true))
    contextP = (contextP->contexts > 0) ? contextP->contextV[0] : NULL;

  return ((contextP != NULL) && (contextP->nameHT != NULL)) ? contextP : NULL;
}



// -----------------------------------------------------------------------------
//
// corLdCoreTermsAdd -
//
int corLdCoreTermsAdd(const CorLdCoreTerm* termV, KAlloc* kaP)
{
  CorLdContext* coreP = coreNameTable(corLdCoreContextP);

  if (coreP == NULL)
    return -1;

  int added = 0;

  for (const CorLdCoreTerm* tP = termV; (tP != NULL) && (tP->name != NULL); tP++)
  {
    if (khashItemLookup(coreP->nameHT, tP->name) != NULL)
      continue;   // the core has it already - its own definition stands

    CorLdItem* itemP = (CorLdItem*) kaAlloc(kaP, sizeof(CorLdItem));

    if (itemP == NULL)
      return -1;

    memset(itemP, 0, sizeof(CorLdItem));

    //
    // Born in the form coreContextRewriteToShort gives every core term: id = name,
    // so it expands to itself and is stored, matched and rendered short.
    //
    itemP->name      = kaStrdup(kaP, tP->name);
    itemP->id        = itemP->name;
    itemP->type      = (tP->type != NULL) ? kaStrdup(kaP, tP->type) : NULL;
    itemP->container = CorLdContainerNone;
    itemP->flags     = coreTermFlags(itemP->name);

    khashItemAdd(coreP->nameHT,  itemP->name, itemP);
    khashItemAdd(coreP->valueHT, itemP->id,   itemP);
    ++added;
  }

  return added;
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
    // The second, un-rewritten parse. Everything that needs a real core IRI
    // reads it from here — see the comment on corLdCorePristineP.
    coreContextPristineBuild(corLdCoreContextBody, kaP);

    // Prefix snapshot for corLdCompact's longest-match scan. Taken from the
    // PRISTINE copy now, so it is an ordinary derived cache rather than
    // something that has to happen before the rewrite.
    coreContextPrefixSnapshot((corLdCorePristineP != NULL) ? corLdCorePristineP : contextP);

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

    coreContextPristineBuild(corLdCoreContextP->body, kaP);
    coreContextPrefixSnapshot((corLdCorePristineP != NULL) ? corLdCorePristineP : corLdCoreContextP);
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
