//
// FILE            corLdExpandTree.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdbool.h>                                 // bool
#include <string.h>                                  // strcmp, strlen, memcpy

#include "corAlloc/corAlloc.h"                       // corAlloc
#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corTree/CorNode.h"                         // CorNode, CorObject
#include "corTree/corTreeLookup.h"                   // corTreeLookup
#include "corTree/corTreeBuilder.h"                  // corTreeChildRemove
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTExpand
#include "corJsonld/corLdInit.h"                       // corLdCoreContext, corLdCoreItemById
#include "corJsonld/corLdExpand.h"                     // corLdExpand
#include "corJsonld/corLdContextParse.h"               // corLdContextFromTree
#include "corJsonld/corLdExpandTree.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// expandObject - recursively expand all names inside an object
//
// -----------------------------------------------------------------------------
//
// vocabValueExpand - expand one value of an @type:@vocab term
//
// The part before a registrant-declared suffix (CorLdVocabValueSuffix) is the
// term; the suffix is appended back verbatim.
//
static char* vocabValueExpand(CorLdContext* contextP, CorLdItem* termItemP, char* value, CorAlloc* kaP)
{
  CorLdVocabValueSuffix suffixFn = corLdGetVocabValueSuffix();
  int                   ix       = (suffixFn != NULL) ? suffixFn(termItemP->name, value) : -1;

  if (ix <= 0)
    return corLdExpand(contextP, value, kaP, NULL, NULL);

  char* termP = (char*) corAlloc(kaP, ix + 1);
  memcpy(termP, value, ix);
  termP[ix] = 0;

  char* expandedP = corLdExpand(contextP, termP, kaP, NULL, NULL);
  if (expandedP == NULL)
    return NULL;

  int   expandedLen = strlen(expandedP);
  int   suffixLen   = strlen(&value[ix]);
  char* outP        = (char*) corAlloc(kaP, expandedLen + suffixLen + 1);

  memcpy(outP, expandedP, expandedLen);
  memcpy(&outP[expandedLen], &value[ix], suffixLen + 1);

  return outP;
}



// -----------------------------------------------------------------------------
//
// isJsonLiteral - does this object declare itself JSON, not JSON-LD? ("@type": "@json")
//
// JSON-LD's JSON literal, { "@type": "@json", "@value": <any JSON> }. Its content
// is somebody else's JSON: keys that are not terms, that may not even satisfy
// the term grammar - expanding them is wrong, and can make valid JSON fail as
// a bad name.
//
static bool isJsonLiteral(CorNode* objectP)
{
  if ((objectP == NULL) || (objectP->type != CorObject))
    return false;

  CorNode* typeP = corTreeLookup(objectP, "@type");

  return ((typeP != NULL) && (typeP->type == CorString) && (strcmp(typeP->value.s, "@json") == 0));
}



static void expandObject(CorNode* objectP, CorLdContext* contextP, CorAlloc* kaP, int level, bool inValue);



// -----------------------------------------------------------------------------
//
// expandArray - expand the objects in an array, at any depth
//
// Arrays nest (a ListProperty's valueList may hold arrays of objects), and an object
// two arrays down is as much a node as one directly inside.
//
static void expandArray(CorNode* arrayP, CorLdContext* contextP, CorAlloc* kaP, int level, bool inValue)
{
  for (CorNode* itemP = arrayP->value.head; itemP != NULL; itemP = itemP->next)
  {
    if (itemP->type == CorObject)
      expandObject(itemP, contextP, kaP, level, inValue);
    else if (itemP->type == CorArray)
      expandArray(itemP, contextP, kaP, level, inValue);
  }
}



