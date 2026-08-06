//
// FILE            swldExpandTree.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool
#include <string.h>                                  // strcmp

#include "kjson/KjNode.h"                            // KjNode, KjObject
#include "kjson/kjLookup.h"                          // kjLookup
#include "kjson/kjBuilder.h"                         // kjChildRemove
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTExpand
#include "swJsonld/swldInit.h"                       // swldCoreContext
#include "swJsonld/swldExpand.h"                     // swldExpand
#include "swJsonld/swldContextParse.h"               // swldContextFromTree
#include "swJsonld/swldExpandTree.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// expandObject - recursively expand all names inside an object
//
static void expandObject(KjNode* objectP, SwldContext* contextP, KAlloc* kaP, int level)
{
  if (objectP == NULL || objectP->type != KjObject)
    return;

  for (KjNode* childP = objectP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;

    //
    // Skip @-prefixed names
    //
    if (childP->name[0] == '@')
      continue;

    //
    // Expand the name - but discard if it expands to an @-keyword
    //
    bool coreContext = false;
    SwldItem* termItemP = NULL;
    char* expanded = swldExpand(contextP, childP->name, kaP, &termItemP, &coreContext);

    // Copy the term's classification bits (KJF_*) from the matched context item
    // onto the node, so the broker tells structural members from sub-attributes
    // (and which value-key) with a bit test rather than a strcmp chain. Core
    // terms reached via prefix-expansion carry no item — flag them core-term.
    if (termItemP != NULL)
      childP->flags |= termItemP->flags;
    else if (coreContext)
      childP->flags |= KJF_CORE_TERM;

    // @container: "@language" / "@index" — the value's keys are opaque
    // (BCP-47 language tags or user-defined index strings), NOT terms.
    // Don't recurse into those subtrees with the term-expander.
    //
    // Same for the VALUE-position core terms (value / json / valueList):
    // a Property's value is plain JSON — its member names are NOT
    // vocabulary terms and q's "[...]" addresses them verbatim
    // (§ 4.5.2 / § 4.9). Expanding them rewrote {"c":{"d":7}} into
    // default-context IRized keys, unreachable by q=attr[c.d].
    int  vk         = (termItemP != NULL) ? KJF_VK_ID(termItemP->flags) : KJF_VK_NONE;
    bool opaqueKeys = (termItemP != NULL &&
                       ((termItemP->container & SWLD_CONTAINER_OPAQUE_KEYS) != 0 ||
                        vk == KJF_VK_VALUE || vk == KJF_VK_JSON || vk == KJF_VK_VALUELIST));

    if (expanded != NULL && expanded[0] != '@')
    {
      childP->name = expanded;

      //
      // A non-reified value-object — {"@type":X,"@value":Y} — carries a typed
      // literal directly (the shape a registration's "free" / non-spec
      // properties take after expansion). The term-binding branches below act
      // only on DIRECT primitive values, so an explicit value-object would
      // otherwise skip both @value validation and @vocab expansion. Validate Y
      // against its OWN @type via the same SwldValueCheck callback the primitive
      // path uses (mapping the NGSI-LD `DateTime` type onto xsd:dateTime so the
      // existing per-type checks apply), and @vocab-expand Y — KEEPING the
      // object node verbatim so a GET round-trips the submitted representation.
      //
      // Skip value-position members (a Property's value / json / valueList):
      // a value-object there is an attribute value, validated downstream by
      // ldCheckAttribute — not a free property of the enclosing resource.
      //
      if (opaqueKeys == false && childP->type == KjObject)
      {
        KjNode* atValueP = kjLookup(childP, "@value");

        if (atValueP != NULL)
        {
          KjNode*     atTypeP = kjLookup(childP, "@type");
          const char* voType  = (atTypeP != NULL && atTypeP->type == KjString) ? atTypeP->value.s : NULL;

          if (voType != NULL && strcmp(voType, "@vocab") == 0)
          {
            // @vocab — the @value is itself a vocab term (or array of them).
            if (atValueP->type == KjString && atValueP->value.s != NULL && atValueP->value.s[0] != '\0')
            {
              char* ev = swldExpand(contextP, atValueP->value.s, kaP, NULL, NULL);
              if (ev != NULL) atValueP->value.s = ev;
            }
            else if (atValueP->type == KjArray)
            {
              for (KjNode* elemP = atValueP->value.firstChildP; elemP != NULL; elemP = elemP->next)
                if (elemP->type == KjString && elemP->value.s != NULL && elemP->value.s[0] != '\0')
                {
                  char* ev = swldExpand(contextP, elemP->value.s, kaP, NULL, NULL);
                  if (ev != NULL) elemP->value.s = ev;
                }
            }
          }
          else if (voType != NULL && atValueP->type != KjObject && atValueP->type != KjArray)
          {
            SwldValueCheck vc = swldGetValueCheck();
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
        if (childP->type == KjString && childP->value.s != NULL && childP->value.s[0] != '\0')
        {
          char* ev = swldExpand(contextP, childP->value.s, kaP, NULL, NULL);
          if (ev != NULL) childP->value.s = ev;
        }
        else if (childP->type == KjArray)
        {
          for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
          {
            if (elemP->type == KjString && elemP->value.s != NULL && elemP->value.s[0] != '\0')
            {
              char* ev = swldExpand(contextP, elemP->value.s, kaP, NULL, NULL);
              if (ev != NULL) elemP->value.s = ev;
            }
          }
        }
      }
      //
      // Other @type:<datatype> bindings (xsd:dateTime, xsd:integer, …):
      // delegate to the registered SwldValueCheck callback so the broker
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
        SwldValueCheck vc = swldGetValueCheck();
        if (vc != NULL)
        {
          const char* shortName = (termItemP->name != NULL) ? termItemP->name : childP->name;
          if (childP->type == KjArray)
          {
            for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
            {
              if (elemP->type == KjObject || elemP->type == KjArray)
                continue;
              vc(shortName, termItemP->type, elemP);
            }
          }
          else if (childP->type != KjObject)
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
      if (childP->type == KjString)
      {
        char* expandedValue = swldExpand(contextP, childP->value.s, kaP, NULL, NULL);

        if (expandedValue != NULL)
          childP->value.s = expandedValue;
      }
      else if (childP->type == KjArray)
      {
        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString)
          {
            char* expandedValue = swldExpand(contextP, elemP->value.s, kaP, NULL, NULL);

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
    if (opaqueKeys)
      continue;

    if (childP->type == KjObject)
      expandObject(childP, contextP, kaP, level + 1);
    else if (childP->type == KjArray)
    {
      for (KjNode* itemP = childP->value.firstChildP; itemP != NULL; itemP = itemP->next)
        expandObject(itemP, contextP, kaP, level + 1);
    }
  }
}



// -----------------------------------------------------------------------------
//
// swldExpandTree -
//
// Two kinds of body are handled:
//
//   Single object ({ ... })
//     - Look for a root @context child. If present, parse it (string URL,
//       inline object, or array of either — swldContextFromTree handles all
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
// The returned SwldContext* is the representative context of the request
// (used downstream for link-header emit / response compaction). For an
// array body that's the first element's context; callers who care about
// per-element contexts must track them out-of-band.
//
// @context is ALWAYS stripped from the tree here so nothing past the HTTP
// boundary sees a stray @context (it would otherwise flow into service
// routines as if it were a user attribute — subtle stored-Property leak).
//
SwldContext* swldExpandTree(KjNode* treeP, SwldContext* userContextP, KAlloc* kaP)
{
  if (treeP == NULL)
    return NULL;

  if (userContextP == NULL)
    userContextP = swldCoreContext();

  if (treeP->type == KjObject)
  {
    KjNode*      atContextP = kjLookup(treeP, "@context");
    SwldContext* bodyCtxP   = NULL;

    if (atContextP != NULL)
    {
      bodyCtxP = swldContextFromTree(atContextP, kaP, NULL);   // Inline @context - no URL of its own, so no base
      kjChildRemove(treeP, atContextP);
    }

    SwldContext* contextP = (bodyCtxP != NULL) ? bodyCtxP : userContextP;
    if (contextP == NULL)
      return NULL;

    expandObject(treeP, contextP, kaP, 0);
    return contextP;
  }

  if (treeP->type == KjArray)
  {
    SwldContext* firstContextP = NULL;

    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
    {
      if (itemP->type != KjObject)
        continue;

      KjNode*      atContextP = kjLookup(itemP, "@context");
      SwldContext* elemCtxP   = NULL;

      if (atContextP != NULL)
      {
        elemCtxP = swldContextFromTree(atContextP, kaP, NULL);   // Inline @context - no URL of its own, so no base
        kjChildRemove(itemP, atContextP);
      }

      SwldContext* useCtx = (elemCtxP != NULL) ? elemCtxP : userContextP;
      if (useCtx == NULL)
        continue;

      expandObject(itemP, useCtx, kaP, 0);

      if (firstContextP == NULL)
        firstContextP = useCtx;
    }

    return (firstContextP != NULL) ? firstContextP : userContextP;
  }

  return NULL;
}
