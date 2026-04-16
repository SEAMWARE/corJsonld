//
// FILE            swldIdGen.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdio.h>                                   // snprintf
#include <time.h>                                    // time
#include <pthread.h>                                 // pthread_mutex_t

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                          // kaAlloc

#include "swJsonld/swldIdGen.h"                      // Own interface



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
// swldIdGenerate -
//
char* swldIdGenerate(KAlloc* kaP)
{
  pthread_mutex_lock(&idMutex);
  unsigned long n = ++idCounter;
  pthread_mutex_unlock(&idMutex);

  long long     ts  = (long long) time(NULL);
  char*         buf = (char*) kaAlloc(kaP, 64);

  if (buf != NULL)
    snprintf(buf, 64, "urn:ngsi-ld:Context:%lu-%lld", n, ts);

  return buf;
}
