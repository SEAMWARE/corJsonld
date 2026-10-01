//
// FILE            corLdContextParse.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdio.h>                                   // snprintf
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strcmp, strlen

#include "corAlloc/corAlloc.h"                       // corAlloc
#include "corAlloc/corAllocStrdup.h"                 // corAllocStrdup
#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corTree/CorNode.h"                         // CorNode, CorValueType
#include "corTree/corTreeLookup.h"                   // corTreeLookup
#include "corHash/corHash.h"                         // corHashTableCreate, corHashItemAdd, corHashItemLookup
#include "corJsonld/CorLdItem.h"                       // CorLdItem
#include "corJsonld/CorLdContext.h"                     // CorLdContext
#include "corJsonld/corLdTraceLevels.h"                // CorLdTContextParse
#include "corJsonld/corLdPrefixExpand.h"               // corLdPrefixExpand
#include "corJsonld/corLdInit.h"                       // CorLdErrorFunction
#include "corJsonld/corLdDownload.h"                   // corLdIsCoreContextUrl
#include "corJsonld/corLdDownload.h"                   // corLdContextFromUrl
#include "corJsonld/corLdUrlResolve.h"                 // corLdUrlResolve
#include "corJsonld/corLdContextParse.h"               // Own interface



// -----------------------------------------------------------------------------
//
// nameHashCode - djb2 hash over the short name
//
static unsigned int nameHashCode(const char* name)
{
  unsigned int hash = 5381;

  while (*name != 0)
  {
    hash = hash * 33 + (unsigned char) *name;
    ++name;
  }

  return hash;
}



// -----------------------------------------------------------------------------
//
// nameCompare -
//
static int nameCompare(const char* name, void* itemP)
{
  return strcmp(name, ((CorLdItem*) itemP)->name);
}



// -----------------------------------------------------------------------------
//
// valueHashCode - djb2 hash over the IRI
//
static unsigned int valueHashCode(const char* iri)
{
  unsigned int hash = 5381;

  while (*iri != 0)
  {
    hash = hash * 33 + (unsigned char) *iri;
    ++iri;
  }

  return hash;
}



// -----------------------------------------------------------------------------
//
// valueCompare -
//
static int valueCompare(const char* iri, void* itemP)
{
  return strcmp(iri, ((CorLdItem*) itemP)->id);
}



// -----------------------------------------------------------------------------
//
// corLdErrorGet - internal (corLdInit.c), as in corLdDownload.c
//
extern CorLdErrorFunction corLdErrorGet(void);



