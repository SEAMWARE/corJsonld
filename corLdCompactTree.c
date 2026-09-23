//
// FILE            corLdCompactTree.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <string.h>                                  // strcmp, strlen, memcpy, memmove

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
// -----------------------------------------------------------------------------
//
// vocabValueCompact - compact one value of an @type:@vocab term
//
// The mirror of vocabValueExpand: the part before a registrant-declared suffix
// is compacted and the suffix follows it. Done IN PLACE, as there is no
// allocator here - the compacted term is never longer than its IRI, so the
// suffix only ever moves towards the front of the value's own buffer. Should a
// compaction ever come out longer, the value is left as it was.
//
static void vocabValueCompact(CorLdContext* coreP, CorLdItem* termItemP, KjNode* valueP)
{
  CorLdVocabValueSuffix suffixFn = corLdGetVocabValueSuffix();
  int                   ix       = (suffixFn != NULL) ? suffixFn(termItemP->name, valueP->value.s) : -1;

  if (ix <= 0)
  {
    const char* cv = corLdCompact(coreP, valueP->value.s);
    if (cv != NULL) valueP->value.s = (char*) cv;
    return;
  }

  char* valueS = valueP->value.s;
  char  saved  = valueS[ix];

  //
  // The compacted term may point INTO the value itself (the tail of the IRI),
  // so it is measured while the term is still terminated, and moved to the front
  // before the suffix is put back and moved up behind it. Both moves go towards
  // the front and stay within [0, ix) and [ix, end) respectively.
  //
  valueS[ix] = 0;

  const char* cv    = corLdCompact(coreP, valueS);
  int         cvLen = (cv != NULL) ? (int) strlen(cv) : -1;

  if ((cv == NULL) || (cvLen > ix))
  {
    valueS[ix] = saved;
    return;
  }

  memmove(valueS, cv, cvLen);
  valueS[ix] = saved;
  memmove(&valueS[cvLen], &valueS[ix], strlen(&valueS[ix]) + 1);
}



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
    //
    // ... and a JsonProperty's `json`, where the spec is blunt: "Raw
    // unexpandable JSON which shall not be interpreted as JSON-LD using the
    // supplied @context" (TS 104-175, clause 5). We were interpreting it - a
    // key under the core @vocab came back stripped:
    //
    //   in : "json": { "https://uri.etsi.org/ngsi-ld/default-context/foo": 1 }
    //   out: "json": { "foo": 1 }
    //
    // ⚠️ NOT extended to VK_VALUE, although corLdExpandTree lists it and the
    // symmetry is tempting. A Property's value reaches expansion in two shapes
    // and only one of them is recognisable there: in NORMALIZED input the
    // `value` key is present and the expander leaves the subtree alone, but in
    // SIMPLIFIED input the attribute IS the value, nothing yet marks it as one,
    // and its keys get @vocab-expanded like any other term. Compaction has to
    // undo that, so making `value` opaque here broke simplified round-tripping
    // (create_entity_simplified: `num` came back as
    // `.../default-context/num`).
    //
    // Which leaves a real ambiguity rather than a bug: on the way out, a key
    // that IS literally a default-context IRI is indistinguishable from a short
    // key that was expanded on the way in. It cannot be preserved without
    // knowing which it was, and the wire format does not say. Recorded as C4 in
    // coraine/doc/tutorial-doubts.md; `json` is the half the spec settles.
    //
    int  vk         = (termItemP != NULL) ? KJF_VK_ID(termItemP->flags) : KJF_VK_NONE;
    bool opaqueKeys = (termItemP != NULL &&
                       ((termItemP->container & CORLD_CONTAINER_OPAQUE_KEYS) != 0 ||
                        vk == KJF_VK_JSON));

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
        vocabValueCompact(coreP, termItemP, childP);
      else if (childP->type == KjArray)
      {
        for (KjNode* elemP = childP->value.firstChildP; elemP != NULL; elemP = elemP->next)
        {
          if (elemP->type == KjString)
            vocabValueCompact(coreP, termItemP, elemP);
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
