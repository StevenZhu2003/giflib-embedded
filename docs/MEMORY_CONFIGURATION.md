# Memory configuration

This is the authoritative configuration and RAM-planning document for decoder-owned memory. It covers the selected allocator backend, the `GIF_MEM_USE_BUILTIN` pool, and the port-owned handle used by the standard storage port. The output framebuffer and every other application buffer remain application-owned and are not allocated from the GIF pool.

## Select a backend

`GIF_MEM_BACKEND` is defined in `gif_config.h`. The supplied CMake build accepts `-DGIF_MEM_BACKEND=BUILTIN`, `PRIVATE`, `LIBC`, or `LVGL` and applies the matching C macro while compiling the library.

| Backend | Use when | Memory boundary |
| --- | --- | --- |
| `GIF_MEM_USE_BUILTIN` | A fixed, bounded library pool is desirable. | One aligned TLSF pool of `GIF_MEM_POOL_SIZE`; no heap fallback or automatic expansion. |
| `GIF_MEM_USE_PRIVATE` | The product provides an allocator domain. | The three primitives in `port/gif_mem_private.c` supply all decoder and standard port-handle allocations. |
| `GIF_MEM_USE_LIBC` | Hosted development or a deliberate C-runtime heap choice. | The private facade calls the C runtime heap; giflib-derived sources still do not call it directly. |
| `GIF_MEM_USE_LVGL` | The application already manages an LVGL 8.4 or 9.x allocator domain. | The private bridge uses only LVGL's public allocator API. LVGL must remain initialized while decoders are live. |

The default is BUILTIN with `GIF_MEM_POOL_SIZE = 48 KiB`. This is a build default, not a universal production recommendation. Select a capacity from the profiles below and compile that value into the library. `GIF_MEM_POOL_ALIGNMENT` affects BUILTIN only and must be a power of two of at least 8.

All backends share the same decoder allocation semantics. The library adds no allocator lock; serialized decoder calls are the documented BUILTIN contract. PRIVATE, LIBC, and LVGL products must also make their own allocator and application concurrency rules explicit.

For PRIVATE, implement only `gif_mem_private_malloc()`, `gif_mem_private_realloc()`, and `gif_mem_private_free()` in `port/gif_mem_private.c`; the common facade supplies zero-size and overflow behavior. LIBC needs no allocator port. LVGL support covers 8.4 and 9.x through public allocation APIs only; the application calls `lv_init()` before opening an LVGL-backed decoder and keeps LVGL initialized until every such decoder closes. The library never initializes, deinitializes, resets, or locks LVGL.

## Optional Restore-to-Previous feature

`GIF_ENABLE_DISPOSAL_METHOD_3` in `gif_config.h` selects Restore-to-Previous support. It defaults to `0`; that build has no method-3 snapshot state or copy path and returns `GIF_STATUS_UNSUPPORTED_FEATURE` for a method-3 Graphic Control Extension. Set it to `1` only when the accepted GIF set requires it. CMake maps `-DGIFLIB_ENABLE_DISPOSAL_METHOD_3=ON` to the same public macro.

An enabled build preserves the caller-owned framebuffer model. The additional decoder-owned payload is explained below and must be included in a BUILTIN product budget; the framebuffer itself remains separate.

## What the BUILTIN pool covers

The streaming decoder does not retain decoded frames or `SavedImages`. Its pool contains decoder and giflib state, retained palettes, one transient palette-index row buffer, TLSF metadata, and—when the standard dynamic port pattern is used—the per-stream port handle allocated with `gif_porting_mem_alloc()`. An enabled method-3 build also retains one pending packed pre-composition image rectangle for each live decoder whose most recently completed frame requests Restore to Previous.

For a declared product envelope, the source-level payload model is:

```text
payload = N × fixed_decoder_state
        + global_palette_count × palette_size
        + local_palette_count × palette_size
        + W
        + H_port(N)
        + H_previous(N)
        + TLSF control and allocation metadata
```

`N` is the maximum simultaneously live `GifDecoder` count, and `W` is the greatest accepted image-descriptor width in pixels. One row consumes one byte per pixel. The public decoder-call contract is serialized, so the maximum transient row term is `W`, not `W × N`.

`H_port(N)` is the total live payload of the port's independently allocated handles. It is product-specific: a simple memory cursor might be small, while a filesystem or driver wrapper can be much larger. Measure or bound it in the port; do not silently assume it is zero.

