# User Guide

This guide is the application-facing reference for `giflib-embedded`. It is organized around three questions:

1. What must be ready before the application calls the decoder?
2. What exactly does each public type and function mean?
3. How does one complete GIF decode lifecycle work in an application?

Only `gif_decoder.h` and `gif_config.h` are public headers. `gif_lib.h`, `gif_porting.h`, the `gif_mem_*` headers, TLSF headers, and all files under `src/` and `vendor/` are private implementation boundaries. Do not include them from application code.

## 1. Before Integration

Complete the following work before the application calls `gif_decoder_open()`.

### 1.1 Select one decoder memory backend

The selection controls decoder-owned dynamic allocations only. It does not determine the application's framebuffer, display, timing, or resource-storage policy.

| Backend | Intended use | Required action |
| --- | --- | --- |
| `GIF_MEM_USE_BUILTIN` | Default for bounded embedded use. | Select a suitable fixed-pool size with `GIF_MEM_POOL_SIZE`. |
| `GIF_MEM_USE_PRIVATE` | The product has its own allocator domain. | Implement the three primitives in `port/gif_mem_private.c`. |
| `GIF_MEM_USE_LIBC` | Hosted development or a product that deliberately uses the C runtime heap. | No allocator port is required. |
| `GIF_MEM_USE_LVGL` | The product already owns an LVGL 8.4 or 9.x allocator domain. | Initialize LVGL before opening a decoder and keep it initialized until all decoders close. |

`GIF_MEM_BACKEND` defaults to `GIF_MEM_USE_BUILTIN`. BUILTIN owns one fixed TLSF pool, never expands it, and does not fall back to a C library heap. PRIVATE allocation, resize, and release must all use one application allocator domain. LIBC is explicit: it uses the C runtime heap behind the library's private allocation facade, rather than causing decoder or giflib-derived sources to call heap functions directly. LVGL uses only LVGL's public allocation, resize, and release API; the library never calls `lv_init()` or `lv_deinit()`. `GIF_MEM_POOL_SIZE` and `GIF_MEM_POOL_ALIGNMENT` affect only BUILTIN.

The library does not add allocator locks. The application must serialize decoder activity unless its selected provider and wider application design intentionally support concurrency.

For fixed-pool sizing and static-RAM planning, see [MEMORY_CONFIGURATION.md](MEMORY_CONFIGURATION.md).

### 1.2 Complete the platform byte-source port

The decoder consumes sequential bytes. Before use, implement the open/read/close operations in the single target-owned file `port/gif_porting.c`. The application selects a resource through `GifDecoderConfig.source_identifier`; the port alone interprets that opaque value and obtains bytes from the target.

The port must follow these non-negotiable rules:

- open returns a non-null handle only after complete success;
- every read reports its actual byte count and never exceeds the requested count;
- a successful read must make progress, so it must not report OK with zero bytes;
- EOF means no later byte is available, and may accompany final valid bytes; and
- close is null-safe and releases every successfully opened source exactly once.

The port does not need a pathname, filesystem, seek operation, file size, or a specific SDK. The teaching model in [PORTING_GUIDE.md](PORTING_GUIDE.md) uses the deliberately imaginary `storage_open()`, `storage_read()`, and `storage_close()` operations to show the abstraction; replace those names with the equivalent source operations available on the target. The same guide contains the complete contract, implementation rules, and a memory-source reference.

### 1.3 Prepare application services

The application must provide three product-specific responsibilities outside the library:

- select a resource identifier that the port understands;
- provide writable output storage in a supported pixel format; and
- decide how completed frames are consumed and how `GifFrameInfo.delay_ms` affects playback timing.

The selected resource identifier must remain valid until `gif_decoder_close()` returns. For example, a path string must not refer to expired stack storage, and a memory descriptor must outlive the active decoder. Output storage remains application-owned for its entire bound lifetime.

## 2. API Specification

### 2.1 Public types

#### `GifDecoder`

`GifDecoder` is an opaque library-owned decoder instance. Applications receive it only through `gif_decoder_open()` and must release it with `gif_decoder_close()`. Applications must not inspect, copy, allocate, or free this type directly.

#### `GifDecoderConfig`

```c
typedef struct GifDecoderConfig {
    const void *source_identifier;
} GifDecoderConfig;
```

