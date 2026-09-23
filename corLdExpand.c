//
// FILE            corLdExpand.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strncmp, strchr, strlen

#include "kalloc/kaAlloc.h"                          // kaAlloc
#include "kalloc/kaStrdup.h"                         // kaStrdup
#include "khash/khash.h"                             // khashItemLookup
#include "kjson/KjNode.h"                            // KjNode
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTExpand
#include "corJsonld/corLdPrefixExpand.h"               // corLdPrefixExpand
#include "corJsonld/corLdInit.h"                       // corLdCoreContext
#include "corJsonld/corLdExpand.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// vocabExpandCheck - callback for name validation before @vocab expansion
//
static CorLdVocabExpandCheck vocabExpandCheck = NULL;
static CorLdValueCheck       valueCheck       = NULL;
static CorLdKeywordCheck     keywordCheck     = NULL;
static CorLdVocabValueSuffix vocabValueSuffix = NULL;

void corLdSetVocabValueSuffix(CorLdVocabValueSuffix fn)
{
  vocabValueSuffix = fn;
}

CorLdVocabValueSuffix corLdGetVocabValueSuffix(void)
{
  return vocabValueSuffix;
}

void corLdSetValueCheck(CorLdValueCheck fn)
{
  valueCheck = fn;
}

CorLdValueCheck corLdGetValueCheck(void)
{
  return valueCheck;
}

void corLdSetVocabExpandCheck(CorLdVocabExpandCheck fn)
{
  vocabExpandCheck = fn;
}


void corLdSetKeywordCheck(CorLdKeywordCheck fn)
{
  keywordCheck = fn;
}

CorLdKeywordCheck corLdGetKeywordCheck(void)
{
  return keywordCheck;
}



