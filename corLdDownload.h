//
// FILE            corLdDownload.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CORLD_DOWNLOAD_H
#define CORLD_DOWNLOAD_H

#include <stdbool.h>                                 // bool

#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corJsonld/CorLdContext.h"                     // CorLdContext



// -----------------------------------------------------------------------------
//
// corLdContextFromUrl -
//
extern CorLdContext* corLdContextFromUrl(const char* url, CorAlloc* kaP);



// -----------------------------------------------------------------------------
//
// CorLdOwnContextFunction - a URL the application serves @contexts at itself, resolved to its context
//
// An application that serves @contexts of its own (a broker's /jsonldContexts/{id}) knows them under an id,
// not under the URL a client names them by. Without this, such a URL misses the cache, is downloaded - from
// the application itself - and cached a second time, as a Cached copy of its own context. The function is
// asked first, on every cache miss: *ownP false - not one of its URLs, downloaded as any other; *ownP true -
// its own, and the answer is the context (NULL: it has none by that URL), never downloaded.
//
// NULL (the default): every URL is downloaded.
//
typedef CorLdContext* (*CorLdOwnContextFunction)(const char* url, bool* ownP);

extern void corLdSetOwnContext(CorLdOwnContextFunction fn);



// -----------------------------------------------------------------------------
//
// corLdIsCoreContextUrl - true if `url` names the NGSI-LD core context:
// the configured core, the canonical unversioned form, or any older
// versioned form (the ignored-stub family). The admin API treats all of
// them as THE core entry.
//
extern bool corLdIsCoreContextUrl(const char* url);

#endif
