//
// FILE            corLdIdGen.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
#include <stdio.h>                                   // snprintf
#include <time.h>                                    // time
#include <pthread.h>                                 // pthread_mutex_t

#include "corAlloc/CorAlloc.h"                       // CorAlloc
#include "corAlloc/corAlloc.h"                       // corAlloc

#include "corJsonld/corLdIdGen.h"                      // Own interface



// -----------------------------------------------------------------------------
//
// Counter + lock. Using a mutex instead of <stdatomic.h> to stay portable
// with the rest of the codebase; contention is negligible (one CAS per
// POST /jsonldContexts).
//
static pthread_mutex_t  idMutex   = PTHREAD_MUTEX_INITIALIZER;
static unsigned long    idCounter = 0;



// -----------------------------------------------------------------------------
//
// corLdIdGenerate -
//
char* corLdIdGenerate(CorAlloc* kaP)
{
  pthread_mutex_lock(&idMutex);
  unsigned long n = ++idCounter;
  pthread_mutex_unlock(&idMutex);

  long long     ts  = (long long) time(NULL);
  char*         buf = (char*) corAlloc(kaP, 64);

  if (buf != NULL)
    snprintf(buf, 64, "urn:ngsi-ld:Context:%lu-%lld", n, ts);

  return buf;
}
