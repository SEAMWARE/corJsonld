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
// SwldItem -
//
typedef struct SwldItem
{
  char*  name;   // Short name (e.g., "temperature")
  char*  id;     // Expanded IRI (e.g., "https://uri.etsi.org/ngsi-ld/temperature")
  char*  type;   // "@id", "@vocab", "DateTime", etc. - or NULL
} SwldItem;

#endif
