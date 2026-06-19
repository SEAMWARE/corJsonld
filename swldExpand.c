//
// FILE            swldExpand.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strncmp, strchr, strlen

#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "khash/khash.h"                             // khashItemLookup
#include "kjson/KjNode.h"                            // KjNode
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
static SwldValueCheck       valueCheck       = NULL;

void swldSetValueCheck(SwldValueCheck fn)
{
  valueCheck = fn;
}

SwldValueCheck swldGetValueCheck(void)
{
  return valueCheck;
}

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
  if (value[0] == '@')                                 // JSON-LD keyword (@id, @type, @vocab, ...)
    return true;
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
const char* contextVocab(SwldContext* contextP)
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
  // Step 4: NGSI-LD invariant — core wins. The conceptual chain is
  // [user..., core]; the reverse-iterating walk starts at core, so a user
  // context can never shadow a core term (id, type, entities, notification,
  // ...). With coreContextRewriteToShort, core items return their short
  // name as id, so this step naturally produces the short form for any
  // core term.
  //
  SwldItem* itemP = contextItemLookup(swldCoreContext(), name);
  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    if (coreContextP != NULL)
      *coreContextP = true;

    return itemP->id;
  }

  //
  // Step 5: Lookup in the user context (reverse-iterated by
  // contextItemLookup for arrays). Only reached when core has nothing.
  //
  itemP = contextItemLookup(contextP, name);
  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    return itemP->id;
  }

  //
  // Step 6: @vocab fallback. Per JSON-LD: every term that's not a JSON-LD
  // keyword and not already an absolute IRI MUST be expanded. If no
  // context defines the term explicitly, the active context's @vocab is
  // used. NGSI-LD's invariant (and ours) is stronger: only the broker's
  // configured core context carries @vocab — user contexts MUST NOT — so
  // this branch is the unconditional final fallback for any short term
  // no context defined explicitly. swldCoreVocab is set once at init
  // (the broker refuses to start without it), so it's read directly
  // here — no per-call context walk.
  //

  // The vocab-expand check is a defence-in-depth filter for ill-formed
  // names that should never reach this path; if it fires, return NULL
  // (rather than the bare name) so the caller sees the failure instead
  // of silently storing a non-IRI.
  if (vocabExpandCheck != NULL && vocabExpandCheck(name) == false)
    return NULL;

  int   nameLen  = (int) strlen(name);
  char* expanded = (char*) kaAlloc(kaP, swldCoreVocabLen + nameLen + 1);

  if (expanded == NULL)
    return NULL;

  memcpy(expanded, swldCoreVocab, swldCoreVocabLen);
  memcpy(expanded + swldCoreVocabLen, name, nameLen + 1);
  return expanded;
}



// -----------------------------------------------------------------------------
//
// swldValueObjectIs - true if obj is a JSON-LD value object (has @value or @type)
//
bool swldValueObjectIs(KjNode* objP)
{
  if ((objP == NULL) || (objP->type != KjObject))
    return false;

  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;
    if ((strcmp(childP->name, "@value") == 0) || (strcmp(childP->name, "@type") == 0))
      return true;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// swldValueObjectCheck - validate the structure of a JSON-LD value object
//
// The caller has established that objP IS a value object (swldValueObjectIs).
// A value object accepts only `@value` (required) and `@type` (optional, a
// string). Any other member — plain or @-prefixed — is invalid. This is the
// JSON-LD-level structural rule; datatype/lexical validation of the `@value`
// against its `@type` is the application's concern (NGSI-LD, downstream).
// Duplicate `@value`/`@type` are already rejected upstream by the
// duplicate-member check, so they are not re-counted here.
//
// On violation returns false and points *detailP at a static reason string.
//
bool swldValueObjectCheck(KjNode* objP, char** detailP)
{
  KjNode* atValueP = NULL;
  KjNode* atTypeP  = NULL;

  for (KjNode* childP = objP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if      ((childP->name != NULL) && (strcmp(childP->name, "@value") == 0))  atValueP = childP;
    else if ((childP->name != NULL) && (strcmp(childP->name, "@type")  == 0))  atTypeP  = childP;
    else
    {
      *detailP = (char*) "a JSON-LD value object accepts only '@value' and '@type'";
      return false;
    }
  }

  if (atValueP == NULL)
  {
    *detailP = (char*) "a JSON-LD value object must have '@value'";
    return false;
  }

  if ((atTypeP != NULL) && (atTypeP->type != KjString))
  {
    *detailP = (char*) "the '@type' of a JSON-LD value object must be a string";
    return false;
  }

  return true;
}
