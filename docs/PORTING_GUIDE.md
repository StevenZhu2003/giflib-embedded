# Porting guide

This guide walks through one complete port of `giflib-embedded`. It assumes
that you are comfortable with embedded C and basic FatFs operations such as
`f_open()`, `f_read()`, and `f_close()`. No knowledge of giflib internals is
required.

The goal is simple: the application selects a GIF at run time, opens it through
the public decoder API, and decodes each frame into a caller-owned framebuffer.
Only `port/gif_porting.c` needs target-specific storage code.

## 1. Start with the finished application workflow

After the port is complete, an application can use the decoder like this:

```c
#include <gif_decoder.h>

#include <stddef.h>
#include <stdint.h>

#define FRAMEBUFFER_WIDTH  800U
#define FRAMEBUFFER_HEIGHT 480U
#define FRAMEBUFFER_STRIDE (FRAMEBUFFER_WIDTH * 3U)

static uint8_t framebuffer[FRAMEBUFFER_HEIGHT * FRAMEBUFFER_STRIDE];

/* Application display code; this is not part of giflib-embedded. */
extern void display_framebuffer(const void *pixels,
                                uint32_t width,
                                uint32_t height,
                                size_t stride_bytes);

static GifStatus play_gif(const char *path) {
    GifDecoderConfig config = {
        .source_identifier = path,
    };
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    GifStatus status;

    status = gif_decoder_open(&config, &decoder, &stream);
    if (status != GIF_STATUS_OK) {
        return status;
    }

    GifOutputSurface surface = {
        .pixels = framebuffer,
        .capacity_bytes = sizeof(framebuffer),
        .stride_bytes = FRAMEBUFFER_STRIDE,
        .pixel_format = GIF_PIXEL_RGB888,
    };

    status = gif_decoder_bind_output(decoder, &surface);
    while (status == GIF_STATUS_OK) {
        status = gif_decoder_next_frame(decoder, &frame);
        if (status == GIF_STATUS_OK) {
            display_framebuffer(surface.pixels,
                                stream.canvas_width,
                                stream.canvas_height,
                                surface.stride_bytes);
        }
    }

    if (status == GIF_STATUS_END_OF_STREAM) {
        status = GIF_STATUS_OK;
    }

    gif_decoder_close(decoder);
    return status;
}

void show_fixed_animation(void) {
    (void)play_gif("0:/images/demo.gif");
}
```

The framebuffer, display operation, and any playback timing are application
responsibilities. The current public `GifFrameInfo` does not expose a frame
delay, so this example does not invent or call one. The decoder itself never
sleeps and never controls a display.

The surface capacity and stride must be large enough for the dimensions
returned in `GifStreamInfo`. A product can use a fixed maximum-size buffer as
above, a pool, or another application-owned allocation policy.

## 2. Why the decoder needs a porting layer

The application example contains this line:

```c
.source_identifier = "0:/images/demo.gif"
```

How does a platform-independent decoder know that this value should be passed
to FatFs `f_open()`? It does not. That translation is the purpose of:

```text
port/gif_porting.c
```

The complete input path is:

```text
Application
    |
    | source_identifier
    v
Public decoder API
    |
    v
port/gif_porting.c
    |
    | open / read / close
    v
FatFs / memory / flash / another byte source
```

The application decides **what to open**. The port decides **how that resource
is opened and read on this target**. The hidden decoder and vendored giflib code
only consume sequential bytes.

For a normal target port:

- edit `port/gif_porting.c`;
- leave `port/gif_porting.h` unchanged;
- do not add storage glue to the application entry point, public decoder
  facade, hidden decoder core, or vendored giflib sources.

Filesystem mounting, storage-device setup, drivers, and hardware
initialization must already have been completed by the target application or
platform. They are below the `giflib-embedded` porting boundary.

## 3. Select a resource at run time

For the FatFs port built in this guide, `source_identifier` is a pointer to a
null-terminated pathname. A fixed choice is straightforward:

```c
GifDecoderConfig config = {
    .source_identifier = "0:/images/demo.gif",
};
```

It is not a compile-time library setting. It means:

> This decoder instance should open this resource on this call.