// -----------------------------------------------------------------------------
//
// inValue: objectP is (inside) a compound value - a Property's value, a ListProperty's
// valueList, a LanguageProperty's languageMap. Its member names are expanded but not
// held to the NGSI-LD name grammar (corLdExpandValueKey), and an @-name is kept as it
// is, unchecked - it is somebody's JSON, not an NGSI-LD name.
//
static void expandObject(CorNode* objectP, CorLdContext* contextP, CorAlloc* kaP, int level, bool inValue)
{
  if (objectP == NULL || objectP->type != CorObject)
    return;

  if (isJsonLiteral(objectP) == true)
    return;   // taken verbatim - also as an element of an array

  CorLdKeywordCheck keywordCheckP = corLdGetKeywordCheck();

  for (CorNode* childP = objectP->value.head; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;

    //
    // Skip @-prefixed names - a JSON-LD keyword is not a term, it neither
    // expands nor carries a term subtree to descend into.
    //
    // But ONLY a real keyword. A name that merely looks like one ("@referredType")
    // is not a keyword, so skipping it here left its whole subtree unexpanded and
    // unmarked: the NGSI-LD layer then saw an attribute whose "type"/"value"
    // members carried no KJF_ATTR_TERM bit and reified them as sub-attributes,
    // turning a valid Property into {"type":{"type":"Property","value":"Property"}}.
    // Hand it to the keyword-check callback so the caller can reject it.
    //
    if (childP->name[0] == '@')
    {
      if (inValue == true)
        continue;

      if (corLdKeywordIs(childP->name) == false)
      {
        if ((keywordCheckP != NULL) && (keywordCheckP(childP->name) == false))
          return;
      }

      continue;
    }

    //
    // Expand the name - but discard if it expands to an @-keyword
    //
    bool coreContext = false;
    CorLdItem* termItemP = NULL;
    //
    // A node the parser already stamped as a core term (corJson key hook) needs no
    // expansion: its item is an array index away, and core terms always win, so it is
    // the item corLdExpand would find. Everything else is expanded as always.
    //
    CorLdItem* stampedP = corLdCoreItemById(childP->termId);
    char*      expanded;

    if (stampedP != NULL)
    {
      termItemP   = stampedP;
      coreContext = true;
      expanded    = stampedP->id;
    }
    else if (inValue == true)
      expanded = corLdExpandValueKey(contextP, childP->name, kaP, &termItemP, &coreContext);
    else
      expanded = corLdExpand(contextP, childP->name, kaP, &termItemP, &coreContext);

    // Copy the term's classification bits (KJF_*) from the matched context item
    // onto the node, so the broker tells structural members from sub-attributes
    // (and which value-key) with a bit test rather than a strcmp chain. Core
    // terms reached via prefix-expansion carry no item — flag them core-term.
    if (termItemP != NULL)
    {
      childP->flags  |= termItemP->flags;

      if (termItemP->termId != 0)                  // a USER item has none - keep what the parser stamped
        childP->termId = termItemP->termId;
    }
    else if (coreContext)
      childP->flags |= KJF_CORE_TERM;

    // @container: "@language" / "@index" — the value's keys are opaque
    // (BCP-47 language tags or user-defined index strings), NOT terms.
    // Don't recurse into those subtrees with the term-expander.
    //
    // Same for anything typed @json - by its term (the core's own "json", or
    // "bridgeOptions": {"@type": "@json"} in a context) or by itself (a JSON
    // literal, { "@type": "@json", "@value": ... }): JSON, not JSON-LD, so taken
    // verbatim.
    //
    // ⚠ A Property's `value` (and a ListProperty's `valueList`) is NOT opaque.
    // The core context defines "value" as a plain term (ngsi-ld:hasValue) and
    // "valueList" as an @list - JSON-LD, so the member names of a compound value
    // are terms and are expanded like any other. TS 104 175 clause 7 says the same
    // from the query side: a q attribute path, its bracketed trailing path
    // included, "is always a composition of short hand names" with an @context
    // "properly defining all the terms", and its Example 11 addresses
    // rawdata[airquality.particulate] - keys INSIDE a value - as such terms.
    // Treating value as opaque also stored one compound value in two forms: short
    // keys from normalized input, expanded ones from simplified input (where
    // nothing marks the value as one).
    //
    int  vk            = (termItemP != NULL) ? KJF_VK_ID(termItemP->flags) : KJF_VK_NONE;
    bool jsonTyped     = (termItemP != NULL) && (termItemP->type != NULL) && (strcmp(termItemP->type, "@json") == 0);
    bool valuePosition = (vk == KJF_VK_VALUE) || (vk == KJF_VK_JSON) || (vk == KJF_VK_VALUELIST);
    bool opaqueKeys    = (termItemP != NULL &&
                          ((termItemP->container & CORLD_CONTAINER_OPAQUE_KEYS) != 0 || vk == KJF_VK_JSON)) ||
                         jsonTyped || isJsonLiteral(childP);

    if (expanded != NULL && expanded[0] != '@')
    {
      childP->name = expanded;

      //
      // A non-reified value-object — {"@type":X,"@value":Y} — carries a typed
      // literal directly (the shape a registration's "free" / non-spec
      // properties take after expansion). The term-binding branches below act
      // only on DIRECT primitive values, so an explicit value-object would
      // otherwise skip both @value validation and @vocab expansion. Validate Y
      // against its OWN @type via the same CorLdValueCheck callback the primitive
      // path uses (mapping the NGSI-LD `DateTime` type onto xsd:dateTime so the
      // existing per-type checks apply), and @vocab-expand Y — KEEPING the
      // object node verbatim so a GET round-trips the submitted representation.
      //
      // Skip value-position members (a Property's value / json / valueList):
      // a value-object there is an attribute value, validated downstream by
      // ldCheckAttribute — not a free property of the enclosing resource.
      //
      if ((opaqueKeys == false) && (valuePosition == false) && (childP->type == CorObject))
      {
        CorNode* atValueP = corTreeLookup(childP, "@value");

        if (atValueP != NULL)
        {
          CorNode*    atTypeP = corTreeLookup(childP, "@type");
          const char* voType  = (atTypeP != NULL && atTypeP->type == CorString) ? atTypeP->value.s : NULL;

          if (voType != NULL && strcmp(voType, "@vocab") == 0)
          {
            // @vocab — the @value is itself a vocab term (or array of them).
            if (atValueP->type == CorString && atValueP->value.s != NULL && atValueP->value.s[0] != '\0')
            {
              char* ev = corLdExpand(contextP, atValueP->value.s, kaP, NULL, NULL);
              if (ev != NULL) atValueP->value.s = ev;
            }
            else if (atValueP->type == CorArray)
            {
              for (CorNode* elemP = atValueP->value.head; elemP != NULL; elemP = elemP->next)
                if (elemP->type == CorString && elemP->value.s != NULL && elemP->value.s[0] != '\0')
                {
                  char* ev = corLdExpand(contextP, elemP->value.s, kaP, NULL, NULL);
                  if (ev != NULL) elemP->value.s = ev;
                }
            }
          }
          else if (voType != NULL && atValueP->type != CorObject && atValueP->type != CorArray)
          {
            CorLdValueCheck vc = corLdGetValueCheck();
            if (vc != NULL)
            {
              const char* shortName = (termItemP != NULL && termItemP->name != NULL) ? termItemP->name : childP->name;
              const char* dt        = voType;

              // NGSI-LD `DateTime` is the core temporal type (the core context
              // maps it to ngsi-ld:DateTime); route it through the xsd:dateTime
              // check so a free DateTime property is validated like any other.
              if ((strcmp(voType, "DateTime") == 0) ||
                  (strcmp(voType, "https://uri.etsi.org/ngsi-ld/DateTime") == 0))
                dt = "xsd:dateTime";

              vc(shortName, dt, atValueP);
            }
          }
        }
      }

      //
      // § 4.5.x / JSON-LD — when the term carries `@type: @vocab`, its
      // string values are themselves vocab terms and must be expanded
      // through the active @context. Core-context terms that need this:
      // propertyNames, relationshipNames, watchedAttributes,
      // attributeList, typeNames, objectType, vocab, etc. Without this,
      // CSR.propertyNames=["name"] persists short while a query
      // `?attrs=name` expands to the IRI form — match fails and the
      // CSR isn't found.
      //
      // @type:@id is intentionally NOT coerced — those values are
      // contracted to be IRIs already; @vocab-expanding a bare-name
      // input would launder it into a syntactically-valid IRI past
      // the URI validator (e.g. datasetId="not-a-uri").
      //
      if (termItemP != NULL &&
          termItemP->type != NULL &&
          strcmp(termItemP->type, "@vocab") == 0)
      {
        // Empty strings and other null-meaning values are NOT vocab terms
        // — let the post-expansion shape validators see the original so
        // they can reject. Without this guard the empty string gets
        // prefixed by @vocab and silently passes URI-shape checks.
        if (childP->type == CorString && childP->value.s != NULL && childP->value.s[0] != '\0')
        {
          char* ev = vocabValueExpand(contextP, termItemP, childP->value.s, kaP);
          if (ev != NULL) childP->value.s = ev;
        }
        else if (childP->type == CorArray)
        {
          for (CorNode* elemP = childP->value.head; elemP != NULL; elemP = elemP->next)
          {
            if (elemP->type == CorString && elemP->value.s != NULL && elemP->value.s[0] != '\0')
            {
              char* ev = vocabValueExpand(contextP, termItemP, elemP->value.s, kaP);
              if (ev != NULL) elemP->value.s = ev;
            }
          }
        }
      }
      //
      // Other @type:<datatype> bindings (xsd:dateTime, xsd:integer, …):
      // delegate to the registered CorLdValueCheck callback so the broker
      // can reject ill-formed values at expansion time (§ 4.3 JSON-LD).
      // @id values are deliberately NOT routed here — coercing a bare
      // name to an IRI would launder it past downstream URI validators.
      //
      // JSON-LD @type only applies to DIRECT primitive value positions.
      // NGSI-LD wraps attribute values in `{type, value, …}` objects, so
      // an Object-typed child is not the position the binding talks
      // about — the validation belongs to ldCheckAttribute downstream.
      //
      else if (termItemP != NULL &&
               termItemP->type != NULL &&
               strcmp(termItemP->type, "@id") != 0)
      {
        CorLdValueCheck vc = corLdGetValueCheck();
        if (vc != NULL)
        {
          const char* shortName = (termItemP->name != NULL) ? termItemP->name : childP->name;
          if (childP->type == CorArray)
          {
            for (CorNode* elemP = childP->value.head; elemP != NULL; elemP = elemP->next)
            {
              if (elemP->type == CorObject || elemP->type == CorArray)
                continue;
              vc(shortName, termItemP->type, elemP);
            }
          }
          else if (childP->type != CorObject)
          {
            vc(shortName, termItemP->type, childP);
          }
        }
      }
    }
    else if (expanded != NULL && strcmp(expanded, "@type") == 0)
    {
      //
      // "type" maps to "@type" - don't rename the key, but expand the string value.
      // (Its KJF_* bits are already copied from the context item above — "type"
      // is a normal core item ("type":"@type") like any other @type alias.)
      //
      if (childP->type == CorString)
      {
        char* expandedValue = corLdExpand(contextP, childP->value.s, kaP, NULL, NULL);

        if (expandedValue != NULL)
          childP->value.s = expandedValue;
      }
      else if (childP->type == CorArray)
      {
        for (CorNode* elemP = childP->value.head; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == CorString)
          {
            char* expandedValue = corLdExpand(contextP, elemP->value.s, kaP, NULL, NULL);

            if (expandedValue != NULL)
              elemP->value.s = expandedValue;
          }
        }
      }
    }

    //
    // Recurse into sub-objects and arrays of objects.
    // Skip recursion when the term's container marks the inner keys as
    // opaque (see @container handling above).
    //
    //
    // An @index map (a simplified multi-attribute's "dataset", keyed by datasetId):
    // its KEYS are index strings and stay as they are, but each VALUE is an ordinary
    // node whose members are expanded like any other.
    //
    if ((termItemP != NULL) && ((termItemP->container & CorLdContainerIndex) != 0) && (childP->type == CorObject))
    {
      for (CorNode* indexedP = childP->value.head; indexedP != NULL; indexedP = indexedP->next)
      {
        if (indexedP->type == CorObject)
          expandObject(indexedP, contextP, kaP, level + 1, inValue);
      }
      continue;
    }

    if (opaqueKeys)
      continue;

    bool childInValue = (inValue == true) || (valuePosition == true) ||
                        ((termItemP != NULL) && ((termItemP->container & CorLdContainerLanguage) != 0));

    if (childP->type == CorObject)
      expandObject(childP, contextP, kaP, level + 1, childInValue);
    else if (childP->type == CorArray)
      expandArray(childP, contextP, kaP, level + 1, childInValue);
  }
}



