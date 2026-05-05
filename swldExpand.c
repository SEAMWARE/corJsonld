//
// FILE            swldExpand.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strncmp, strchr, strlen

#include "ktrace/kTrace.h"                            // KT_E
#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "khash/khash.h"                             // khashItemLookup
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTExpand
#include "swJsonld/swldPrefixExpand.h"               // swldPrefixExpand
#include "swJsonld/swldInit.h"                       // swldCoreContext
#include "swJsonld/swldExpand.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// vocabExpandCheck - callback for name validation before @vocab expansion
//
static SwldVocabExpandCheck vocabExpandCheck = NULL;

void swldSetVocabExpandCheck(SwldVocabExpandCheck fn)
{
  vocabExpandCheck = fn;
}



// -----------------------------------------------------------------------------
//
// swldAlreadyExpanded -
//
bool swldAlreadyExpanded(const char* value)
{
  if (value == NULL)
    return false;
  if (strncmp(value, "urn:", 4) == 0)
    return true;
  if (strncmp(value, "http://", 7) == 0)
    return true;
  if (strncmp(value, "https://", 8) == 0)
    return true;

  return false;
}



// -----------------------------------------------------------------------------
//
// contextItemLookup - lookup a term by short name (handles array contexts).
//
// Used internally by swldExpand and externally by swldCompactTree (which
// needs to consult @container before recursing into a term's value).
//
SwldItem* contextItemLookup(SwldContext* contextP, const char* name)
{
  if (contextP == NULL)
    return NULL;

  // Older NGSI-LD core (recognised in swldContextFromUrl) — broker's own
  // core handles these terms.
  if (contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    //
    // Last-wins: iterate from last to first
    //
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      SwldItem* itemP = contextItemLookup(contextP->contextV[ix], name);

      if (itemP != NULL)
        return itemP;
    }

    return NULL;
  }

  if (contextP->nameHT == NULL)
    return NULL;

  return (SwldItem*) khashItemLookup(contextP->nameHT, name);
}



// -----------------------------------------------------------------------------
//
// contextVocab - get @vocab value (handles arrays)
//
static const char* contextVocab(SwldContext* contextP)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->ignored == true)
    return NULL;

  if (contextP->vocab != NULL)
    return contextP->vocab;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      const char* vocab = contextVocab(contextP->contextV[ix]);

      if (vocab != NULL)
        return vocab;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// swldExpand -
//
char* swldExpand(SwldContext* contextP, const char* name, KAlloc* kaP, SwldItem** itemPP, bool* coreContextP)
{
  if (itemPP != NULL)
    *itemPP = NULL;

  if (coreContextP != NULL)
    *coreContextP = false;

  if (name == NULL)
    return NULL;

  //
  // Step 1: Already expanded (urn:/http:/https:/)
  //
  if (swldAlreadyExpanded(name) == true)
    return (char*) name;

  //
  // Step 2: JSON-LD keyword (starts with @)
  //
  if (name[0] == '@')
    return (char*) name;

  //
  // Step 3: Prefix notation (contains ':')
  //
  if (strchr(name, ':') != NULL)
  {
    char* expanded = swldPrefixExpand(contextP, name, kaP);

    if (expanded != NULL)
      return expanded;

    //
    // Try core context prefix expansion
    //
    expanded = swldPrefixExpand(swldCoreContext(), name, kaP);
    if (expanded != NULL)
    {
      if (coreContextP != NULL)
        *coreContextP = true;
      return expanded;
    }
  }

  //
  // Step 4: Lookup in user context
  //
  SwldItem* itemP = contextItemLookup(contextP, name);

  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    //
    // User context may include the core context — set the coreContext flag
    // only when the user-context expansion *equals* the core-context one.
    // A user context that REMAPS a core term (e.g. binds "scope" to a
    // different IRI) must not be eligible for the keep-short shortcut.
    //
    if (coreContextP != NULL)
    {
      SwldItem* coreItemP = contextItemLookup(swldCoreContext(), name);
      if (coreItemP != NULL && coreItemP->id != NULL && itemP->id != NULL &&
          strcmp(coreItemP->id, itemP->id) == 0)
      {
        *coreContextP = true;
      }
    }

    return itemP->id;
  }

  //
  // Step 5: Lookup in core context
  //
  itemP = contextItemLookup(swldCoreContext(), name);
  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    if (coreContextP != NULL)
      *coreContextP = true;

    return itemP->id;
  }

  //
  // Step 6: @vocab fallback. Per JSON-LD: every term that's not a JSON-LD
  // keyword and not already an absolute IRI MUST be expanded. If no
  // context defines the term explicitly, the active context's @vocab is
  // used. NGSI-LD's invariant (and ours) is stronger: only the broker's
  // configured core context carries @vocab — user contexts MUST NOT — so
  // this branch is the unconditional final fallback for any short term
  // no context defined explicitly. The core's @vocab string never
  // changes once the broker is up, so cache it (and its length) on
  // first call to avoid a contextVocab() walk + strlen() per expand.
  //
  static const char* coreVocab    = NULL;
  static int         coreVocabLen = 0;

  if (coreVocab == NULL)
  {
    coreVocab = contextVocab(swldCoreContext());
    if (coreVocab == NULL)
    {
      // Broker init bug — core context built without @vocab. Returning
      // NULL surfaces the failure; silently passing the bare name through
      // would corrupt downstream matchers (DB, sub cache, distops) that
      // compare against fully-expanded IRIs.
      KT_E("swldExpand: core context has no @vocab — broker init bug; cannot expand '%s'", name);
      return NULL;
    }
    coreVocabLen = (int) strlen(coreVocab);
  }

  // The vocab-expand check is a defence-in-depth filter for ill-formed
  // names that should never reach this path; if it fires, return NULL
  // (rather than the bare name) so the caller sees the failure instead
  // of silently storing a non-IRI.
  if (vocabExpandCheck != NULL && vocabExpandCheck(name) == false)
    return NULL;

  int   nameLen  = (int) strlen(name);
  char* expanded = (char*) kaAlloc(kaP, coreVocabLen + nameLen + 1);

  if (expanded == NULL)
    return NULL;

  memcpy(expanded, coreVocab, coreVocabLen);
  memcpy(expanded + coreVocabLen, name, nameLen + 1);
  return expanded;
}