The application can obtain that choice from a serial command interface, a
command line, a menu, a graphical interface, or a resource table:

```c
char filename[128];

if (uart_receive_line(filename, sizeof(filename))) {
    GifDecoderConfig config = {
        .source_identifier = filename,
    };
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    if (gif_decoder_open(&config, &decoder, &stream) == GIF_STATUS_OK) {
        /* Bind output, decode frames, and display them. */
        gif_decoder_close(decoder);
    }
}
```

`uart_receive_line()` represents application input code, not a decoder or
porting API. The application still does not call FatFs. The same completed port
accepts any valid pathname passed this way.

Only after seeing this use is the formal definition useful:
`source_identifier` is opaque to the decoder. `gif_porting_open()` defines its
meaning. A FatFs port can interpret it as `const char *`; a memory port can use
a memory-source descriptor; a flash port can use an asset descriptor; another
port can use a logical resource key. This is why the public API does not expose
`FIL` or another storage-specific type.

Keep the referenced identifier valid and unchanged until
`gif_decoder_close()` unless the port explicitly documents that it consumes or
copies the identifier during `gif_porting_open()`. The FatFs implementation
below consumes the pathname synchronously in `f_open()` and does not retain it,
but keeping the buffer valid through close remains a simple portable rule.

## 4. Build a FatFs port step by step

The repository supplies a compile-safe stub in `port/gif_porting.c`. Replace
the three function bodies in that file and add the FatFs include there. No
other decoder file needs to change.

### 4.1 Open the selected pathname

When the application calls:

```c
gif_decoder_open(&config, &decoder, &stream);
```

the decoder calls `gif_porting_open()` before parsing the GIF header. For the
FatFs port, the data moves through these forms:

```text
source_identifier
    |
    | "0:/images/demo.gif"
    v
gif_porting_open()
    |
    | f_open()
    v
FIL object
    |
    v
GifPortingHandle
```

Start the implementation with one static `FIL`. This deliberately supports one
active decoder at a time:

```c
#include "gif_porting.h"

#include "ff.h"

static FIL gif_port_file;
static int gif_port_file_is_open;
```

Then implement open:

```c
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
```

The implementation first clears the output handle so every failed open leaves
it `NULL`. It rejects a second active decoder because both instances would
otherwise overwrite the same `FIL`. Only a completely successful `f_open()`
publishes the handle.

### 4.2 Why `GifPortingHandle` exists

The pathname answers “which resource?”, but it does not hold the current file
position or other state of an already open stream. After `f_open()`, the `FIL`
object holds that state. The port returns `&gif_port_file` as a
`GifPortingHandle`, and later reads receive the same value.

The decoder stores the handle but never interprets it. In this port it is a
`FIL *`; in another port it could point to memory stream state, a flash-reader
descriptor, a fixed-pool slot, or a port-allocated object. That is why the
contract declares it as an opaque `void *`.

The static object keeps this first implementation small. If the product needs
multiple decoders open concurrently, replace it with independent per-stream
state, such as a fixed-size port-owned pool. That policy remains entirely in
`gif_porting.c`.

### 4.3 Read bytes while frames are decoded

The application never calls `gif_porting_read()` directly. After the port is
opened, `gif_decoder_open()` uses it to read the GIF header and logical-screen
data. Later, `gif_decoder_next_frame()` uses it whenever a frame needs more
input bytes:

```text
gif_decoder_open()              gif_decoder_next_frame()
        |                                  |
        | read header                      | read frame data
        +----------------+-----------------+
                         |
                         v
                 gif_porting_read()
                         |
                         v
                      f_read()
```

Add `<limits.h>` and implement the sequential read:

```c
#include <limits.h>

GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    UINT request;
    UINT bytes_read = 0;
    FRESULT result;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (handle == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

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
```

`requested_bytes` is the maximum amount the decoder currently wants.
`actual_bytes` is the exact amount the port placed in `destination`. The
`UINT_MAX` limit prevents a `size_t` request from being narrowed silently when
the storage API uses a smaller request type. If capped, a successful read lets
the decoder request the remainder later.