// -----------------------------------------------------------------------------
//
// corLdExpandTree -
//
// Two kinds of body are handled:
//
//   Single object ({ ... })
//     - Look for a root @context child. If present, parse it (string URL,
//       inline object, or array of either — corLdContextFromTree handles all
//       three), use it to expand child names, and remove it from the tree.
//
//   Array body ([ ... , ... ]) — i.e. a batch op
//     - JSON arrays can't carry a root @context. Per ld+json array semantics
//       each element carries its own @context. For every object element
//       extract+parse+strip+expand independently; non-object elements are
//       left alone (batch-delete takes a list of id strings, so rejecting
//       here would break it — the entity-batch service routines that DO
//       require objects validate that for themselves).
//
// The returned CorLdContext* is the representative context of the request
// (used downstream for link-header emit / response compaction). For an
// array body that's the first element's context; callers who care about
// per-element contexts must track them out-of-band.
//
// @context is ALWAYS stripped from the tree here so nothing past the HTTP
// boundary sees a stray @context (it would otherwise flow into service
// routines as if it were a user attribute — subtle stored-Property leak).
//
CorLdContext* corLdExpandTree(CorNode* treeP, CorLdContext* userContextP, CorAlloc* kaP)
{
  if (treeP == NULL)
    return NULL;

  if (userContextP == NULL)
    userContextP = corLdCoreContext();

  if (treeP->type == CorObject)
  {
    CorNode*     atContextP = corTreeLookup(treeP, "@context");
    CorLdContext* bodyCtxP   = NULL;

    if (atContextP != NULL)
    {
      bodyCtxP = corLdContextFromTree(atContextP, kaP, NULL);   // Inline @context - no URL of its own, so no base
      corTreeChildRemove(treeP, atContextP);
    }

    CorLdContext* contextP = (bodyCtxP != NULL) ? bodyCtxP : userContextP;
    if (contextP == NULL)
      return NULL;

    expandObject(treeP, contextP, kaP, 0, false);
    return contextP;
  }

  if (treeP->type == CorArray)
  {
    CorLdContext* firstContextP = NULL;

    for (CorNode* itemP = treeP->value.head; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->type != CorObject)
        continue;

      CorNode*     atContextP = corTreeLookup(itemP, "@context");
      CorLdContext* elemCtxP   = NULL;

      if (atContextP != NULL)
      {
        elemCtxP = corLdContextFromTree(atContextP, kaP, NULL);   // Inline @context - no URL of its own, so no base
        corTreeChildRemove(itemP, atContextP);
      }

      CorLdContext* useCtx = (elemCtxP != NULL) ? elemCtxP : userContextP;
      if (useCtx == NULL)
        continue;

      expandObject(itemP, useCtx, kaP, 0, false);

      if (firstContextP == NULL)
        firstContextP = useCtx;
    }

    return (firstContextP != NULL) ? firstContextP : userContextP;
  }

  return NULL;
}