`H_previous(N)` is zero when disposal method 3 is disabled. When it is enabled, it is the aggregate of all simultaneously pending Restore-to-Previous snapshots. A conservative product bound is `N × R`, where `R` is the largest accepted image-rectangle area multiplied by the selected output pixel size (3 for RGB888/BGR888 or 2 for RGB565). The snapshot is tightly packed and does not include output-surface stride padding. The existing profile study predates method 3; the calculator therefore adds this declared cost explicitly rather than presenting it as evidence already covered by the study.

The caller-owned framebuffer is always separate. Let `pixel_bytes` be 3 for RGB888/BGR888 or 2 for RGB565. Its accessible storage is at least:

```text
(canvas_height - 1) × stride_bytes + canvas_width × pixel_bytes
```

For a tightly packed surface this is `canvas_width × canvas_height × pixel_bytes`. A heuristic such as framebuffer size greater than or equal to the configured GIF pool can be useful for whole-system RAM planning, but it is not a decoder requirement.

## Choose a BUILTIN sizing profile

Use [`tools/estimate_builtin_pool.py`](../tools/estimate_builtin_pool.py) with declared product limits. It is a planning utility: it does not inspect a GIF, emulate the target port, or prove that any arbitrary corpus cannot exhaust memory. Final product validation must run the target or an equivalent allocator path with the actual accepted GIF corpus and lifecycle constraints.

| Profile | Meaning | When to choose it |
| --- | --- | --- |
| Payload-derived | Arithmetic model of the declared state, palettes, one row, port handles, optional method-3 snapshots, ABI values, TLSF control, and an adjustable reserve. | A resource-controlled product that has measured its own corpus and validates its chosen capacity. |
| Balanced | `max(payload-derived + 16 KiB, W + 32 KiB + 40 KiB × N + H_port(N))`. The payload-derived term includes `H_previous(N)`. | The normal starting point for a general serialized product. Its floor encloses the completed random mixed-lifecycle boundary matrix for methods 0/1/2; enabled method 3 adds the declared snapshot payload. |
| Hardened | `max(payload-derived + 128 KiB, W + 128 KiB + 64 KiB × N + H_port(N))`. The payload-derived term includes `H_previous(N)`. | A product willing to trade more RAM for stronger evidence against varied lifetimes, source errors, holes, and wide transient rows. It passed the declared adverse-lifecycle study for methods 0/1/2; enabled method 3 adds the declared snapshot payload and still needs product validation. |

The calculator defaults describe the verified ARM32 ABI. Override its structure-size inputs for another ABI, and round the selected result upward to the target's linker/allocation granularity.

### Orientation matrix

The following values are generated by the calculator with its verified ARM32 defaults, 256-entry global and local palettes per live decoder, and a 64-byte port handle. They are only an order-of-magnitude aid; use the calculator for the actual product envelope.

| N | Maximum width W | Balanced | Hardened |
| ---: | ---: | ---: | ---: |
| 1 | 320 | 72.38 KiB | 192.38 KiB |
| 1 | 800 | 72.84 KiB | 192.84 KiB |
| 2 | 800 | 112.91 KiB | 256.91 KiB |
| 4 | 1,920 | 194.12 KiB | 386.12 KiB |
| 8 | 4,096 | 356.50 KiB | 644.50 KiB |

Example:

```powershell
python tools/estimate_builtin_pool.py `
    --live-decoders 2 `
    --max-row-width 800 `
    --port-handle-bytes 64 `
    --disposal3-snapshot-bytes-per-decoder 0
```

The calculator offers `--json` for planning scripts. Its inputs are assertions supplied by the user, not measurements or guarantees.

## Evidence and limits

[BUILTIN_POOL_SIZING_STUDY.md](BUILTIN_POOL_SIZING_STUDY.md) is the canonical evidence record: corpus shape, workload classes, OOM classification, boundary results, long-duration endurance, and acceptance criteria. It establishes the meaning of the Balanced and Hardened profiles; it does not replace product validation.

The profiles do not budget an application framebuffer, unrelated application allocations, unbounded driver caches, calls from multiple threads or interrupt contexts, a port handle larger than `H_port(N)`, or GIFs beyond the declared width and palette limits. They also do not replace product validation of enabled disposal method 3; declare the snapshot input and validate the accepted corpus.

## Port allocation boundary

`gif_porting_mem_alloc()` and `gif_porting_mem_free()` are porting-layer bridge functions only. They let `port/gif_porting.c` place one independently allocated stream handle in the selected decoder allocator domain. Application code must not include `gif_porting_memory.h` or call those functions, `gif_mem_*`, or `gif_tlsf_*`. See [PORTING_GUIDE.md](PORTING_GUIDE.md) for the exact ownership contract and [MEMORY_API_EVALUATION.md](MEMORY_API_EVALUATION.md) for the deliberately non-implemented public-memory-API evaluation.
