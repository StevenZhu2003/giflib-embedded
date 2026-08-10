# Porting guide

This guide explains how to connect `giflib-embedded` to a storage source on a
new target. The library intentionally has one porting implementation file:

```text
port/gif_porting.c
```

For a normal target port, this is the only library file that should be edited.
Do not add storage glue to the application entry point, public decoder facade,
hidden decoder core, or vendored giflib sources.

## 1. Understand the three responsibilities

The application, port, and decoder have deliberately separate jobs:

```text
application
    selects a resource identifier
    owns the output framebuffer
    displays completed frames
    applies frame delays
              |
              v
fixed public decoder API
              |
              v
port/gif_porting.c
    opens one byte source
    reads sequential bytes
    closes the source
              |
              v
hidden decoder core + vendored giflib
    parses GIF records and LZW data
    converts palette indices to pixels
    composites frames into the caller's framebuffer
```

The porting layer is not a display driver, timer abstraction, filesystem, media
player, or general hardware abstraction. It supplies bytes and nothing else.

Memory allocation is also outside this porting contract. The current decoder
continues to use the C runtime allocation functions documented in the README.

## 2. Know which files may be changed

- `include/gif_decoder.h` is the fixed application API. Do not add target types
  or storage functions to it.
- `src/gif_decoder.c` is the fixed public implementation. Do not add target
  initialization or file calls to it.
- `src/gif_decoder_core.c` and `src/gif_decoder_core.h` are hidden decoder
  implementation. Applications and ports must not include or call them.
- `port/gif_porting.h` is the fixed porting contract. It is intentionally free
  of filesystem and device types and normally requires no changes.
- `port/gif_porting.c` is the target implementation. Add target storage
  includes, private handle types, open/read/close calls, and error mapping here.
- `vendor/giflib/` contains upstream-derived code. A platform port must not
  modify it.

## 3. Understand the source identifier

The application selects a source through:

```c
GifDecoderConfig config = {
    .source_identifier = application_value,
};
```

`source_identifier` is deliberately opaque. `gif_porting_open()` defines its
meaning. Common choices are:

- a null-terminated path string for a filesystem port;
- a pointer to an application-owned memory-source descriptor;
- a pointer to a flash asset descriptor;
- a logical resource key interpreted by the target.

The decoder does not inspect or copy the identifier. For the safest lifetime
rule, keep the referenced object valid until `gif_decoder_close()`. A port may
document a shorter lifetime only when `gif_porting_open()` copies everything it
needs and never retains the original pointer.

Choosing a resource is ordinary application behavior. Knowing how that
resource is opened and read is porting behavior and must remain in
`gif_porting.c`.

## 4. Understand the porting handle

`GifPortingHandle` is an opaque `void *` token returned by
`gif_porting_open()`. The decoder stores it but never examines it.

The handle can point to:

- one static object when the product permits only one active decoder;
- a slot in a fixed-size port-owned pool;
- an object allocated by the port;
- an existing application-owned source object, if the port clearly defines
  its ownership and lifetime.

A successful open must return a non-NULL handle. Every successful open is
paired with exactly one close, including cases where the GIF header is invalid
or later decoder allocation fails.

If concurrent decoders are required, each successful open must return an
independent handle. A single global file object is valid only when the port
explicitly supports one active decoder.

## 5. Implement `gif_porting_open()`

The open operation has this contract:

```c
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle);
```

Implement it in this order:

1. Validate `out_handle`.
2. Set `*out_handle` to `NULL` before doing any work.
3. Validate and interpret `source_identifier`.
4. Acquire a private handle or a free handle slot.
5. Open or initialize the selected byte source for sequential reading.
6. On success, store a non-NULL handle and return `GIF_PORTING_OK`.
7. On any failure, release partially acquired resources and return
   `GIF_PORTING_IO_ERROR` with `*out_handle` still `NULL`.

`GIF_PORTING_EOF` is a read result and must not be returned from open.

## 6. Implement `gif_porting_read()` correctly

