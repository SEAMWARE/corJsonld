#ifndef CORLD_ID_GEN_H
#define CORLD_ID_GEN_H

//
// FILE            corLdIdGen.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// Broker-side URI generator for Hosted JSON-LD contexts (NGSI-LD § 5.13.1).
// The id is assigned at POST /jsonldContexts time and returned in the
// Location header.
//

#include "kalloc/KAlloc.h"                           // KAlloc



// -----------------------------------------------------------------------------
//
// corLdIdGenerate -
//
// Produce a fresh context id of the shape
//     urn:ngsi-ld:Context:<counter>-<epochSeconds>
// allocated in the caller-supplied arena. Thread-safe. No persistence —
// counter restarts at 1 on process boot. Only uniqueness within a running
// broker is required since the cache itself is not persisted (Phase B).
//
extern char* corLdIdGenerate(KAlloc* kaP);

#endif