`source_identifier` selects one resource for one `gif_decoder_open()` call. Its concrete type is an agreement between the application and `port/gif_porting.c`: it may be a pathname, a memory-resource descriptor, a flash asset descriptor, or another target-specific key. The decoder passes it to the port without interpreting it. The referenced value must remain valid until the decoder closes.

#### `GifStreamInfo`

`GifStreamInfo` is output from `gif_decoder_open()` after the GIF logical-screen descriptor is parsed.

| Field | Meaning |
| --- | --- |
| `canvas_width` | Logical-screen width in pixels. |
| `canvas_height` | Logical-screen height in pixels. |
| `background_color_index` | GIF global background palette index. |
| `color_resolution` | GIF colour-resolution field in bits. |
| `has_global_color_table` | Non-zero when the stream contains a global colour table. |

The values describe the selected GIF stream. They are not storage allocation requests and do not transfer ownership of any resource.

#### `GifPixelFormat`

```c
typedef enum GifPixelFormat {
    GIF_PIXEL_RGB888 = 0,
    GIF_PIXEL_BGR888 = 1
} GifPixelFormat;
```

`GIF_PIXEL_RGB888` writes each output pixel in red, green, blue byte order. `GIF_PIXEL_BGR888` writes blue, green, red byte order. Both formats use three bytes per pixel.

#### `GifOutputSurface`

```c
typedef struct GifOutputSurface {
    void *pixels;
    size_t capacity_bytes;
    size_t stride_bytes;
    GifPixelFormat pixel_format;
} GifOutputSurface;
```

`GifOutputSurface` describes application-owned writable output storage.

| Field | Requirement |
| --- | --- |
| `pixels` | Non-null writable storage. The library never owns or frees it. |
| `capacity_bytes` | Total accessible bytes beginning at `pixels`. It must be at least `(canvas_height - 1) × stride_bytes + canvas_width × 3`. |
| `stride_bytes` | Byte distance from one canvas row to the next. It must be at least `canvas_width × 3`. |
| `pixel_format` | One of the supported `GifPixelFormat` values. |

The library copies the descriptor, not the pixel data. The storage it identifies must stay valid and writable after a successful bind until `gif_decoder_close()` returns. Do not change its capacity, stride, format, or ownership while the decoder is active.

#### `GifFrameInfo`

`GifFrameInfo` is output from each successful `gif_decoder_next_frame()` call.

| Field | Meaning |
| --- | --- |
| `frame_index` | Zero-based decoded frame number. |
| `delay_ms` | GIF frame delay in milliseconds. Zero remains zero. The decoder never waits. |
| `image_left`, `image_top` | Image rectangle origin within the logical canvas. |
| `image_width`, `image_height` | Image rectangle dimensions. |
| `updated_left`, `updated_top` | Origin of the canvas rectangle reported as updated. |
| `updated_width`, `updated_height` | Dimensions of the reported updated rectangle. |

The completed canvas is already composited when the call returns `GIF_STATUS_OK`. The application chooses whether to display, queue, copy, delay, or otherwise consume it. The reported updated rectangle conservatively covers the image rectangle and, when the preceding frame used disposal method 2, the rectangle restored to the logical background immediately before this frame was composed. It may therefore include transparent or otherwise visually unchanged pixels.

#### `GifStatus`

Every public operation returns a `GifStatus` unless otherwise specified.

| Status | Meaning and required response |
| --- | --- |
| `GIF_STATUS_OK` | The operation succeeded. Continue the documented lifecycle. |
| `GIF_STATUS_END_OF_STREAM` | The GIF trailer was reached. This is normal completion; close the decoder. |
| `GIF_STATUS_INVALID_ARGUMENT` | A required pointer, configuration value, or public argument was invalid. Correct application integration. |
| `GIF_STATUS_OUT_OF_MEMORY` | The selected decoder backend could not allocate. Adjust the decoder allocation budget or application concurrency. |
| `GIF_STATUS_IO_ERROR` | The port could not open or continue reading the source. Diagnose the target byte-source port. |
| `GIF_STATUS_UNEXPECTED_EOF` | Input ended before a complete GIF structure. Treat the resource as truncated or diagnose read progress. |
| `GIF_STATUS_INVALID_FORMAT` | The resource is not a valid supported GIF. Reject or replace it. |
| `GIF_STATUS_UNSUPPORTED_FEATURE` | The GIF uses intentionally unimplemented semantics. Reject it or consult [TODO_LIST.md](TODO_LIST.md). |
| `GIF_STATUS_BUFFER_TOO_SMALL` | The bound output storage capacity or stride cannot represent the canvas. Provide a valid surface before retrying with a new decoder. |
| `GIF_STATUS_INTERNAL_ERROR` | An internal invariant failed. Close, preserve diagnostics, and report a reproducible case. |
| `GIF_STATUS_INVALID_STATE` | A function was called in the wrong lifecycle state. Correct the call order. |

