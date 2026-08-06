//
// FILE            swldUrlResolve.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#ifndef SWLD_URL_RESOLVE_H
#define SWLD_URL_RESOLVE_H

#include "kalloc/KAlloc.h"                           // KAlloc



// -----------------------------------------------------------------------------
//
// swldUrlResolve - resolve a relative IRI reference against a base URL
//
extern const char* swldUrlResolve(const char* base, const char* ref, KAlloc* kaP);

#endif
