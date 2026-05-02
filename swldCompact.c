//
// FILE            swldCompact.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <string.h>                                  // strncmp, strlen

#include "khash/khash.h"                             // khashItemLookup
#include "swJsonld/SwldItem.h"                       // SwldItem
#include "swJsonld/SwldContext.h"                     // SwldContext
#include "swJsonld/swldTraceLevels.h"                // SwldTCompact
#include "swJsonld/swldExpand.h"                     // swldAlreadyExpanded
#include "swJsonld/swldInit.h"                       // swldCoreContext
#include "swJsonld/swldCompact.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// contextReverseLookup - lookup by IRI in valueHT (handles arrays)
//
static SwldItem* contextReverseLookup(SwldContext* contextP, const char* iri)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->ignored == true)
    return NULL;

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      SwldItem* itemP = contextReverseLookup(contextP->contextV[ix], iri);

      if (itemP != NULL)
        return itemP;
    }

    return NULL;
  }

  if (contextP->valueHT == NULL)
    return NULL;

  return (SwldItem*) khashItemLookup(contextP->valueHT, iri);
}



// -----------------------------------------------------------------------------
//
// contextVocabCompact - strip @vocab prefix if it matches
//
static const char* contextVocabCompact(SwldContext* contextP, const char* iri)
{
  if (contextP == NULL)
    return NULL;

  if (contextP->ignored == true)
    return NULL;

  if (contextP->vocab != NULL)
  {
    int vocabLen = strlen(contextP->vocab);

    if (strncmp(iri, contextP->vocab, vocabLen) == 0)
      return iri + vocabLen;
  }

  if (contextP->isArray == true)
  {
    for (int ix = contextP->contexts - 1; ix >= 0; ix--)
    {
      const char* result = contextVocabCompact(contextP->contextV[ix], iri);

      if (result != NULL)
        return result;
    }
  }

  return NULL;
}



// -----------------------------------------------------------------------------
//
// swldCompact -
//
const char* swldCompact(SwldContext* contextP, const char* iri)
{
  //
  // Step 1: Not a full IRI - return as-is
  //
  if (swldAlreadyExpanded(iri) == false)
    return iri;

  //
  // Step 2: Try @vocab stripping (user context)
  //
  const char* result = contextVocabCompact(contextP, iri);

  if (result != NULL)
    return result;

  //
  // Step 3: Reverse lookup in user context
  //
  SwldItem* itemP = contextReverseLookup(contextP, iri);

  if (itemP != NULL)
    return itemP->name;

  //
  // Step 4: Try core context
  //
  SwldContext* coreP = swldCoreContext();

  result = contextVocabCompact(coreP, iri);
  if (result != NULL)
    return result;

  itemP = contextReverseLookup(coreP, iri);
  if (itemP != NULL)
    return itemP->name;

  //
  // Step 5: Return unchanged
  //
  return iri;
}
