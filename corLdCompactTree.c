//
// FILE            corLdCompactTree.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                  // strcmp

#include "kjson/KjNode.h"                            // KjNode, KjObject, KjArray
#include "corJsonld/CorLdItem.h"                       // CorLdItem, CorLdContainer*
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTCompact
#include "corJsonld/corLdInit.h"                       // corLdCoreContext
#include "corJsonld/corLdCompact.h"                    // corLdCompact
#include "corJsonld/corLdExpand.h"                      // contextItemLookup
#include "corJsonld/corLdCompactTree.h"                // Own interface



// -----------------------------------------------------------------------------
//
// compactObject - recursively compact all names inside an object
//
static void compactObject(KjNode* objectP, CorLdContext* coreP, int level)
{
  if (objectP == NULL || objectP->type != KjObject)
    return;

  for (KjNode* childP = objectP->value.firstChildP; childP != NULL; childP = childP->next)
  {
    if (childP->name == NULL)
      continue;

    //
    // Skip @-prefixed names - JSON-LD directives, not data
    //
    if (childP->name[0] == '@')
      continue;

    //
    // Compact the name
    //
    const char* compacted = corLdCompact(coreP, childP->name);

    if (compacted != NULL)
      childP->name = (char*) compacted;

    // @container: "@language" / "@index" — value's keys are opaque (BCP-47
    // language tags or user-defined index strings). Never reverse-map them.
    //
    // Look up in the request context first (the ETSI test-suite context
    // and other user contexts may override or define terms). Fall back
    // to core, which carries the canonical NGSI-LD term metadata
    // (@type, @container) that user contexts inherit by reference.
    CorLdItem* termItemP = contextItemLookup(coreP, childP->name);
    if (termItemP == NULL)
      termItemP = contextItemLookup(corLdCoreContext(), childP->name);
    bool opaqueKeys = (termItemP != NULL &&
                       (termItemP->container & CORLD_CONTAINER_OPAQUE_KEYS) != 0);

    //
    // For "type" fields at entity level, also compact the string value (e.g. full URI -> "Vehicle")
    // At attribute level (level 1+), "type" values are NGSI-LD keywords, not expanded URIs.
    //
    if (strcmp(childP->name, "type") == 0)
    {
      if (childP->type == KjString)
      {
        const char* compactedValue = corLdCompact(coreP, childP->value.s);

        if (compactedValue != NULL)
          childP->value.s = (char*) compactedValue;
      }
      else if (childP->type == KjArray)
      {
        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString)
          {
            const char* compactedValue = corLdCompact(coreP, elemP->value.s);

            if (compactedValue != NULL)
              elemP->value.s = (char*) compactedValue;
          }
        }
      }
    }
    //
    // Mirror of the expansion-side @type:@vocab handling: terms whose
    // values were vocab-expanded must be vocab-compacted on the way out
    // so the wire shape uses the response @context's short forms.
    // Examples: propertyNames, relationshipNames, watchedAttributes,
    // attributeList, typeNames, objectType, vocab.
    //
    else if (termItemP != NULL &&
             termItemP->type != NULL &&
             strcmp(termItemP->type, "@vocab") == 0)
    {
      if (childP->type == KjString)
      {
        const char* cv = corLdCompact(coreP, childP->value.s);
        if (cv != NULL) childP->value.s = (char*) cv;
      }
      else if (childP->type == KjArray)
      {
        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString)
          {
            const char* cv = corLdCompact(coreP, elemP->value.s);
            if (cv != NULL) elemP->value.s = (char*) cv;
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
      compactObject(childP, coreP, level + 1);
    else if (childP->type == KjArray)
    {
      for (KjNode* itemP = childP->value.firstChildP; itemP != NULL; itemP = itemP->next)
        compactObject(itemP, coreP, level + 1);
    }
  }
}



// -----------------------------------------------------------------------------
//
// corLdCompactTreeWith -
//
void corLdCompactTreeWith(KjNode* treeP, CorLdContext* ctxP)
{
  if (treeP == NULL || ctxP == NULL)
    return;

  if (treeP->type == KjObject)
  {
    compactObject(treeP, ctxP, 0);
  }
  else if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      compactObject(itemP, ctxP, 0);
  }
}



// -----------------------------------------------------------------------------
//
// corLdCompactTree -
//
void corLdCompactTree(KjNode* treeP)
{
  corLdCompactTreeWith(treeP, corLdCoreContext());
}