// -----------------------------------------------------------------------------
//
// corLdContextFromObject -
//
CorLdContext* corLdContextFromObject(CorNode* objectNode, CorAlloc* kaP, const char* url)
{
  CorLdContext* contextP = (CorLdContext*) corAlloc(kaP, sizeof(CorLdContext));

  if (contextP == NULL)
    return NULL;

  memset(contextP, 0, sizeof(CorLdContext));

  contextP->nameHT  = corHashTableCreate(kaP, nameHashCode, nameCompare, 128);
  contextP->valueHT = corHashTableCreate(kaP, valueHashCode, valueCompare, 128);

  if (contextP->nameHT == NULL || contextP->valueHT == NULL)
    return NULL;

  if (url != NULL)
  {
    contextP->url = corAllocStrdup(kaP, url);
    contextP->id  = contextP->url;
  }

  //
  // Pass 1: Build nameHT from object members
  //
  CorNode* memberP = objectNode->value.head;

  while (memberP != NULL)
  {
    if (memberP->name == NULL)
    {
      memberP = memberP->next;
      continue;
    }

    //
    // @import (JSON-LD 1.1 § 4.1.10) - load another context and apply this object's terms on top of
    // it. Not implemented, and not silently ignored either: it became a term named "@import" before,
    // and the client's imported terms simply never applied. NGSI-LD does not mention it (spec-doubts-2
    // #137) - 501 until it does.
    //
    if (strcmp(memberP->name, "@import") == 0)
    {
      CorLdErrorFunction errorFn = corLdErrorGet();

      if (errorFn != NULL)
        errorFn(501, "Not Implemented", "@import in an @context is not supported");

      return NULL;
    }

    //
    // Skip @version and @protected
    //
    if ((strcmp(memberP->name, "@version") == 0) || (strcmp(memberP->name, "@protected") == 0))
    {
      memberP = memberP->next;
      continue;
    }

    //
    // @vocab - store the value
    //
    if (strcmp(memberP->name, "@vocab") == 0)
    {
      if (memberP->type == CorString)
        contextP->vocab = corAllocStrdup(kaP, memberP->value.s);

      memberP = memberP->next;
      continue;
    }

    CorLdItem* itemP = (CorLdItem*) corAlloc(kaP, sizeof(CorLdItem));

    if (itemP == NULL)
    {
      memberP = memberP->next;
      continue;
    }

    itemP->name = corAllocStrdup(kaP, memberP->name);
    itemP->id   = NULL;
    itemP->type = NULL;

    if (memberP->type == CorString)
    {
      //
      // Simple mapping: "temperature": "https://..."
      //
      itemP->id = corAllocStrdup(kaP, memberP->value.s);
    }
    else if (memberP->type == CorObject)
    {
      //
      // A scoped context - a term definition with an @context of its own - is not allowed in a user
      // @context (TS 104-175 § 8.2: it "could be used to modify terms defined in the core @context"),
      // and the answer is BadRequestData. The core context itself has one (ngsildproof), so the core
      // is exempt - it is the context the rule protects.
      //
      if ((corTreeLookup(memberP, "@context") != NULL) && ((url == NULL) || (corLdIsCoreContextUrl(url) == false)))
      {
        CorLdErrorFunction errorFn = corLdErrorGet();

        if (errorFn != NULL)
        {
          char detail[512];
          snprintf(detail, sizeof(detail), "term '%s' defines a scoped @context - not allowed in a user @context (TS 104-175 § 8.2)", memberP->name);
          errorFn(400, "Scoped Context Not Allowed", detail);
        }

        return NULL;
      }

      //
      // Object mapping: "temperature": { "@id": "...", "@type": "..." }
      //
      CorNode* idNodeP       = corTreeLookup(memberP, "@id");
      CorNode* typeNodeP     = corTreeLookup(memberP, "@type");
      CorNode* containerNodeP = corTreeLookup(memberP, "@container");

      if (idNodeP != NULL && idNodeP->type == CorString)
        itemP->id = corAllocStrdup(kaP, idNodeP->value.s);
      else
        itemP->id = corAllocStrdup(kaP, memberP->name);

      if (typeNodeP != NULL && typeNodeP->type == CorString)
        itemP->type = corAllocStrdup(kaP, typeNodeP->value.s);

      // Parse @container into the enum once here — checked on every term
      // lookup during expand/compact, so strcmp would be wasteful.
      // NGSI-LD ignores @graph if present (no graph semantics).
      if (containerNodeP != NULL && containerNodeP->type == CorString)
      {
        const char* c = containerNodeP->value.s;
        if      (strcmp(c, "@language") == 0)  itemP->container = CorLdContainerLanguage;
        else if (strcmp(c, "@index")    == 0)  itemP->container = CorLdContainerIndex;
        else if (strcmp(c, "@list")     == 0)  itemP->container = CorLdContainerList;
        else if (strcmp(c, "@set")      == 0)  itemP->container = CorLdContainerSet;
        else if (strcmp(c, "@type")     == 0)  itemP->container = CorLdContainerType;
        else if (strcmp(c, "@id")       == 0)  itemP->container = CorLdContainerId;
        else if (strcmp(c, "@graph")    == 0)  itemP->container = CorLdContainerGraph;
        else                                   itemP->container = CorLdContainerOther;
      }
    }
    else
    {
      itemP->id = corAllocStrdup(kaP, memberP->name);
    }

    corHashItemAdd(contextP->nameHT, itemP->name, itemP);
    memberP = memberP->next;
  }

  //
  // Pass 2: Expand prefix notation in id values, build valueHT
  //
  for (int slot = 0; slot < contextP->nameHT->arraySize; slot++)
  {
    CorHashListItem* listItemP = contextP->nameHT->array[slot];

    while (listItemP != NULL)
    {
      CorLdItem* itemP = (CorLdItem*) listItemP->data;

      if (itemP->id != NULL)
      {
        //
        // If id is not already a full IRI, try prefix expansion
        //
        if ((strncmp(itemP->id, "http://", 7) != 0) &&
            (strncmp(itemP->id, "https://", 8) != 0) &&
            (strncmp(itemP->id, "urn:", 4) != 0) &&
            (itemP->id[0] != '@'))
        {
          char* expanded = corLdPrefixExpand(contextP, itemP->id, kaP);

          if (expanded != NULL)
            itemP->id = expanded;
        }

        corHashItemAdd(contextP->valueHT, itemP->id, itemP);
      }

      listItemP = listItemP->next;
    }
  }

  return contextP;
}



// -----------------------------------------------------------------------------
//
// corLdContextFromTree -
//
CorLdContext* corLdContextFromTree(CorNode* contextNode, CorAlloc* kaP, const char* baseUrl)
{
  if (contextNode == NULL)
    return NULL;

  if (contextNode->type == CorString)
  {
    //
    // IRI reference - resolve it against the URL of the @context it appeared in, then download
    // and parse. 'baseUrl' is NULL for an @context that arrived inline in a request body, and
    // corLdUrlResolve then leaves the reference exactly as it is.
    //
    return corLdContextFromUrl(corLdUrlResolve(baseUrl, contextNode->value.s, kaP), kaP);
  }

  if (contextNode->type == CorObject)
  {
    //
    // Inline context object
    //
    return corLdContextFromObject(contextNode, kaP, NULL);
  }

  if (contextNode->type == CorArray)
  {
    //
    // Array of contexts
    //
    int count = 0;

    for (CorNode* childP = contextNode->value.head; childP != NULL; childP = childP->next)
      count += 1;

    CorLdContext* contextP = (CorLdContext*) corAlloc(kaP, sizeof(CorLdContext));

    if (contextP == NULL)
      return NULL;

    memset(contextP, 0, sizeof(CorLdContext));
    contextP->isArray  = true;
    contextP->contexts = count;
    contextP->contextV = (CorLdContext**) corAlloc(kaP, count * sizeof(CorLdContext*));

    if (contextP->contextV == NULL)
      return NULL;

    int ix = 0;

    for (CorNode* childP = contextNode->value.head; childP != NULL; childP = childP->next)
    {
      contextP->contextV[ix] = corLdContextFromTree(childP, kaP, baseUrl);

      //
      // An element that cannot be resolved fails the whole @context. Carrying on without it
      // would leave the terms it defines to fall back on the default context, and the request
      // would be accepted with the Entity stored under the wrong Attribute names.
      // TS 104-176 clause 6: LdContextNotAvailable, 504 - raised by the caller.
      //
      if (contextP->contextV[ix] == NULL)
        return NULL;

      ix += 1;
    }

    return contextP;
  }

  return NULL;
}
