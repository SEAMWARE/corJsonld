//
// FILE            SwldContext.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_CONTEXT_H
#define SWLD_CONTEXT_H

#include <stdbool.h>                                 // bool
#include "khash/khash.h"                             // KHashTable



// -----------------------------------------------------------------------------
//
// SwldContext -
//
typedef struct SwldContext
{
  char*                      url;         // URL of this context (NULL for inline)
  char*                      id;          // Identifier (URL or generated)
  KHashTable*                nameHT;      // name -> SwldItem  (expansion)
  KHashTable*                valueHT;     // IRI  -> SwldItem  (compaction)
  char*                      vocab;       // @vocab value, or NULL
  struct SwldContext**        contextV;    // child contexts for arrays
  int                        contexts;    // count of child contexts
  bool                       isArray;     // true = array of child contexts
  double                     createdAt;
  double                     usedAt;
  struct SwldContext*         next;        // cache linked-list linkage
} SwldContext;

#endif