For an ordinary regular file, FatFs completes the request unless it reaches
the end of the file. Therefore `FR_OK` together with
`bytes_read < request` means that the reported bytes are the final bytes and
maps to `GIF_PORTING_EOF`. It is not an I/O failure. A different byte source
whose short reads do not mean EOF may return `GIF_PORTING_OK` with a positive
short count; the decoder will ask for the remainder.

The decoder only needs forward, sequential reads. The port does not implement
seek, rewind, tell, file size, directory operations, or GIF parsing.

### 4.4 Close the file on every exit path

When the application calls:

```c
gif_decoder_close(decoder);
```

the decoder eventually calls the port close operation:

```text
gif_decoder_close()
        |
        v
gif_porting_close()
        |
        v
     f_close()
```

Implement it as follows:

```c
void gif_porting_close(GifPortingHandle handle) {
    if (handle == NULL) {
        return;
    }

    (void)f_close((FIL *)handle);
    gif_port_file_is_open = 0;
}
```

The public decoder owns the handle after a successful open. If `f_open()`
succeeds but the GIF header is malformed, or later decoder initialization
fails, the decoder still calls `gif_porting_close()` exactly once. The
application calls only `gif_decoder_close()`; it must not close the `FIL`
directly.

## 5. Complete FatFs reference implementation