Use `gif_status_string()` for diagnostics only. Make program decisions with the enum value, not with the returned text.

### 2.2 Public functions

#### `gif_decoder_open`

```c
GifStatus gif_decoder_open(const GifDecoderConfig *config,
                           GifDecoder **out_decoder,
                           GifStreamInfo *out_stream);
```

Opens the application-selected source through the port and reads the GIF logical-screen descriptor.

| Parameter | Direction | Contract |
| --- | --- | --- |
| `config` | input | Non-null configuration with a non-null `source_identifier`. |
| `out_decoder` | output | Non-null location that receives a new library-owned decoder on success. It receives `NULL` on failure. |
| `out_stream` | output | Non-null location that receives stream information on success and is cleared on failure. |

On `GIF_STATUS_OK`, the caller owns one active `GifDecoder` lifecycle and must later call `gif_decoder_close()`. On any failure, no decoder is returned. If the port had opened a source before failure, the library closes it itself.

#### `gif_decoder_bind_output`

```c
GifStatus gif_decoder_bind_output(GifDecoder *decoder,
                                  const GifOutputSurface *surface);
```

Validates and binds application-owned output storage, then initializes the logical canvas background.

| Parameter | Direction | Contract |
| --- | --- | --- |
| `decoder` | input/output | A non-null decoder returned by successful `gif_decoder_open()`. |
| `surface` | input | A non-null descriptor that satisfies the `GifOutputSurface` requirements. |

Call this after a successful open and before any frame decode. A successful call copies the descriptor; it does not copy, allocate, or own the pixel storage. A validation failure leaves the decoder open and unbound, so the application may correct the descriptor and bind it again. A successful bind cannot be repeated during the same lifecycle.

#### `gif_decoder_next_frame`

```c
GifStatus gif_decoder_next_frame(GifDecoder *decoder,
                                 GifFrameInfo *out_frame);
```

Reads and composites the next GIF image into the bound output storage.

| Parameter | Direction | Contract |
| --- | --- | --- |
| `decoder` | input/output | A non-null open decoder with a successfully bound output surface. |
| `out_frame` | output | Non-null location that receives metadata only when the return status is `GIF_STATUS_OK`. |

Call repeatedly while it returns `GIF_STATUS_OK`. After `GIF_STATUS_END_OF_STREAM` or any error, stop calling it and close the decoder. Decode errors are sticky; an additional call repeats the same failure status rather than becoming successful.

#### `gif_decoder_close`

```c
void gif_decoder_close(GifDecoder *decoder);
```

Releases all library-owned state and closes the port-owned byte source associated with an active decoder. `NULL` is accepted. It does not free the selected resource identifier or the application output storage. Call it once for every successful open, including after bind failure, decode failure, early cancellation, and normal end of stream.

#### `gif_status_string`

```c
const char *gif_status_string(GifStatus status);
```

Returns a static null-terminated description of a status. The returned pointer is library-owned, read-only, and must not be freed or modified. Its content is suitable for logs, not for program control flow.

### 2.3 Lifecycle and call constraints

The supported call sequence is:

```text
GifDecoderConfig + application output storage
                  |
                  v
          gif_decoder_open()
                  |
                  v
       gif_decoder_bind_output()
                  |
                  v
       gif_decoder_next_frame()  -- GIF_STATUS_OK --> repeat
                  |
                  +-- GIF_STATUS_END_OF_STREAM --> gif_decoder_close()
                  |
                  +-- any error ----------------> gif_decoder_close()
```

