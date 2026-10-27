# corJsonld — JSON-LD Context Library

A JSON-LD context-processing library for C: term expansion, IRI compaction,
prefix handling, `@vocab` / `@container` support, whole-tree expand/compact, and a
thread-safe context cache. Built for NGSI-LD (ETSI GS CIM 009) brokers, but usable
standalone.

- **Version:** 0.1.0
- **Language:** C
- **License:** [Apache License 2.0](LICENSE) — Copyright 2026 Seamware

## Features

- **Term expansion** — short name → full IRI (e.g. `temperature` →
  `https://uri.etsi.org/ngsi-ld/temperature`).
- **IRI compaction** — full IRI → short name (the reverse of expansion).
- **Prefix expansion** — `prefix:suffix` notation (e.g. `schema:name` →
  `https://schema.org/name`).
- **@vocab** — the core context's `@vocab` is the default vocabulary, for expansion
  and compaction. A user context's `@vocab` is parsed and not used: a user context
  cannot change the default vocabulary.
- **@container** — parsed per term (`@language`, `@index`, `@list`, `@set`,
  `@type`, `@id`); language/index keys are treated as opaque (never expanded).
- **Context arrays** — last-wins precedence over overlapping definitions.
- **Whole-tree operations** — expand or compact an entire parsed JSON tree in one
  call, not just individual terms.
- **Thread-safe cache** — mutex-protected, LRU eviction, concurrent-download
  dedup, with `Implicit` / `Cached` / `Hosted` context kinds and short-lived
  *volatile* contexts (NGSI-LD § 5.13), retired by a TTL reaper.
- **Core context** — the NGSI-LD core context is loaded at init and is always in
  force: its terms are looked up before the user context's, so a user context
  cannot redefine them.
- **Pluggable download** — you supply the HTTP callback used to fetch remote
  contexts; the library never opens a socket itself.

## API reference

The umbrella header pulls in everything:

```c
#include "corJsonld/corJsonld.h"
```

### Types

```c
// A single term mapping (see CorLdItem.h)
typedef struct CorLdItem {
  char*          name;       // short name, e.g. "temperature"
  char*          id;         // expanded IRI
  char*          type;       // "@id", "@vocab", "DateTime", … or NULL
  CorLdContainer  container;  // parsed @container (CorLdContainerNone if absent)
  unsigned char  flags;      // KJF_* classification bits (corLdExpand.h)
} CorLdItem;

// A parsed context (see CorLdContext.h)
typedef struct CorLdContext {
  char*               url;        // URL of this context (NULL for inline)
  char*               id;         // identifier (URL or broker-generated)
  char*               body;       // raw JSON body as received (may be NULL)
  CorLdContextKind     kind;       // CorLdKindImplicit | CorLdKindCached | CorLdKindHosted
  CorHashTable*       nameHT;     // name -> CorLdItem  (expansion)
  CorHashTable*       valueHT;    // IRI  -> CorLdItem  (compaction)
  char*               vocab;      // @vocab value, or NULL
  struct CorLdContext** contextV;  // child contexts (for arrays)
  int                 contexts;   // child-context count
  bool                isArray;    // true = array of child contexts
  // … cache bookkeeping (kind flags, volatile/expiry, timestamps, list linkage)
} CorLdContext;

// Download callback: return a malloc'd body, set *statusCodeP
typedef char* (*CorLdDownloadFunction)(const char* url, int* statusCodeP);

// Error callback: an HTTP status, a title and a detail - the caller decides what becomes of them
typedef void (*CorLdErrorFunction)(int status, const char* title, const char* detail);
```

`CorAlloc` comes from `corAlloc`, `CorHashTable` from `corHash`, `CorNode` from
corTree and `CorJson` from corJson. Every function that allocates takes the
caller's `CorAlloc` arena — the library holds no request-scoped memory of its own.

### Lifecycle

```c
int             corLdInit(CorAlloc* kaP, const char* coreContextUrl,
                          CorLdDownloadFunction downloadFn, CorLdErrorFunction errorFn);
void            corLdCleanup(void);
CorLdContext*    corLdCoreContext(void);
```

Initialize once at startup. Pass `NULL` for `coreContextUrl` to use the embedded
NGSI-LD core context; pass `NULL` for `downloadFn` if only inline contexts are
needed, `NULL` for `errorFn` to have errors (a cyclic `@context`, `@import`, a
scoped context) go unreported. `corLdCoreContext()` returns the loaded core.

### Parsing contexts

```c
CorLdContext*    corLdContextFromTree(CorNode* contextNode, CorAlloc* kaP, const char* baseUrl);
CorLdContext*    corLdContextFromObject(CorNode* objectNode, CorAlloc* kaP, const char* url);
CorLdContext*    corLdContextFromUrl(const char* url, CorAlloc* kaP); // uses cache + download callback
```

`corLdContextFromTree` accepts the value of a `@context` member (string, object, or
array) - not the document holding it - and resolves relative URLs against `baseUrl`
(`NULL`: none);
`corLdContextFromObject` parses a single context object; `corLdContextFromUrl`
downloads (or cache-hits) a remote context.