The read operation has this contract:

```c
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes);
```

The decoder requires only forward, sequential reads. It never requires seek,
rewind, tell, file size, directory information, or storage metadata.

Start every call by setting `*actual_bytes` to zero. After the storage operation
finishes, report the exact number of valid bytes placed in `destination`.
Never report more than `requested_bytes`.

Use the result statuses as follows:

| Status | `actual_bytes` | Meaning |
| --- | ---: | --- |
| `GIF_PORTING_OK` | `1..requested_bytes` | Progress was made and more data may follow. |
| `GIF_PORTING_EOF` | `0..requested_bytes` | These are the final bytes; no future byte exists. |
| `GIF_PORTING_IO_ERROR` | `0..requested_bytes` | The source failed; any reported bytes are still valid. |

Important rules:

- `GIF_PORTING_OK` with zero bytes is invalid because it cannot make progress.
  The decoder deliberately treats it as an I/O error instead of looping
  forever.
- A short `GIF_PORTING_OK` read is legal. The hidden bridge calls the port again
  until giflib's request is satisfied.
- EOF may be returned together with final bytes. The decoder consumes those
  bytes before treating the source as terminal.
- An I/O error may also accompany final valid bytes. The bytes are counted, but
  the error remains terminal and is reported to the application.
- Do not translate every short read into an error. Determine whether the
  underlying source means “temporarily fewer bytes,” “end of input,” or
  “failure.”

If the storage API uses a request-length type smaller than `size_t`, cap each
individual request to the largest representable value. Returning a successful
short read lets the decoder request the remainder safely.

## 7. Implement `gif_porting_close()`

The close operation has this contract:

```c
void gif_porting_close(GifPortingHandle handle);
```

It must:

1. accept `NULL` without failing;
2. close or release the underlying source;
3. release only resources owned by the port;
4. return a pool slot to the available state when a pool is used;
5. avoid accessing decoder or framebuffer state.

The public decoder owns a successfully opened handle after open completes. The
application must call `gif_decoder_close()` rather than calling the port close
operation directly.

## 8. Complete FatFs example

The following example treats `source_identifier` as a path string and supports
one active decoder. All FatFs types and calls remain inside
`port/gif_porting.c`.

```c
#include "gif_porting.h"

#include "ff.h"

#include <limits.h>

static FIL gif_port_file;
static int gif_port_file_is_open;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const char *path = (const char *)source_identifier;
    FRESULT result;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (path == NULL || gif_port_file_is_open) {
        return GIF_PORTING_IO_ERROR;
    }

    result = f_open(&gif_port_file, path, FA_READ);
    if (result != FR_OK) {
        return GIF_PORTING_IO_ERROR;
    }

    gif_port_file_is_open = 1;
    *out_handle = &gif_port_file;
    return GIF_PORTING_OK;
}

GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    UINT request;
    UINT bytes_read = 0;
    FRESULT result;

    if (handle == NULL || destination == NULL || actual_bytes == NULL ||
        requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    request = requested_bytes > (size_t)UINT_MAX
                  ? (UINT)UINT_MAX
                  : (UINT)requested_bytes;
    result = f_read((FIL *)handle, destination, request, &bytes_read);
    *actual_bytes = (size_t)bytes_read;

    if (result != FR_OK) {
        return GIF_PORTING_IO_ERROR;
    }
    if (bytes_read < request) {
        return GIF_PORTING_EOF;
    }
    return GIF_PORTING_OK;
}

void gif_porting_close(GifPortingHandle handle) {
    if (handle == NULL) {
        return;
    }

    (void)f_close((FIL *)handle);
    gif_port_file_is_open = 0;
}
```

This example is intentionally simple. For concurrent decoders, replace the
single object with a fixed pool or another target-appropriate handle policy.
That policy still belongs entirely inside `gif_porting.c`.

Filesystem volume setup and low-level storage initialization belong to the
filesystem and storage stack below this library. The GIF decoder only needs an
already usable byte source through the three operations above.

