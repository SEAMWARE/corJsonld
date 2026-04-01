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
    char* expanded = swldExpand(contextP, childP->name, kaP, NULL, &coreContext);

    if (expanded != NULL && expanded[0] != '@')
    {
      childP->name = expanded;
    }
    else if (expanded != NULL && strcmp(expanded, "@type") == 0)
    {
      //
      // "type" maps to "@type" - don't rename the key, but expand the string value
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
    // Recurse into sub-objects and arrays of objects
    //
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
SwldContext* swldExpandTree(KjNode* treeP, KAlloc* kaP)
{
  if (treeP == NULL)
    return NULL;

  //
  // Extract @context from the tree (if present)
  //
  SwldContext* userContextP = NULL;
  KjNode*     atContextP   = NULL;

  if (treeP->type == KjObject)
    atContextP = kjLookup(treeP, "@context");

  if (atContextP != NULL)
  {
    userContextP = swldContextFromTree(atContextP, kaP);

    //
    // Remove @context from the tree
    //
    kjChildRemove(treeP, atContextP);
  }

  //
  // Use user context if provided, otherwise core context
  //
  SwldContext* contextP = (userContextP != NULL) ? userContextP : swldCoreContext();

  if (contextP == NULL)
    return NULL;

  //
  // Expand the tree (single object or array of objects)
  //
  if (treeP->type == KjObject)
  {
    expandObject(treeP, contextP, kaP, 0);
  }
  else if (treeP->type == KjArray)
  {
    for (KjNode* itemP = treeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
      expandObject(itemP, contextP, kaP, 0);
  }

  return contextP;
}
