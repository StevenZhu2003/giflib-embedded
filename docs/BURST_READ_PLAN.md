# BURST_READ Design and Implementation Plan

## 1. Decision and purpose

BURST_READ is an optional, synchronous input adapter between giflib's small byte requests and the platform's sequential byte-source primitive. Instead of asking the port for exactly every small request made by giflib, it maintains a private FIFO and requests a contiguous batch when the FIFO reaches a configured low-water mark.

The name deliberately describes embedded storage transactions: after the port has established whatever source transaction it needs, it can return a continuous batch of bytes, as with common serial-memory or bus burst transfers. BURST_READ does not imply a background task, DMA policy, seek support, or a new application-facing API.

The feature is disabled by default. It changes neither the public decoder lifecycle nor the `gif_porting.c` function set.

```text
giflib InternalRead(requested bytes)
        ↓
private BURST_READ adapter
        ↓ consumes requested bytes
internal FIFO ────────────────────┐
        ↑ low-water refill        │
        └──── gif_porting_read(handle, contiguous free span, ...)
```

## 2. Preserved contracts and scope

- `gif_decoder_open()`, `gif_decoder_bind_output()`, `gif_decoder_next_frame()`, and `gif_decoder_close()` remain unchanged.
- `gif_porting_open()`, `gif_porting_read()`, and `gif_porting_close()` remain the only porting operations. A porter does not implement FIFO logic and does not call a new BURST_READ API.
- The source remains forward-only. No seek, rewind, file-size query, pathname requirement, filesystem dependency, display dependency, or scheduler dependency is introduced.
- BURST_READ runs synchronously in the existing input callback path. The current decoder call waits only while its port's normal `gif_porting_read()` call is in progress.
- The application continues to own its framebuffer and optional disposal-method-3 snapshot. Neither storage is used as FIFO backing.
- BURST_READ is not a platform benchmark, a DMA integration, or a general asynchronous I/O framework.

## 3. Configuration boundary

`gif_config.h` becomes the canonical source of three public compile-time selectors:

```c
#ifndef GIF_ENABLE_BURST_READ
#define GIF_ENABLE_BURST_READ 0
#endif

#ifndef GIF_BURST_READ_FIFO_SIZE
#define GIF_BURST_READ_FIFO_SIZE 1024U
#endif

#ifndef GIF_BURST_READ_LOW_WATERMARK
#define GIF_BURST_READ_LOW_WATERMARK 256U
#endif
```

The proposed defaults are a starting configuration rather than a performance claim. A product can select a smaller or larger FIFO based on its storage transaction cost and pool budget.

When enabled, compilation rejects:

- a selector other than `0` or `1`;
- a zero FIFO capacity;
- a low-water mark greater than or equal to FIFO capacity; and
- a FIFO capacity that cannot be represented by the internal size calculations.

When disabled, the FIFO array and its metadata must not be compiled into `GifDecoder`; the size and watermark macros have no runtime effect.

CMake adds matching cache configuration:

```text
GIFLIB_ENABLE_BURST_READ
GIFLIB_BURST_READ_FIFO_SIZE
GIFLIB_BURST_READ_LOW_WATERMARK
```

The target forwards the resulting definitions in the same way it currently forwards other public configuration selectors. Direct builds can define the `GIF_*` macros themselves.

## 4. Ownership and private state

The FIFO is library-owned, not application-provided. In an enabled build it is a fixed array embedded in the private `GifDecoder` object, together with its head index, tail index, stored-byte count, and remembered source-terminal state.

The decoder is already created through the selected `gif_mem_*` backend. Embedding the FIFO therefore adds one known compile-time amount to each live decoder allocation without introducing a second variable-size allocation or a new ownership rule. A port handle remains port-owned and is still allocated and released through `gif_porting_mem_alloc()` and `gif_porting_mem_free()` by the port.

The internal layout is intentionally a ring, not a one-shot staging array:

```text
FIFO capacity = C
read index    = next byte given to giflib
write index   = next free byte filled by the port
stored count  = unread bytes, 0 .. C
```

The FIFO has one consumer and one synchronous producer in the same decoder call chain. It needs no lock, task notification, or multi-producer behavior.

## 5. Read behavior

