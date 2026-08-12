# Memory configuration

This document sizes decoder-owned memory. It is intentionally separate from [USER_GUIDE.md](USER_GUIDE.md), which does not prescribe an application's output-storage or display design.

## BUILTIN pool

`GIF_MEM_BACKEND` defaults to `GIF_MEM_USE_BUILTIN`. It owns one explicitly aligned TLSF pool and never expands it or falls back to a C library heap. Its size is set by `GIF_MEM_POOL_SIZE` in `include/gif_config.h`; the default is 48 KiB.

The streaming decoder does not retain decoded frames or `SavedImages`. For a supported stream with both a global and local 256-entry palette, its maximum live allocation payload is bounded by:

```text
D_payload(W) = sizeof(GifDecoder)
             + sizeof(GifFileType)
             + sizeof(GifFilePrivateType)
             + 2 × (sizeof(ColorMapObject) + 256 × sizeof(GifColorType))
             + W × sizeof(GifPixelType)
```

`W` is the largest image width the product accepts, and is no larger than the GIF canvas width. The two palette terms cover the moment when a local colour table is active while the global table remains allocated. The row buffer uses one palette-index byte per image pixel.

For the verified 32-bit ARM build:

```text
D_payload(W) = 26,584 + W bytes
```

This excludes TLSF metadata and a product safety margin. Choose:

```text
GIF_MEM_POOL_SIZE >= D_payload(W)
                   + TLSF_control_and_allocation_metadata
                   + safety_margin
```

With the current 32-bit ARM default pool, TLSF control metadata is about 1,340 bytes. Per-allocation metadata, alignment rounding, and the pool sentinel also consume capacity. Start with at least a 4 KiB product-specific safety margin, then validate the largest intended resources and the actual allocation pattern.

## Static-RAM planning

BUILTIN reserves exactly:

```text
P = GIF_MEM_POOL_SIZE
R_library = P
```

For a tightly packed RGB888/BGR888 output surface, its storage is:

```text
F = canvas_width × canvas_height × 3 bytes
R_decoder_plus_output = P + F
```

For RAM-budget planning, the recommended balance condition is:

```text
F >= P
```

It means visible image storage is at least as large as the decoder's complete reserved static allocation. It is not a functional requirement: an application may use a smaller output allocation and still decode correctly if its selected pool satisfies the decoder's own peak demand. A surface with a wider stride consumes `(canvas_height - 1) × stride_bytes + canvas_width × 3` bytes instead. Conversely, checking only `F >= D_payload(W)` is insufficient when the configured pool `P` is larger than the live decoder payload.

With the default 48 KiB pool, the balance condition corresponds to at least 16,384 RGB888 pixels. A 128 × 64 surface does not meet that balance target; 240 × 320 does. Products with smaller outputs can select a smaller pool after measuring their accepted image width, palette usage, failures, and safety margin.

## Other backends

PRIVATE, LIBC, and LVGL do not reserve this library-owned TLSF pool. PRIVATE uses the application's allocator domain; LIBC uses the selected C runtime heap. LVGL reuses the allocator domain already configured by LVGL. In all three cases, the decoder's live payload bound still helps estimate demand, but total system reservation and fragmentation are properties of the selected provider.

## LVGL backend

`GIF_MEM_USE_LVGL` supports LVGL 8.4 and LVGL 9.x. The private bridge calls only `lv_mem_alloc()`, `lv_mem_realloc()`, and `lv_mem_free()` on LVGL 8.4, or `lv_malloc()`, `lv_realloc()`, and `lv_free()` on LVGL 9.x. It does not include or call LVGL core, TLSF, pool, global-state, monitor, or lock internals.

The application owns the LVGL lifecycle: it must call `lv_init()` before `gif_decoder_open()` and must not call `lv_deinit()` while an LVGL-backed decoder or its allocations remain active. The bridge does not initialize, deinitialize, reset, or reserve an LVGL pool. An LVGL allocation failure becomes the existing `GIF_STATUS_OUT_OF_MEMORY` through the common facade. Concurrency follows the application's LVGL locking and threading rules; this library adds no additional lock.

The project does not bundle, modify, or link LVGL by default. A product selecting this backend supplies the LVGL public header and library in its own build. The namespaced `gif_tlsf_*` implementation is compiled only for BUILTIN, so it does not collide with LVGL's allocator or TLSF symbols.
