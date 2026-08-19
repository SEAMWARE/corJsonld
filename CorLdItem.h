//
// FILE            CorLdItem.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_ITEM_H
#define CORLD_ITEM_H



// -----------------------------------------------------------------------------
//
// CorLdContainer - parsed @container value (compared per-term per-request, so
// kept as an enum to avoid strcmp in hot expand/compact loops).
//
// Values are bit flags so that "is this one of {Language, Index}?" (the
// opaque-keys group, see CORLD_CONTAINER_OPAQUE_KEYS) is one AND + one
// branch instead of two compares.
//
// JSON-LD also allows arrays here (e.g. ["@list", "@id"]), but the NGSI-LD
// core context only uses single-string forms — sufficient for what we need
// today. NGSI-LD ignores @graph if present (no graph semantics).
//
typedef enum CorLdContainer
{
  CorLdContainerNone     = 0,
  CorLdContainerLanguage = 0x01,   // "@language" — keys are BCP-47 language tags (opaque)
  CorLdContainerIndex    = 0x02,   // "@index"    — keys are user-defined index strings (opaque)
  CorLdContainerList     = 0x04,   // "@list"     — ordered array
  CorLdContainerSet      = 0x08,   // "@set"      — unordered array
  CorLdContainerType     = 0x10,   // "@type"     — keys are type IRIs
  CorLdContainerId       = 0x20,   // "@id"       — keys are id IRIs
  CorLdContainerGraph    = 0x40,   // "@graph"    — ignored by NGSI-LD
  CorLdContainerOther    = 0x80    // anything else we haven't enumerated yet
} CorLdContainer;

// Mask of containers whose value-object keys must NOT be expanded/compacted
// as terms — language tags and user-defined index strings are opaque.
#define CORLD_CONTAINER_OPAQUE_KEYS  (CorLdContainerLanguage | CorLdContainerIndex)



// -----------------------------------------------------------------------------
//
// CorLdItem -
//
typedef struct CorLdItem
{
  char*          name;       // Short name (e.g., "temperature")
  char*          id;         // Expanded IRI
  char*          type;       // "@id", "@vocab", "DateTime", etc. - or NULL
  CorLdContainer  container;  // parsed @container; CorLdContainerNone if absent.
                             // For Language / Index, the term's value-object
                             // keys are opaque and must not be expanded as terms.
  unsigned char  flags;      // KJF_* bits (corLdExpand.h), classified once when the
                             // core context is loaded and copied onto each KjNode
                             // during expansion — so downstream is a bit test, not strcmp.
} CorLdItem;

#endif