An application that serves @contexts itself (a broker's `/jsonldContexts/{id}`) registers
`corLdSetOwnContext(fn)`: on a cache miss `corLdContextFromUrl` asks it first, and a URL
it says is its own is answered with its context - never downloaded, never cached a
second time as a `Cached` copy.

The core context's cache entry is of kind `Cached` (NGSI-LD § 5.13.1): a local copy of
the @context at its external URL, compiled in instead of downloaded.

### Expansion / compaction

```c
char*           corLdExpand(CorLdContext* contextP, const char* name, CorAlloc* kaP,
                           CorLdItem** itemPP, bool* coreContextP);
const char*     corLdCompact(CorLdContext* contextP, const char* iri, CorAlloc* kaP);
char*           corLdPrefixExpand(CorLdContext* contextP, const char* name, CorAlloc* kaP);
bool            corLdAlreadyExpanded(const char* value);   // true if "urn:"/"http://"/"https://"
```

Expansion, first match wins:

1. Already an IRI (`urn:`, `http://`, `https://`) - a core term's IRI becomes its
   short name, any other stays as it is.
2. A `@keyword` - unchanged.
3. `prefix:suffix` - the user context's prefix, else the core's; a core term's IRI
   becomes its short name.
4. The core context. **Core first:** a user context cannot redefine a core term.
5. The user context (an array: its last context first).
6. The core `@vocab` + the name.

Compaction, first match wins:

1. Not an IRI - unchanged.
2. Reverse lookup in the user context (an array: its last context first).
3. Reverse lookup in the core context, as loaded.
4. Stripping the core `@vocab` - unless the short name it leaves is a term (user or
   core) for another IRI.
5. Only when step 4 was refused for that reason: a compact IRI `prefix:suffix`, the
   longest matching prefix of the user and the core contexts, built in `kaP`.
6. The IRI, unchanged.

### Whole-tree operations

```c
CorLdContext*    corLdExpandTree(CorNode* treeP, CorLdContext* userContextP, CorAlloc* kaP);
void            corLdCompactTree(CorNode* treeP, CorAlloc* kaP);                      // against the core context
void            corLdCompactTreeWith(CorNode* treeP, CorLdContext* ctxP, CorAlloc* kaP); // against a specific context
```

Expand or compact every term in a parsed JSON tree in a single pass — the common
case for an NGSI-LD payload going in (`expand`) or a response going out
(`compact`). `userContextP` `NULL` expands with the core context alone; a
`@context` in the tree itself replaces `userContextP` for its subtree and is
removed.

### Cache

```c
CorLdContext*    corLdCacheLookup(const char* url);
void            corLdCacheInsert(CorLdContext* contextP);
CorLdContext*    corLdCacheRemove(const char* idOrUrl);
int             corLdCacheReapVolatile(double now);        // drop expired volatile contexts
void            corLdCacheSnapshot(CorAlloc* allocP, CorLdContext*** arrPP, int* nP);
```

Thread-safe (mutex-protected). LRU eviction when full. Volatile contexts (minted so
a `Link` header can reference an inline/array `@context` by URL) are not removed
when they are fetched - identical bodies share one entry, which several requests can
reference at once. `corLdCacheReapVolatile` removes them once `expiresAt` has
passed; the application moves `expiresAt` forward when it reuses one.

## Usage example

```c
#include "corAlloc/corAlloc.h"      // CorAlloc, corAllocBufferInit
#include "corJson/corJsonCreate.h"  // CorJson, corJsonCreate
#include "corJson/corJsonParse.h"   // corJsonParse
#include "corTree/corTreeLookup.h"  // corTreeLookup
#include "corJsonld/corJsonld.h"

// User-provided download function (e.g. via libcurl) — return a malloc'd body.
static char* myDownload(const char* url, int* statusCodeP)
{
  // ... HTTP GET ...
  *statusCodeP = 200;
  return body;
}

int main(void)
{
  // 1. A corAlloc arena
  CorAlloc ka;
  char   buf[65536];
  corAllocBufferInit(&ka, buf, sizeof(buf), 65536, NULL, "jsonld");

  // 2. Init the library (NULL coreContextUrl → embedded NGSI-LD core, NULL errorFn → errors unreported)
  corLdInit(&ka, NULL, myDownload, NULL);

  // 3. Parse an inline context
  CorJson  corJson;
  CorJson* corJsonP = corJsonCreate(&corJson, &ka);
  char     json[]   = "{ \"@context\": { \"temperature\": \"https://example.org/temperature\" } }";
  CorNode* tree     = corJsonParse(corJsonP, json);

  // The VALUE of the @context member, not the whole document
  CorLdContext* ctxP = corLdContextFromTree(corTreeLookup(tree, "@context"), &ka, NULL);

  // 4. Expand / compact
  char*       iri  = corLdExpand(ctxP, "temperature", &ka, NULL, NULL);
  // iri  == "https://example.org/temperature"
  const char* name = corLdCompact(ctxP, "https://example.org/temperature", &ka);
  // name == "temperature"

  corLdCleanup();
  return 0;
}
```

## Building

```bash
make            # build libcorJsonld.a (+ .so)
make ci         # clean + install
make di         # debug + install
```

The library links statically into its consumers as `libcorJsonld.a`. Sibling
repos must be present (the build references `../<lib>/lib<lib>.a`).

## Dependencies

Sibling repos (one `.a` each):

- [`corAlloc`](https://github.com/SEAMWARE/corAlloc) — arena allocator (`CorAlloc`)
- [`corTree`](https://github.com/SEAMWARE/corTree) — the tree (`CorNode`)
- [`corJson`](https://github.com/SEAMWARE/corJson) — JSON parsing and rendering (`CorJson`)
- [`corBase`](https://github.com/SEAMWARE/corBase) — core utilities and the library log (`COR_LIB_*`)
- [`corHash`](https://github.com/SEAMWARE/corHash) — hash tables (`CorHashTable`)
- [`corLog`](https://github.com/SEAMWARE/corLog) — logging and trace levels

Plus `pthread` for the cache mutex.