// -----------------------------------------------------------------------------
//
// corLdKeywordIs -
//
// The complete JSON-LD 1.1 keyword list (§ 1.7 "Syntax Tokens and Keywords").
// Grouped by second character so a non-keyword costs one switch and at most a
// couple of strcmps - this runs once per member name of every object in every
// request body.
//
bool corLdKeywordIs(const char* name)
{
  if ((name == NULL) || (name[0] != '@'))
    return false;

  switch (name[1])
  {
  case 'b':  return (strcmp(name, "@base")      == 0);
  case 'c':  return (strcmp(name, "@container") == 0) || (strcmp(name, "@context") == 0);
  case 'd':  return (strcmp(name, "@direction") == 0);
  case 'g':  return (strcmp(name, "@graph")     == 0);
  case 'i':  return (strcmp(name, "@id")        == 0) || (strcmp(name, "@import")   == 0) ||
                    (strcmp(name, "@included")  == 0) || (strcmp(name, "@index")    == 0);
  case 'j':  return (strcmp(name, "@json")      == 0);
  case 'l':  return (strcmp(name, "@language")  == 0) || (strcmp(name, "@list")     == 0);
  case 'n':  return (strcmp(name, "@nest")      == 0) || (strcmp(name, "@none")     == 0);
  case 'p':  return (strcmp(name, "@prefix")    == 0) || (strcmp(name, "@propagate") == 0) ||
                    (strcmp(name, "@protected") == 0);
  case 'r':  return (strcmp(name, "@reverse")   == 0);
  case 's':  return (strcmp(name, "@set")       == 0);
  case 't':  return (strcmp(name, "@type")      == 0);
  case 'v':  return (strcmp(name, "@value")     == 0) || (strcmp(name, "@version")  == 0) ||
                    (strcmp(name, "@vocab")     == 0);
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// corLdAlreadyExpanded -
//
bool corLdAlreadyExpanded(const char* value)
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
// Used internally by corLdExpand and externally by corLdCompactTree (which
// needs to consult @container before recursing into a term's value).
//
CorLdItem* contextItemLookup(CorLdContext* contextP, const char* name)
{
  if (contextP == NULL)
    return NULL;

  // Older NGSI-LD core (recognised in corLdContextFromUrl) — broker's own
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
      CorLdItem* itemP = contextItemLookup(contextP->contextV[ix], name);

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
// contextVocab - get @vocab value (handles arrays)
//
const char* contextVocab(CorLdContext* contextP)
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
// corLdExpand -
//
char* corLdExpand(CorLdContext* contextP, const char* name, KAlloc* kaP, CorLdItem** itemPP, bool* coreContextP)
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
  // ... but a CORE term may arrive in either spelling. After JSON-LD expansion
  // `observedAt` and `https://uri.etsi.org/ngsi-ld/observedAt` are the same
  // thing, and TS 104-175 § 4.3.4.2 only ALLOWS the short form - it does not
  // mandate it, and nothing in the spec forbids the long one. coraine's
  // canonical internal form for a core term is the SHORT name
  // (coreContextRewriteToShort), so the long spelling must land there too, or
  // the two diverge: `observedAt` sent expanded used to become an ordinary user
  // Property, and an attribute `type` sent expanded produced TWO `type` members
  // in one object - invalid JSON on the wire.
  //
  // This is the "compact via the core context" half of the rule, and it costs
  // nothing on the hot path: a short name is never `alreadyExpanded`, so only
  // an IRI pays the lookup, and that lookup is one hash probe.
  //
  // Deliberately NOT a prefix test on "https://uri.etsi.org/ngsi-ld/". That was
  // tried once and pulled, because it also swallowed the @vocab expansions -
  // a `Poinxt` geometry arriving as `.../default-context/Poinxt` was stored as
  // junk instead of being rejected. corLdCoreItemByIri holds term definitions
  // ONLY, so a @vocab expansion can never match it.
  //
  if (corLdAlreadyExpanded(name) == true)
  {
    CorLdItem* coreItemP = corLdCoreItemByIri(name);

    if (coreItemP != NULL)
    {
      if (itemPP != NULL)
        *itemPP = coreItemP;

      if (coreContextP != NULL)
        *coreContextP = true;

      return (char*) coreItemP->name;
    }

    return (char*) name;
  }

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
    char* expanded = corLdPrefixExpand(contextP, name, kaP);

    if (expanded == NULL)
    {
      //
      // Try core context prefix expansion
      //
      expanded = corLdPrefixExpand(corLdCoreContext(), name, kaP);

      if ((expanded != NULL) && (coreContextP != NULL))
        *coreContextP = true;
    }

    if (expanded != NULL)
    {
      //
      // Prefix expansion produced an IRI - which may be a CORE term's IRI, and
      // then the canonical internal form is the SHORT name, exactly as for the
      // same term written out in full (see step 1). `ngsi-ld:Property` is the
      // same thing as `Property` and as
      // `https://uri.etsi.org/ngsi-ld/Property`; without this it alone came out
      // as the IRI, so three legal spellings of one type produced two stored
      // forms.
      //
      CorLdItem* coreItemP = corLdCoreItemByIri(expanded);

      if (coreItemP != NULL)
      {
        if (itemPP != NULL)
          *itemPP = coreItemP;

        if (coreContextP != NULL)
          *coreContextP = true;

        return (char*) coreItemP->name;
      }

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
  CorLdItem* itemP = contextItemLookup(corLdCoreContext(), name);
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
  // no context defined explicitly. corLdCoreVocab is set once at init
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
  char* expanded = (char*) kaAlloc(kaP, corLdCoreVocabLen + nameLen + 1);

  if (expanded == NULL)
    return NULL;

  memcpy(expanded, corLdCoreVocab, corLdCoreVocabLen);
  memcpy(expanded + corLdCoreVocabLen, name, nameLen + 1);
  return expanded;
}



// -----------------------------------------------------------------------------
//
// corLdValueObjectIs - true if obj is a JSON-LD value object (has @value or @type)
//
bool corLdValueObjectIs(KjNode* objP)
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
// corLdValueObjectCheck - validate the structure of a JSON-LD value object
//
// The caller has established that objP IS a value object (corLdValueObjectIs).
// A value object accepts only `@value` (required) and `@type` (optional, a
// string). Any other member — plain or @-prefixed — is invalid. This is the
// JSON-LD-level structural rule; datatype/lexical validation of the `@value`
// against its `@type` is the application's concern (NGSI-LD, downstream).
// Duplicate `@value`/`@type` are already rejected upstream by the
// duplicate-member check, so they are not re-counted here.
//
// On violation returns false and points *detailP at a static reason string.
//
bool corLdValueObjectCheck(KjNode* objP, char** detailP)
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
