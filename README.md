# swJsonld — JSON-LD Context Library

A JSON-LD context-processing library for C: term expansion, IRI compaction,
prefix handling, `@vocab` / `@container` support, whole-tree expand/compact, and a
thread-safe context cache. Built for NGSI-LD (ETSI GS CIM 009) brokers, but usable
standalone.

- **Version:** 0.1.0
- **Language:** C
- **License:** Apache License 2.0 — Copyright 2026 Seamware

## Features

- **Term expansion** — short name → full IRI (e.g. `temperature` →
  `https://uri.etsi.org/ngsi-ld/temperature`).
- **IRI compaction** — full IRI → short name (the reverse of expansion).
- **Prefix expansion** — `prefix:suffix` notation (e.g. `schema:name` →
  `https://schema.org/name`).
- **@vocab support** — default-vocabulary fallback for both expansion and compaction.
- **@container** — parsed per term (`@language`, `@index`, `@list`, `@set`,
  `@type`, `@id`); language/index keys are treated as opaque (never expanded).
- **Context arrays** — last-wins precedence over overlapping definitions.
- **Whole-tree operations** — expand or compact an entire parsed JSON tree in one
  call, not just individual terms.
- **Thread-safe cache** — mutex-protected, LRU eviction, concurrent-download
  dedup, with `Implicit` / `Cached` / `Hosted` context kinds and short-lived
  *volatile* contexts (NGSI-LD § 5.13).
- **Core context** — the NGSI-LD core context is loaded at init and used as an
  implicit fallback.
- **Pluggable download** — you supply the HTTP callback used to fetch remote
  contexts; the library never opens a socket itself.

## API reference

The umbrella header pulls in everything:

```c
#include "swJsonld/swJsonld.h"
```

### Types

```c
// A single term mapping (see SwldItem.h)
typedef struct SwldItem {
  char*          name;       // short name, e.g. "temperature"
  char*          id;         // expanded IRI
  char*          type;       // "@id", "@vocab", "DateTime", … or NULL
  SwldContainer  container;  // parsed @container (SwldContainerNone if absent)
  unsigned char  flags;      // KJF_* classification bits
} SwldItem;

// A parsed context (see SwldContext.h)
typedef struct SwldContext {
  char*               url;        // URL of this context (NULL for inline)
  char*               id;         // identifier (URL or broker-generated)
  char*               body;       // raw JSON body as received (may be NULL)
  SwldContextKind     kind;       // SwldKindImplicit | SwldKindCached | SwldKindHosted
  KHashTable*         nameHT;     // name -> SwldItem  (expansion)
  KHashTable*         valueHT;    // IRI  -> SwldItem  (compaction)
  char*               vocab;      // @vocab value, or NULL
  struct SwldContext** contextV;  // child contexts (for arrays)
  int                 contexts;   // child-context count
  bool                isArray;    // true = array of child contexts
  // … cache bookkeeping (kind flags, volatile/expiry, timestamps, list linkage)
} SwldContext;

// Download callback: return a malloc'd body, set *statusCodeP
typedef char* (*SwldDownloadFunction)(const char* url, int* statusCodeP);
```

`KAlloc`, `KjNode`, `KHashTable` and `Kjson` come from the k-libs (`kalloc`,
`kjson`, `khash`). swJsonld allocates onto a caller-provided `KAlloc` arena — it
does not own request-scoped memory.

### Lifecycle

```c
int             swldInit(KAlloc* kaP, const char* coreContextUrl, SwldDownloadFunction downloadFn);
void            swldCleanup(void);
SwldContext*    swldCoreContext(void);
```

Initialize once at startup. Pass `NULL` for `coreContextUrl` to use the embedded
NGSI-LD core context; pass `NULL` for `downloadFn` if only inline contexts are
needed. `swldCoreContext()` returns the loaded core.

### Parsing contexts

```c
SwldContext*    swldContextFromTree(KjNode* contextNode, KAlloc* kaP);
SwldContext*    swldContextFromObject(KjNode* objectNode, KAlloc* kaP, const char* url);
SwldContext*    swldContextFromUrl(const char* url, KAlloc* kaP);   // uses cache + download callback
```