The existing `gif_decoder_read_bridge()` remains giflib's callback. Its direct calls to `gif_porting_read()` are routed through a private burst-read helper when the selector is enabled.

For each giflib request, the helper:

1. copies available FIFO bytes to giflib's destination;
2. checks whether the remaining FIFO count is at or below the low-water mark;
3. requests the largest contiguous free FIFO span from `gif_porting_read()`;
4. continues filling until the FIFO is full, the port reports a terminal result, or the port cannot make positive progress;
5. uses a second request only when a ring wrap leaves another contiguous free span; and
6. continues serving the original giflib request until it is satisfied or the established source terminal prevents further bytes.

This makes the port's requested size commonly approach the configured FIFO capacity, while preserving the legal short-read behavior of the existing contract. A port may still return fewer bytes than requested.

### 5.1 Terminal-result handling

The current porting contract permits `GIF_PORTING_EOF` or `GIF_PORTING_IO_ERROR` together with final valid bytes. BURST_READ must keep any such bytes available to giflib and then expose the same terminal outcome at the first point where no more source byte can be supplied.

The implementation must record a pending terminal result separately from the FIFO byte count. It must define and test the exact translation for:

- `GIF_PORTING_OK` with positive bytes;
- `GIF_PORTING_OK` with zero bytes, which remains non-progressing input;
- `GIF_PORTING_EOF` with zero or positive bytes;
- `GIF_PORTING_IO_ERROR` with zero or positive bytes; and
- close while unread FIFO bytes remain.

The disabled and enabled configurations must produce the same public decoder status sequence for each scheduled source outcome. Existing decoder behavior is the reference for those assertions; the implementation must not assume that EOF and I/O terminal cases are interchangeable.

## 6. Memory model and estimator work

BURST_READ adds fixed decoder-domain storage in an enabled build. It must be included in the allocator model and excluded from the application-owned categories:

```text
enabled incremental storage per live decoder
≈ FIFO capacity + ring metadata + target ABI alignment
```

The authoritative value is the measured `sizeof(GifDecoder)` for the selected target ABI and feature combination, not an assumed byte count. The current ARM sizing probe and `tools/gif_builtin_pool_estimate.py` must be extended to distinguish at least:

```text
disposal method 3: disabled / enabled
BURST_READ:         disabled / enabled
FIFO capacity:       configured bytes
```

The calculator will add the enabled per-decoder FIFO contribution to each profile because it is part of the selected allocator domain. The application framebuffer, the application-owned disposal-method-3 snapshot, and the port handle remain separate terms with their existing ownership rules.

The initial change does not claim that a particular FIFO capacity has undergone the existing long-running fragmentation study. The updated model and targeted fixed-pool tests establish accounting; product-specific pool validation remains necessary for a chosen FIFO capacity and concurrency limit.

## 7. Test plan

### 7.1 Functional equivalence

Add an instrumented test port that records read request sizes and can return a reproducible sequence of short reads and terminal statuses. Decode the existing valid, animated, local/global-palette, interlaced, RGB888, RGB565, disposal-2, and optional disposal-3 fixtures with BURST_READ disabled and enabled. Assert equal frame output, frame metadata, and public terminal status.

### 7.2 FIFO state coverage

- initial empty FIFO and initial fill;
- exact low-water boundary and one byte above/below it;
- FIFO wrap while a single giflib request spans buffered and newly read bytes;
- request larger than FIFO capacity;
- repeated short positive source reads while filling;
- zero-byte `GIF_PORTING_OK`;
- final-byte EOF and final-byte I/O status;
- decoder open failure while reading the GIF header;
- early `gif_decoder_close()` with unread FIFO bytes; and
- repeated and concurrently live decoders, proving state does not cross streams.

The tests also assert that BURST_READ requests a batch larger than representative one-byte giflib requests when the source permits it. They do not require an exact number of port calls, because short-read schedules are allowed by the porting contract.

### 7.3 Build and allocator coverage

- Default-off host matrix: existing behavior and no FIFO storage.
- Enabled host matrix: PRIVATE, BUILTIN, LIBC, and both supported LVGL public-API configurations.
- Enabled combined configuration with disposal method 3.
- ARM BUILTIN and PRIVATE builds, including the existing unresolved-symbol audit.
- BUILTIN constrained-pool and repeated lifecycle coverage updated for the selected FIFO size.
- Existing host instrumentation and compatibility harness run in an enabled configuration without expanding the matrix unnecessarily.

