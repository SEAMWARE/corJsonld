//
// FILE            swldUrlResolve.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
#include <stdbool.h>                                 // bool, true, false
#include <string.h>                                  // strstr, strchr, strrchr, strlen, memcpy, strncmp
#include <ctype.h>                                   // isalpha, isalnum

#include "kalloc/KAlloc.h"                           // KAlloc
#include "kalloc/kaAlloc.h"                          // kaAlloc

#include "swJsonld/swldUrlResolve.h"                 // Own interface



// -----------------------------------------------------------------------------
//
// urlIsAbsolute - does the IRI reference start with a scheme?
//
// RFC 3986 § 3.1: scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
//
static bool urlIsAbsolute(const char* ref)
{
  if (isalpha(*ref) == 0)
    return false;

  for (const char* sP = &ref[1]; *sP != 0; sP++)
  {
    if (*sP == ':')
      return true;

    if ((isalnum(*sP) == 0) && (*sP != '+') && (*sP != '-') && (*sP != '.'))
      return false;
  }

  return false;
}



// -----------------------------------------------------------------------------
//
// swldUrlResolve - resolve a relative IRI reference against a base URL
//
// A string inside an @context array is an IRI REFERENCE, not necessarily an absolute URL.
// JSON-LD resolves it against the base IRI (RFC 3986 § 5), and for a downloaded @context the base
// is the URL that very @context came from:
//
//   base:   https://a.b/x/y/compound.jsonld
//   ref:    sub.jsonld
//   result: https://a.b/x/y/sub.jsonld
//
// 'ref' is returned untouched when it is already absolute, and when there is no base to resolve
// against - an @context that arrived inline in a request body has no URL of its own.
//
const char* swldUrlResolve(const char* base, const char* ref, KAlloc* kaP)
{
  if ((base == NULL) || (ref == NULL) || (*ref == 0) || (urlIsAbsolute(ref) == true))
    return ref;

  const char* authorityP = strstr(base, "://");

  if (authorityP == NULL)  // Not a URL that can be taken apart - leave the reference alone
    return ref;

  authorityP = &authorityP[3];

  const char* pathP = strchr(authorityP, '/');   // Start of the path inside 'base'
  const char* endP;                              // What to keep of 'base'

  if (ref[0] == '/')
  {
    if (ref[1] == '/')                           // "//host/path" - keep only "scheme:"
      endP = &strstr(base, "://")[1];
    else                                         // "/path" - keep "scheme://authority"
      endP = (pathP != NULL)? pathP : &base[strlen(base)];
  }
  else                                           // Relative path - keep everything up to the last '/'
  {
    const char* slashP = (pathP != NULL)? strrchr(pathP, '/') : NULL;

    endP = (slashP != NULL)? &slashP[1] : &base[strlen(base)];

    //
    // Collapse the dot-segments of the reference (RFC 3986 § 5.2.4)
    //
    while (true)
    {
      if (strncmp(ref, "./", 2) == 0)
        ref = &ref[2];
      else if ((strncmp(ref, "../", 3) == 0) && (slashP != NULL) && (endP > &pathP[1]))
      {
        const char* upP = endP - 1;                   // Step onto the trailing '/' ...

        while ((upP > pathP) && (upP[-1] != '/'))     // ... and back to the one before it
          upP -= 1;

        endP = upP;
        ref  = &ref[3];
      }
      else
        break;
    }
  }

  int   baseLen = endP - base;
  int   refLen  = strlen(ref);
  char* urlP    = (char*) kaAlloc(kaP, baseLen + refLen + 2);

  if (urlP == NULL)
    return ref;

  memcpy(urlP, base, baseLen);

  if ((baseLen > 0) && (urlP[baseLen - 1] != '/') && (ref[0] != '/'))
    urlP[baseLen++] = '/';

  memcpy(&urlP[baseLen], ref, refLen);
  urlP[baseLen + refLen] = 0;

  return urlP;
}