`swldContextFromTree` accepts the value of a `@context` (string, object, or array);
`swldContextFromObject` parses a single context object; `swldContextFromUrl`
downloads (or cache-hits) a remote context.

### Expansion / compaction

```c
char*           swldExpand(SwldContext* contextP, const char* name, KAlloc* kaP,
                           SwldItem** itemPP, bool* coreContextP);
const char*     swldCompact(SwldContext* contextP, const char* iri);
char*           swldPrefixExpand(SwldContext* contextP, const char* name, KAlloc* kaP);
bool            swldAlreadyExpanded(const char* value);   // true if "urn:"/"http://"/"https://"
```

Expansion lookup chain: already-expanded → `@keyword` → `prefix:suffix` → user
context → core context → `@vocab` → unchanged. Compaction tries `@vocab` stripping,
then reverse lookup in the user context, then the core.

### Whole-tree operations

```c
SwldContext*    swldExpandTree(KjNode* treeP, SwldContext* userContextP, KAlloc* kaP);
void            swldCompactTree(KjNode* treeP);                       // against the core context
void            swldCompactTreeWith(KjNode* treeP, SwldContext* ctxP); // against a specific context
```

Expand or compact every term in a parsed JSON tree in a single pass — the common
case for an NGSI-LD payload going in (`expand`) or a response going out
(`compact`).

### Cache

```c
SwldContext*    swldCacheLookup(const char* url);
void            swldCacheInsert(SwldContext* contextP);
SwldContext*    swldCacheRemove(const char* idOrUrl);
int             swldCacheReapVolatile(double now);        // drop expired volatile contexts
void            swldCacheSnapshot(KAlloc* allocP, SwldContext*** arrPP, int* nP);
```

Thread-safe (mutex-protected). LRU eviction when full. Volatile contexts (minted so
a `Link` header can reference an inline/array `@context` by URL) are served once and
reaped at `expiresAt`.

## Usage example

```c
#include "kalloc/kalloc.h"          // KAlloc, kaBufferInit
#include "kjson/kjBufferCreate.h"   // Kjson, kjBufferCreate
#include "kjson/kjParse.h"          // kjParse
#include "swJsonld/swJsonld.h"

// User-provided download function (e.g. via libcurl) — return a malloc'd body.
static char* myDownload(const char* url, int* statusCodeP)
{
  // ... HTTP GET ...
  *statusCodeP = 200;
  return body;
}

int main(void)
{
  // 1. A kalloc arena
  KAlloc ka;
  char   buf[65536];
  kaBufferInit(&ka, buf, sizeof(buf), 65536, NULL, "jsonld");

  // 2. Init the library (NULL coreContextUrl → embedded NGSI-LD core)
  swldInit(&ka, NULL, myDownload);

  // 3. Parse an inline context
  Kjson  kjson;
  Kjson* kjP = kjBufferCreate(&kjson, &ka);
  char   json[] = "{ \"@context\": { \"temperature\": \"https://example.org/temperature\" } }";
  KjNode* tree  = kjParse(kjP, json);

  SwldContext* ctxP = swldContextFromTree(tree, &ka);

  // 4. Expand / compact
  char*       iri  = swldExpand(ctxP, "temperature", &ka, NULL, NULL);
  // iri  == "https://example.org/temperature"
  const char* name = swldCompact(ctxP, "https://example.org/temperature");
  // name == "temperature"

  swldCleanup();
  return 0;
}
```

## Building

```bash
make            # build libswJsonld.a (+ .so)
make ci         # clean + install
make di         # debug + install
```

The library links statically into its consumers as `libswJsonld.a`. Sibling
k-lib repos must be present (the build references `../<lib>/lib<lib>.a`).

## Dependencies

Sibling k-lib repos (one `.a` each):

- [`kalloc`](../kalloc) — arena allocator (`KAlloc`)
- [`kjson`](../kjson) — JSON parsing / trees (`KjNode`, `Kjson`)
- [`kbase`](../kbase) — core utilities
- [`khash`](../khash) — hash tables (`KHashTable`)
- [`klog`](../klog) — logging
- [`ktrace`](../ktrace) — trace levels

Plus `pthread` for the cache mutex.
