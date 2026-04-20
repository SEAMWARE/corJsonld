//
// FILE            swldCompactTree.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                  // strcmp

#include "kjson/KjNode.h"                            // KjNode, KjObject, KjArray
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTCompact
#include "swJsonld/swldInit.h"                       // swldCoreContext
#include "swJsonld/swldCompact.h"                    // swldCompact
#include "swJsonld/swldCompactTree.h"                // Own interface



// -----------------------------------------------------------------------------
//
// compactObject - recursively compact all names inside an object
//
static void compactObject(KjNode* objectP, SwldContext* coreP, int level)
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
    const char* compacted = swldCompact(coreP, childP->name);

    if (compacted != NULL)
      childP->name = (char*) compacted;

    //
    // For "type" fields at entity level, also compact the string value (e.g. full URI -> "Vehicle")
    // At attribute level (level 1+), "type" values are NGSI-LD keywords, not expanded URIs.
    //
    if (strcmp(childP->name, "type") == 0)
    {
      if (childP->type == KjString)
      {
        const char* compactedValue = swldCompact(coreP, childP->value.s);

        if (compactedValue != NULL)
          childP->value.s = (char*) compactedValue;
      }
      else if (childP->type == KjArray)
      {
        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString)
          {
            const char* compactedValue = swldCompact(coreP, elemP->value.s);

            if (compactedValue != NULL)
              elemP->value.s = (char*) compactedValue;
          }
        }
      }
    }

    //
    // Recurse into sub-objects and arrays of objects
    //
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
// swldCompactTreeWith -
//
void swldCompactTreeWith(KjNode* treeP, SwldContext* ctxP)
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
// swldCompactTree -
//
void swldCompactTree(KjNode* treeP)
{
  swldCompactTreeWith(treeP, swldCoreContext());
}