`gif_decoder_next_frame()` before a successful bind is invalid. Do not continue decoding after end of stream or a decode failure, and never use a decoder after close. A decoder, its source identifier, and its bound output storage form one active lifecycle. Start a new lifecycle with `gif_decoder_open()` to decode another resource.

## 3. How to Use

This chapter follows one decoder lifecycle in the order an application performs it. It assumes the configuration and byte-source port from Section 1 are already complete. The application chooses the resource and owns the output storage, presentation, and timing policy.

### 3.1 Select the resource and prepare `GifDecoderConfig`

Choose a resource in the form understood by `port/gif_porting.c`. It can be a pathname, a memory descriptor, a network-stream descriptor, or any other application-defined object; the decoder never interprets it. Store that object in `source_identifier` and retain it until the decoder is closed.

```c
const void *resource = selected_resource;
GifDecoderConfig config = {
    .source_identifier = resource
};
```

At this point there is no decoder and no GIF metadata. The configuration merely tells the port what byte source `gif_decoder_open()` must open.

### 3.2 Open the GIF and receive its stream information

Open before allocating a canvas-sized framebuffer because the logical GIF dimensions are not known yet. A successful call creates the decoder and returns the stream canvas information needed by the application.

```c
GifDecoder *decoder = NULL;
GifStreamInfo stream;
GifStatus status;

status = gif_decoder_open(&config, &decoder, &stream);
if (status != GIF_STATUS_OK) {
    return status;
}
```

After success, `decoder` owns an open byte source and `stream.canvas_width` and `stream.canvas_height` describe the complete composited canvas. The next task is to decide whether the application can provide output storage for that canvas. From this point onward, every exit path must call `gif_decoder_close(decoder)`.

### 3.3 Prepare framebuffer storage from `GifStreamInfo`

The decoder draws the full GIF canvas into application-owned memory. Choose a pixel format, compute a stride of at least `canvas_width * 3`, and ensure the supplied storage is at least `(canvas_height - 1) * stride_bytes + canvas_width * 3`. Check each multiplication and addition against `SIZE_MAX` before using the result. The following uses a tightly packed pre-existing application framebuffer; the allocation mechanism is deliberately outside this guide.

```c
size_t row_bytes = (size_t)stream.canvas_width * 3U;
size_t required_bytes = row_bytes * (size_t)stream.canvas_height;

if (stream.canvas_width == 0U ||
    row_bytes / 3U != (size_t)stream.canvas_width ||
    (stream.canvas_height != 0U &&
     required_bytes / (size_t)stream.canvas_height != row_bytes) ||
    framebuffer_capacity_bytes < required_bytes) {
    gif_decoder_close(decoder);
    return GIF_STATUS_BUFFER_TOO_SMALL;
}
```

`framebuffer_pixels` and `framebuffer_capacity_bytes` are application-owned values. The decoder pool is separate from this output storage. A product may choose a wider stride for alignment or display-controller requirements; in that case, use the wider value for both the capacity check and the surface in the next step.

### 3.4 Construct and bind `GifOutputSurface`

Binding gives the decoder a writable destination before the first frame is decoded. Bind once the framebuffer has passed the size check; the application must not release, relocate, or reuse its pixels for unrelated work while the decoder remains bound.

```c
GifOutputSurface surface = {
    .pixels = framebuffer_pixels,
    .capacity_bytes = framebuffer_capacity_bytes,
    .stride_bytes = row_bytes,
    .pixel_format = GIF_PIXEL_RGB888
};

status = gif_decoder_bind_output(decoder, &surface);
if (status != GIF_STATUS_OK) {
    gif_decoder_close(decoder);
    return status;
}
```

After a successful bind, `surface` may go out of scope because the decoder copied its description, but the pixel storage it describes must remain valid. The decoder is now ready to produce the first completed canvas.

### 3.5 Decode one frame at a time

Call `gif_decoder_next_frame()` repeatedly. Each successful call advances the GIF stream and updates the bound framebuffer to the composited canvas for one displayable frame.

```c
GifFrameInfo frame;

status = gif_decoder_next_frame(decoder, &frame);
if (status == GIF_STATUS_OK) {
    /* The complete framebuffer is ready for presentation. */
}
```