At this point the three pieces can be checked against one complete
`port/gif_porting.c`. This version supports one active decoder:

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

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (handle == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

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

The parent build must make `ff.h` and the FatFs implementation available to
the library target. Keep machine-specific absolute paths out of this
repository. In a CMake parent project, one possible relationship is:

```cmake
add_subdirectory(path/to/giflib-embedded)

target_link_libraries(giflib_embedded PRIVATE platform_storage)
target_link_libraries(application PRIVATE
    giflib_embedded::giflib_embedded
)
```

Here `platform_storage` is supplied by the parent project. This build
composition does not create another decoder porting location: storage calls
and their error mapping still live only in `gif_porting.c`.

## 6. Open GIF A and GIF B without changing the port

A pathname is selected per decoder instance, so the same built library can
open different files at run time:

```c
char filename[128];

for (;;) {
    if (!uart_receive_line(filename, sizeof(filename))) {
        continue;
    }

    (void)play_gif(filename);
}
```

If the first input is `0:/images/a.gif`, `play_gif()` opens, decodes, displays,
and closes A. The function then returns, the same buffer may receive
`0:/images/b.gif`, and the unchanged `play_gif()` opens B. This requires:

- no recompilation;
- no change to `gif_porting.c`;
- no FatFs calls in the application entry point;
- no change to the public or hidden decoder code.

The `filename` buffer remains valid and unchanged for the complete
open/decode/close call, satisfying the conservative source-identifier lifetime
rule. It can be reused after `gif_decoder_close()`.

## 7. A short memory-backed port

The abstraction is a byte-source adapter, not a filesystem wrapper. For a GIF
already stored in addressable memory, the application can pass a descriptor:

```c
typedef struct MemoryGifSource {
    const uint8_t *data;
    size_t size;
} MemoryGifSource;

static const MemoryGifSource splash_gif = {
    .data = splash_gif_bytes,
    .size = sizeof(splash_gif_bytes),
};

GifDecoderConfig config = {
    .source_identifier = &splash_gif,
};
```

The target integration can define the shared descriptor in an
application-owned header and implement the same three operations in
`gif_porting.c`. This compact example again supports one active decoder:

```c
#include "gif_porting.h"
#include "memory_gif_source.h"

#include <string.h>

typedef struct MemoryGifStream {
    const uint8_t *next;
    size_t remaining;
    int active;
} MemoryGifStream;

static MemoryGifStream gif_memory_stream;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const MemoryGifSource *source =
        (const MemoryGifSource *)source_identifier;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (source == NULL || source->data == NULL || gif_memory_stream.active) {
        return GIF_PORTING_IO_ERROR;
    }

    gif_memory_stream.next = source->data;
    gif_memory_stream.remaining = source->size;
    gif_memory_stream.active = 1;
    *out_handle = &gif_memory_stream;
    return GIF_PORTING_OK;
}

GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    MemoryGifStream *stream = (MemoryGifStream *)handle;
    size_t count;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (stream == NULL || destination == NULL || requested_bytes == 0 ||
        !stream->active) {
        return GIF_PORTING_IO_ERROR;
    }

    count = requested_bytes < stream->remaining
                ? requested_bytes
                : stream->remaining;
    if (count != 0) {
        memcpy(destination, stream->next, count);
        stream->next += count;
        stream->remaining -= count;
        *actual_bytes = count;
    }

    return stream->remaining == 0 ? GIF_PORTING_EOF : GIF_PORTING_OK;
}

void gif_porting_close(GifPortingHandle handle) {
    MemoryGifStream *stream = (MemoryGifStream *)handle;

    if (stream != NULL) {
        stream->next = NULL;
        stream->remaining = 0;
        stream->active = 0;
    }
}
```

Here open initializes sequential stream state, read copies the next bytes, and
close resets the port-owned state. The decoder workflow above is unchanged.

## 8. Porting contract reference

The tutorial explains why each concept exists. This section collects the
formal rules used by every byte-source implementation.

### 8.1 File and component ownership

- `include/gif_decoder.h` is the fixed application API. Do not add storage
  types or calls to it.
- `src/gif_decoder.c` is the fixed public implementation. Do not add target
  initialization or storage calls to it.
- `src/gif_decoder_core.c` and `src/gif_decoder_core.h` are hidden decoder
  implementation. Applications and ports do not include or call them.
- `port/gif_porting.h` is the fixed, platform-neutral port contract and
  normally requires no changes.
- `port/gif_porting.c` is the target implementation. Target storage includes,
  private handle state, open/read/close calls, and error mapping belong here.
- `vendor/giflib/` contains upstream-derived parser and LZW code. A platform
  port does not modify it.

The port supplies input bytes only. The caller owns the output framebuffer.
Display, cache policy, playback timing, user input, storage initialization, and
hardware control remain outside the porting layer. Memory allocation is also
outside this three-function contract; current runtime requirements are listed
in the README.

### 8.2 Source identifier rules

- Its meaning is defined by `gif_porting_open()`.
- The decoder neither inspects nor copies it.
- It selects one resource for one decoder open operation.
- The referenced object should remain valid through `gif_decoder_close()` for
  portable application code.
- A port may document a shorter lifetime only when open copies or consumes all
  required information and never retains the original pointer.

### 8.3 Handle, ownership, and concurrency rules

- A successful open returns a non-`NULL` handle.
- A failed open leaves the output handle `NULL` and releases partial resources.
- `GIF_PORTING_EOF` is a read result and is never returned from open.
- After successful open, the decoder owns the handle and pairs it with exactly
  one close.
- The application does not inspect, reuse, or close the porting handle.
- A single static source object is valid only for one active decoder and must
  reject another open while busy.
- Concurrent decoders require independent source position and mutable state,
  for example separate objects or slots in a port-owned pool.
- Close releases only resources owned by the port and accepts `NULL` safely.

### 8.4 Read rules

The read signature is:

```c
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes);
```

Set `*actual_bytes` to zero before attempting the source operation. Then report
the exact number of valid bytes written, never more than `requested_bytes`.

| Status | `actual_bytes` | Meaning |
| --- | ---: | --- |
| `GIF_PORTING_OK` | `1..requested_bytes` | Progress was made and more data may follow. |
| `GIF_PORTING_EOF` | `0..requested_bytes` | These are the final bytes; no future byte exists. |
| `GIF_PORTING_IO_ERROR` | `0..requested_bytes` | The source failed; any reported bytes are still valid. |

Important consequences:

- `GIF_PORTING_OK` with zero bytes is invalid because it makes no progress.
  The decoder treats it as an I/O error rather than looping forever.
- A positive short `GIF_PORTING_OK` read is legal. The decoder asks again for
  the remainder.
- EOF may accompany final valid bytes. They are consumed before the source is
  treated as terminal.
- I/O error may also accompany valid bytes. They are counted, but the error
  remains terminal and is reported to the application.
- Do not map every short read to failure. Use the underlying source's meaning:
  temporary short progress, end of input, or actual failure.
- Only forward reads are required. Seek, rewind, tell, and file size are not
  part of the contract.

### 8.5 Lifecycle and error mapping

The normal lifecycle is:

```text
gif_decoder_open()
    -> gif_porting_open()
    -> zero or more gif_porting_read() calls
    -> parse the GIF logical screen

gif_decoder_next_frame()
    -> zero or more gif_porting_read() calls

gif_decoder_close()
    -> gif_porting_close()
```

If port open fails, the public result is `GIF_STATUS_IO_ERROR`. Unexpected EOF
while parsing a required GIF structure becomes `GIF_STATUS_UNEXPECTED_EOF`.
An actual source failure becomes `GIF_STATUS_IO_ERROR`.

Every successful port open is closed exactly once, including malformed GIFs,
allocation failures, unsupported input discovered during initialization, and
normal end of stream. A port-open failure that never publishes a handle is not
followed by close; the open implementation must clean up its own partial work.

## 9. Verify the completed port

First perform a simple application walkthrough:

1. Initialize the underlying filesystem, storage device, and drivers outside
   the decoder.
2. Pass a valid pathname to `GifDecoderConfig.source_identifier`.
3. Open the decoder and check the returned canvas dimensions.
4. Bind a sufficiently large caller-owned framebuffer.
5. Call `gif_decoder_next_frame()` until `GIF_STATUS_END_OF_STREAM`.
6. Display each completed framebuffer in application code.
7. Close the decoder.
8. Change the runtime pathname and repeat without rebuilding or changing the
   port.

Then exercise the boundary cases:

1. A valid single-frame GIF opens, decodes, reaches end of stream, and closes.
2. A valid multi-frame GIF performs repeated sequential reads correctly.
3. A truncated GIF reports unexpected EOF rather than hanging.
4. A forced storage failure reports `GIF_STATUS_IO_ERROR`.
5. A malformed GIF closes a successfully opened source exactly once.
6. Repeated open/decode/close cycles do not leak handles or pool slots.
7. If concurrency is supported, two decoders do not share mutable source state.
8. The port never reports `actual_bytes > requested_bytes`.
9. The port never returns `GIF_PORTING_OK` with zero bytes.
10. Target storage symbols appear only in `port/gif_porting.c` and the target's
    own storage stack.

The repository host tests use a separate memory-backed test port to verify the
same open/read/close contract, including short reads, EOF, injected errors, and
close ownership.

## 10. Diagnose common failures

### Image descriptor reads fail after the animation runs for a while

Check the port before changing giflib:

- confirm the source handle remains valid until decoder close;
- confirm the source position advances by exactly `actual_bytes`;
- confirm a short read is not reported as a successful full read;
- confirm EOF is returned only when no future byte exists;
- confirm `actual_bytes` is cleared on every call;
- confirm another subsystem does not reuse the storage buffer or handle.

### The decoder hangs in the read bridge

The usual cause is `GIF_PORTING_OK` with `actual_bytes == 0`. Return EOF when
the source has ended or I/O error when it cannot make progress.

### One file works but opening another active decoder fails

A port with one static source object supports only one active decoder. Finish
and close the first decoder before opening the second, or implement independent
handle state for concurrency.

### Public code starts including storage headers

Move those includes and calls back into `port/gif_porting.c`. The public config
carries only the resource identifier. The application may choose a pathname,
but it does not open or read the file for the decoder.

## 11. Port acceptance checklist

A port is complete when all statements below are true:

- the application can select GIF A, close it, then select GIF B at run time;
- only `port/gif_porting.c` contains target storage integration;
- `port/gif_porting.h` remains unchanged and platform-neutral;
- public and hidden decoder files remain unchanged;
- open returns a non-`NULL` handle only after complete success;
- read obeys the byte-count and terminal-status rules;
- close is `NULL`-safe and releases each successful open exactly once;
- concurrent decoder behavior is explicitly rejected or correctly supported;
- no seek or file-size operation is required;
- the application owns the framebuffer, display, and playback timing;
- filesystem and storage initialization remain outside the decoder;
- host tests, target compilation, and target runtime checks pass.

With these checks complete, the application needs only `gif_decoder.h` to use
the decoder. It does not need hidden decoder headers, giflib internals,
`gif_porting.h`, or a storage header in order to open, decode, display, and
close a selected GIF.
