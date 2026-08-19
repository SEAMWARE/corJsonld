//
// FILE            corLdUrlResolve.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef CORLD_URL_RESOLVE_H
#define CORLD_URL_RESOLVE_H

#include "kalloc/KAlloc.h"                           // KAlloc



// -----------------------------------------------------------------------------
//
// corLdUrlResolve - resolve a relative IRI reference against a base URL
//
extern const char* corLdUrlResolve(const char* base, const char* ref, KAlloc* kaP);

#endif
