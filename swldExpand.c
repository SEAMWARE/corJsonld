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
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTExpand
#include "swJsonld/swldPrefixExpand.h"               // swldPrefixExpand
#include "swJsonld/swldInit.h"                       // swldCoreContext
#include "swJsonld/swldExpand.h"                     // Own interface



// -----------------------------------------------------------------------------
//
// swldAlreadyExpanded -
//
bool swldAlreadyExpanded(const char* value)
{
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
// contextLookup - lookup in a context (handles arrays)
//
static SwldItem* contextLookup(SwldContext* contextP, const char* name)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->isArray == true)
  {
    //
    // Last-wins: iterate from last to first
    //
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      SwldItem* itemP = contextLookup(contextP->contextV[ix], name);

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
  SwldItem* itemP = contextLookup(contextP, name);

  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    //
    // User context may include the core context - check if this term is also in core
    //
    if (coreContextP != NULL && contextLookup(swldCoreContext(), name) != NULL)
      *coreContextP = true;

    return itemP->id;
  }

  //
  // Step 5: Lookup in core context
  //
  itemP = contextLookup(swldCoreContext(), name);
  if (itemP != NULL)
  {
    if (itemPP != NULL)
      *itemPP = itemP;

    if (coreContextP != NULL)
      *coreContextP = true;

    return itemP->id;
  }

  //
  // Step 6: @vocab fallback
  //
  const char* vocab = contextVocab(contextP);

  if (vocab == NULL)
    vocab = contextVocab(swldCoreContext());

  if (vocab != NULL)
  {
    int vocabLen = strlen(vocab);
    int nameLen  = strlen(name);
    char* expanded = (char*) kaAlloc(kaP, vocabLen + nameLen + 1);

    if (expanded != NULL)
    {
      memcpy(expanded, vocab, vocabLen);
      memcpy(expanded + vocabLen, name, nameLen + 1);
      return expanded;
    }
  }

  //
  // Step 7: Return unchanged
  //
  return (char*) name;
}