The application must not present partially decoded output. Only `GIF_STATUS_OK` means the current canvas is complete. `frame.frame_index` identifies the completed frame, while the remaining `GifFrameInfo` fields describe source-frame and changed-canvas regions for applications that can use them.

### 3.6 Present the completed frame and apply its delay

Presentation and scheduling stay outside the decoder so that the same library fits polling loops, superloops, RTOS tasks, and bare-metal display paths. After every successful decode, pass the complete surface to the display path and then wait or schedule according to `frame.delay_ms`.

```c
display_completed_gif_frame(&surface, &stream, &frame);
wait_for_gif_delay_ms(frame.delay_ms);
```

`display_completed_gif_frame()` and `wait_for_gif_delay_ms()` are concise placeholders for application services, not library functions. Call the next decode only when the product's playback policy permits it.

### 3.7 Distinguish end of stream from an error

At the end of an ordinary GIF, `gif_decoder_next_frame()` returns `GIF_STATUS_END_OF_STREAM`. This is not a malformed-file error. Any other non-OK status ends the active lifecycle with a failure; preserve or report it before cleanup.

```c
if (status == GIF_STATUS_END_OF_STREAM) {
    /* Playback completed normally. */
} else if (status != GIF_STATUS_OK) {
    /* Playback failed; status identifies the reason. */
}
```

Do not continue decoding after either end of stream or a decoding error. To replay a resource, begin a new lifecycle with a new call to `gif_decoder_open()`.

### 3.8 Close on every completed or failed lifecycle

Close releases decoder-owned state and asks the port to close its byte source. It is safe to call after a partial setup, after an error, or after end of stream.

```c
gif_decoder_close(decoder);
decoder = NULL;
```

After close, the source identifier and framebuffer may be released or reused by the application. The decoder handle is no longer valid.

### 3.9 Complete lifecycle example

The following concise example combines the preceding steps. `framebuffer_pixels` and its capacity are supplied by the application, and only presentation and delay are platform-specific placeholders.

```c
#include <gif_decoder.h>

extern void display_completed_gif_frame(const GifOutputSurface *surface,
                                        const GifStreamInfo *stream,
                                        const GifFrameInfo *frame);
extern void wait_for_gif_delay_ms(uint32_t delay_ms);

GifStatus play_gif(const void *resource,
                   void *framebuffer_pixels,
                   size_t framebuffer_capacity_bytes) {
    GifDecoderConfig config = { .source_identifier = resource };
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifOutputSurface surface;
    GifFrameInfo frame;
    GifStatus status;
    size_t row_bytes;
    size_t required_bytes;

    if (resource == NULL || framebuffer_pixels == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }

    status = gif_decoder_open(&config, &decoder, &stream);
    if (status != GIF_STATUS_OK) {
        return status;
    }

    row_bytes = (size_t)stream.canvas_width * 3U;
    required_bytes = row_bytes * (size_t)stream.canvas_height;
    if (stream.canvas_width == 0U ||
        row_bytes / 3U != (size_t)stream.canvas_width ||
        (stream.canvas_height != 0U &&
         required_bytes / (size_t)stream.canvas_height != row_bytes) ||
        framebuffer_capacity_bytes < required_bytes) {
        status = GIF_STATUS_BUFFER_TOO_SMALL;
        goto cleanup;
    }

    surface.pixels = framebuffer_pixels;
    surface.capacity_bytes = framebuffer_capacity_bytes;
    surface.stride_bytes = row_bytes;
    surface.pixel_format = GIF_PIXEL_RGB888;
    status = gif_decoder_bind_output(decoder, &surface);
    if (status != GIF_STATUS_OK) {
        goto cleanup;
    }

    for (;;) {
        status = gif_decoder_next_frame(decoder, &frame);
        if (status == GIF_STATUS_OK) {
            display_completed_gif_frame(&surface, &stream, &frame);
            wait_for_gif_delay_ms(frame.delay_ms);
            continue;
        }

        break;
    }

    if (status == GIF_STATUS_END_OF_STREAM) {
        status = GIF_STATUS_OK;
    }

cleanup:
    gif_decoder_close(decoder);
    return status;
}
```

The porting layer is used only indirectly: `gif_decoder_open()` opens the source, `gif_decoder_next_frame()` consumes its sequential bytes as needed, and `gif_decoder_close()` releases it. Application code never calls the port functions directly.