## 9. Connect the target storage dependency

Adding a storage header in `port/gif_porting.c` also requires the parent build
to make that header and its implementation available. Keep machine-specific
absolute paths out of this repository. Prefer a target or toolchain definition
owned by the parent project.

For a CMake parent project, a typical relationship is:

```cmake
add_subdirectory(path/to/giflib-embedded)

target_link_libraries(giflib_embedded PRIVATE platform_storage)
target_link_libraries(application PRIVATE
    giflib_embedded::giflib_embedded
)
```

Here `platform_storage` is a target supplied by the parent project. Its public
include directory makes the storage header visible to `port/gif_porting.c`.
This is build composition, not an additional decoder porting location; storage
calls and error mapping still remain exclusively in `gif_porting.c`.

## 10. Using the port from an application

Once `gif_porting.c` is implemented, application code remains platform-neutral
with respect to the decoder:

```c
#include <gif_decoder.h>

GifDecoderConfig config = {
    .source_identifier = "media/animation.gif",
};
GifDecoder *decoder = NULL;
GifStreamInfo stream;
GifStatus status;

status = gif_decoder_open(&config, &decoder, &stream);
if (status == GIF_STATUS_OK) {
    /* Prepare a caller-owned framebuffer using stream dimensions. */
    /* Bind the framebuffer, decode frames, display them, then close. */
    gif_decoder_close(decoder);
}
```

The application does not include `gif_porting.h`, filesystem headers, or
decoder-internal headers to use the public API.

## 11. Verification procedure

After implementing the port, verify these cases on the target:

1. A valid single-frame GIF opens, decodes, reaches end of stream, and closes.
2. A valid multi-frame GIF performs repeated sequential reads correctly.
3. A truncated GIF reports unexpected EOF rather than hanging.
4. A forced storage failure reports `GIF_STATUS_IO_ERROR`.
5. A malformed GIF closes the successfully opened port handle exactly once.
6. Repeated open/decode/close cycles do not leak handles or pool slots.
7. If concurrency is supported, two decoders do not share mutable source state.
8. The port never reports `actual_bytes > requested_bytes`.
9. The port never returns `GIF_PORTING_OK` with zero bytes.
10. Platform storage symbols appear only in `port/gif_porting.c` and the
    platform's own storage stack.

The repository host tests use a separate memory-backed test port to validate
the same open/read/close contract, including short reads, EOF, injected errors,
and close ownership.

## 12. Common failure patterns

### Decoder reports an image descriptor read failure after running for a while

Check the port before changing giflib:

- confirm the source handle remains valid until decoder close;
- confirm the byte offset advances by exactly `actual_bytes`;
- confirm a short read is not incorrectly reported as a successful full read;
- confirm EOF is returned only when no future byte exists;
- confirm `actual_bytes` is cleared on every call;
- confirm the storage buffer and handle are not reused by another subsystem.

### Decoder hangs in the read bridge

The usual cause is returning `GIF_PORTING_OK` with `actual_bytes == 0`. Return
EOF when the source is finished or I/O error when it cannot make progress.

### Decoder opens one file but fails when another decoder is created

The port probably uses one global source object without rejecting or supporting
a second open. Either enforce one active decoder explicitly or implement an
independent handle pool.

### Public code starts including filesystem headers

Move those includes and calls back into `port/gif_porting.c`. The public config
should carry only the opaque source identifier.

## 13. Port acceptance checklist

A port is complete when all statements below are true:

- only `port/gif_porting.c` contains target storage integration;
- `port/gif_porting.h` remains unchanged and platform-neutral;
- public and hidden decoder files remain unchanged;
- open returns a non-NULL handle only after complete success;
- read obeys the byte-count and terminal-status rules;
- close is NULL-safe and releases each successful open exactly once;
- no seek or file-size operation is required;
- application code does not implement decoder storage glue;
- display and timing remain outside the decoder;
- host tests, target compilation, and target runtime checks pass.
