//
// FILE            SwldItem.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_ITEM_H
#define SWLD_ITEM_H



// -----------------------------------------------------------------------------
//
// SwldContainer - parsed @container value (compared per-term per-request, so
// kept as an enum to avoid strcmp in hot expand/compact loops).
//
// Values are bit flags so that "is this one of {Language, Index}?" (the
// opaque-keys group, see SWLD_CONTAINER_OPAQUE_KEYS) is one AND + one
// branch instead of two compares.
//
// JSON-LD also allows arrays here (e.g. ["@list", "@id"]), but the NGSI-LD
// core context only uses single-string forms — sufficient for what we need
// today. NGSI-LD ignores @graph if present (no graph semantics).
//
typedef enum SwldContainer
{
  SwldContainerNone     = 0,
  SwldContainerLanguage = 0x01,   // "@language" — keys are BCP-47 language tags (opaque)
  SwldContainerIndex    = 0x02,   // "@index"    — keys are user-defined index strings (opaque)
  SwldContainerList     = 0x04,   // "@list"     — ordered array
  SwldContainerSet      = 0x08,   // "@set"      — unordered array
  SwldContainerType     = 0x10,   // "@type"     — keys are type IRIs
  SwldContainerId       = 0x20,   // "@id"       — keys are id IRIs
  SwldContainerGraph    = 0x40,   // "@graph"    — ignored by NGSI-LD
  SwldContainerOther    = 0x80    // anything else we haven't enumerated yet
} SwldContainer;

// Mask of containers whose value-object keys must NOT be expanded/compacted
// as terms — language tags and user-defined index strings are opaque.
#define SWLD_CONTAINER_OPAQUE_KEYS  (SwldContainerLanguage | SwldContainerIndex)



// -----------------------------------------------------------------------------
//
// SwldItem -
//
typedef struct SwldItem
{
  char*          name;       // Short name (e.g., "temperature")
  char*          id;         // Expanded IRI
  char*          type;       // "@id", "@vocab", "DateTime", etc. - or NULL
  SwldContainer  container;  // parsed @container; SwldContainerNone if absent.
                             // For Language / Index, the term's value-object
                             // keys are opaque and must not be expanded as terms.
} SwldItem;

#endif
