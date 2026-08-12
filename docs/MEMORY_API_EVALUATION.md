# Public Memory API evaluation

## Decision for this stage

Do **not** install or publicly expose the present `gif_mem_*` subsystem.
LIBC completes the decoder's allocator backend set, but it does not make the
underlying allocator a stable application service yet. The installed public
surface remains `gif_decoder.h` and `gif_config.h` only.

`gif_mem_*`, `gif_mem_builtin_*`, `gif_mem_private_*`, and `gif_tlsf_*` remain
private implementation boundaries. In particular, `gif_tlsf_*` is a derived
third-party implementation detail, not an API applications should include.

## Why the existing facade is not ready to publish

The current facade correctly normalizes allocation semantics for decoder use:

- zero-size `malloc` and `calloc` requests return `NULL`;
- `calloc` and `realloc_array` check multiplication overflow;
- `realloc(NULL, n)` allocates and `realloc(p, 0)` frees;
- a failed non-zero `realloc` leaves the old allocation valid;
- `free(NULL)` does nothing.

Those rules make it a good decoder-internal adapter, but not yet a complete
application allocator contract. BUILTIN owns one process-global static pool,
initializes it lazily, provides no deinitialization/reset lifecycle, has no
allocation accounting API, and is not synchronized. PRIVATE and future LVGL
providers may have their own initialization and locking requirements. Exposing
raw allocation functions now would make applications depend on ambiguous
ownership, pool-sharing, alignment, and thread-safety behavior.

## Boundaries considered

| Candidate | Assessment |
| --- | --- |
| Publish `gif_mem_malloc()` et al. unchanged | Rejected: these are decoder-private names and do not declare a usable application lifecycle or concurrency contract. |
| Publish TLSF or a direct pool handle | Rejected: exposes a derived third-party implementation and couples callers to BUILTIN only. |
| Publish a generic `GifMemory` service | Promising later: it can make ownership, capacity, alignment, lifecycle, and optional locking explicit while retaining one consistent API across BUILTIN, PRIVATE, LIBC, and LVGL. |
| Accept an application-owned external buffer only | Promising as an optional later extension, but it needs a distinct initialization/ownership API and must not be confused with the decoder's private pool. |

## Recommended future contract

If a public service is needed, introduce it in a separate breaking-design
review as a named service object, for example `GifMemoryService`, rather than
renaming the private facade. The proposed service should answer these questions
in its public contract before implementation:

1. **Ownership:** Does the caller own an external pool buffer? Who may destroy
   the service, and must every allocation be released first?
2. **Lifecycle:** Is initialization explicit? Can a static default service be
   used before application initialization? Is reset allowed only when no blocks
   are outstanding?
3. **Alignment:** What alignment is guaranteed for every allocation, and what
   alignment is required for an external pool?
4. **Thread safety:** Is the default service explicitly single-threaded, or do
   callers provide lock hooks? The answer must be identical in wording for all
   backends, even when the selected provider has stronger guarantees.
5. **OOM:** Return `NULL` consistently, never silently switch backend, and
   preserve old blocks after failed resize.
6. **Backend compatibility:** A service cannot return a block from BUILTIN and
   later free it through PRIVATE, LIBC, or LVGL. A block belongs to exactly one
   service/provider domain for its complete lifetime.

The future public names should be distinct from the implementation facade,
such as `gif_memory_service_alloc()` and `gif_memory_service_free()`. The
private `gif_mem_*` wrappers may then dispatch through that service internally,
but applications need not know the backend selection detail.

## U8g2 buffer use case

An application can use the same physical RAM budget for a U8g2 display buffer
and the GIF decoder, but it should not currently obtain that buffer via
`gif_mem_malloc()`. U8g2 supports application-supplied display buffers in
configurations that allow a caller-provided buffer; the application should
allocate or statically reserve that buffer in its own allocator domain and
keep it alive for U8g2's documented lifetime.

Sharing the current BUILTIN pool would create practical risks: U8g2 allocation
may fragment the decoder pool, GIF playback could fail after display setup,
and a global unsynchronized pool would couple independent subsystem lifetimes.
The existing README memory formula remains valid only when the framebuffer is
separately owned. A later explicit `GifMemoryService` could support this use
case only after it offers a capacity budget, outstanding-allocation accounting,
and clear application serialization/locking rules.

## Backend implications

- **BUILTIN:** a service would need explicit pool ownership and initialization;
  it must remain bounded and never fall back to libc.
- **PRIVATE:** service calls must reach exactly one application allocator
  domain. Provider initialization and locking remain application-defined.
- **LIBC:** useful for hosted use and integration bring-up, but its allocator
  behavior must not become the public semantic baseline.
- **LVGL (future):** call only stable LVGL public allocation APIs after LVGL
  initialization. Do not expose `lv_tlsf_*` or LVGL internal memory-core APIs.

Until this contract is reviewed and implemented separately, application-facing
memory use should remain application-owned and decoder allocation remains an
internal library concern.