## 8. Documentation plan

Update only canonical locations after implementation:

- [PORTING_GUIDE.md](PORTING_GUIDE.md): explain that the port remains a raw sequential byte provider; the library performs optional batching above it.
- [MEMORY_CONFIGURATION.md](MEMORY_CONFIGURATION.md): describe the enabled per-decoder FIFO term, configuration macros, and calculator inputs.
- [USER_GUIDE.md](USER_GUIDE.md): mention the optional configuration and link to the memory and porting details without exposing private state.
- [README.md](../README.md): add only a concise supported/configurable capability statement when implementation is complete.
- [TODO_LIST.md](TODO_LIST.md): mark this plan complete only after code, tests, docs, and target sizing values agree.

## 9. Implementation order and completion criteria

### Phase 1 — configuration boundary (complete)

The first implementation stage adds the three `GIF_*` configuration macros,
their default values and enabled-mode validation in `gif_config.h`. CMake now
accepts matching cache entries, forwards the selected values to the library and
all locally compiled decoder targets, and rejects an enabled zero-capacity FIFO
or a low-water mark at or above FIFO capacity.

This stage deliberately adds no FIFO object, input helper, porting change, or
read-path behavior. A default-off host regression and an enabled-config host
regression both pass; a deliberately invalid enabled configuration is rejected
at CMake configuration time. The next stage freezes source-terminal behavior
before any FIFO state is introduced.

### Phase 2 — source-terminal baseline (complete)

The host test port can now emit one zero-byte `GIF_PORTING_OK` result at a
chosen source offset, in addition to its existing short-read, EOF, and I/O
schedules. Focused facade tests freeze the current public behavior under
single-byte source reads:

- a complete GIF whose final trailer byte accompanies `GIF_PORTING_EOF` yields
  its frame and then `GIF_STATUS_END_OF_STREAM`;
- the same complete GIF whose final trailer byte accompanies
  `GIF_PORTING_IO_ERROR` yields the same frame and then
  `GIF_STATUS_END_OF_STREAM`; and
- a zero-byte `GIF_PORTING_OK` at the first frame read yields a sticky
  `GIF_STATUS_IO_ERROR`.

These outcomes are the reference for the later FIFO terminal-state adapter.
The test port and decoder tests changed only to make that behavior explicit;
the decoder input path is still direct.

### Phase 3 — private FIFO input adapter (complete)

`GifDecoder` now contains the FIFO and its ring metadata only when
`GIF_ENABLE_BURST_READ` is enabled. The existing giflib read callback remains
the single input boundary; it selects a private FIFO helper in an enabled
build and retains the previous direct port read path otherwise.

The helper fills contiguous FIFO spans through `gif_porting_read()`, including
the second span after a ring wrap. It remembers whether a terminal port result
arrived with usable bytes: those bytes are delivered before the terminal is
reported, whereas a zero-byte terminal result is reported on the next byte
request. This preserves the Phase 2 public status sequence even if the FIFO
has read ahead of giflib.

The default-off host matrix (13 tests) and the enabled host matrix (12 tests)
both pass. The existing single-byte short-read test now explicitly documents
the enabled adapter's final EOF probe; it continues to assert the prior
direct-read count when the feature is disabled. The next stage adds dedicated
instrumentation for FIFO thresholds, wrap, and requested batch sizes.

### Remaining implementation order

1. Add wrap- and threshold-focused FIFO tests, then run broader functional-equivalence tests.
2. Add the enabled build configurations and allocator/pool checks.
3. Measure target `GifDecoder` sizes, update the estimator and memory documentation, then verify the resulting values.
4. Update user-facing documentation, run the normal host matrix, enabled matrix, ARM checks, example build, and documentation checker.

The work is complete only when default-off trimming, enabled input equivalence, terminal-result behavior, fixed-pool accounting, target sizing, and documentation all agree. A real-target storage measurement may guide a product's chosen FIFO depth, but does not block the correctness of the configurable feature itself.
