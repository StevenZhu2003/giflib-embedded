# Porting guide

This guide explains how to connect `giflib-embedded` to any source that can supply sequential bytes. It assumes familiarity with embedded C, but no knowledge of giflib internals.

The goal is to let an application select a GIF at run time, open it through the public decoder API, and decode each frame into a caller-owned framebuffer. With the default BUILTIN memory backend, a normal storage port changes only:

```text
port/gif_porting.c
```

When the optional PRIVATE memory backend is selected, the independent second porting point is `port/gif_mem_private.c`. It supplies allocator primitives; it is not a filesystem adapter and does not replace this guide's byte-source contract. The optional LIBC backend needs no porting file. The optional LVGL backend also needs no porting file, but the application must initialize LVGL before opening a decoder. The backend-specific lifecycle and sizing rules are centralized in [MEMORY_CONFIGURATION.md](MEMORY_CONFIGURATION.md).

The main tutorial uses a deliberately imaginary storage API. Its names do not belong to this library and no such header exists in the repository. They stand for whatever byte-source operations the target already provides. A complete FatFs implementation appears later as one real-world mapping of the same model.

## 1. Start with the finished application workflow

After the port is complete, application code uses only `gif_decoder.h`; it never calls `gif_porting_*()` directly. The full public API reference, step-by-step decoder lifecycle tutorial, and complete platform-neutral example are in [USER_GUIDE.md](USER_GUIDE.md). This guide deliberately concentrates on the one target-owned byte-source boundary that the application API uses indirectly.

The repository's [embedded GIF player example](../examples/embedded_player/README.md) is a complete reference application with a real animation resource, caller-owned framebuffer, display boundary, and application timing policy. It requires neither a filesystem nor a platform SDK.

## 2. Follow the porting boundary from the public API

The decoder receives this application-selected value:

```c
config.source_identifier = resource;
```

It deliberately does not know whether `resource` identifies a file, a memory object, a flash asset, or another byte source. It also does not know how the target opens or reads that source. `port/gif_porting.c` performs that translation:

```text
Application
    |
    | source_identifier: which resource to open
    v
Public decoder API
    |
    v
port/gif_porting.c
    |
    | open / sequential read / close
    v
Target byte-source operations
    |
    v
Memory / flash / filesystem / another source
```

The application decides **what to open**. The port decides **how to open and read it on this target**. The hidden decoder and vendored giflib code only consume the resulting byte stream.

For a normal port:

- edit `port/gif_porting.c`;
- leave `port/gif_porting.h` unchanged;
- do not add byte-source glue to the application entry point, public decoder facade, hidden decoder core, or vendored giflib sources.

Initialization below the byte-source interface remains the responsibility of the target application or platform. For example, a storage device, filesystem, memory mapping, or driver must already be ready before the decoder opens a resource.

## 3. Assume one simple target byte-source API

To explain the port without choosing a filesystem or hardware platform, assume that the target already offers these imaginary operations:

```c
typedef void *StorageHandle;

typedef enum StorageReadResult {
    STORAGE_READ_OK = 0,
    STORAGE_READ_EOF = 1,
    STORAGE_READ_ERROR = 2
} StorageReadResult;

StorageHandle storage_open(const void *resource);

StorageReadResult storage_read(StorageHandle handle,
                               void *buffer,
                               size_t requested,
                               size_t *actual);

void storage_close(StorageHandle handle);
```

These declarations are a teaching model, not a new dependency and not a proposed public API. Do not search the repository for `storage_open()` or copy these declarations into `gif_porting.h`. When implementing a real port, replace each imaginary call with the equivalent operation already available on the target.

For this model:

- `storage_open()` accepts an application-selected resource and returns a non-`NULL` open-source handle, or `NULL` on failure;
- `storage_read()` supplies up to the requested number of sequential bytes and reports the exact count;
- `storage_close()` releases the open source;
- `STORAGE_READ_EOF` means that any reported bytes are the final bytes;
- `STORAGE_READ_ERROR` means that the source failed.

The whole relationship is:

```text
application selects a resource
            |
            v
    gif_porting_open()
            |
            v
      storage_open()

decoder needs more bytes
            |
            v
    gif_porting_read()
            |
            v
      storage_read()

decoder releases the source
            |
            v
    gif_porting_close()
            |
            v
      storage_close()
```

## 4. Implement the three porting operations

The repository provides compile-safe stubs in `port/gif_porting.c`. This section replaces them using the imaginary target API above while following the real contract in `gif_porting.h`.

### 4.1 Open when the application opens a decoder

The first part of the public call is:

```text
application
    |
    | gif_decoder_open(config, ...)
    v
decoder
    |
    | gif_porting_open(config->source_identifier, ...)
    v
target
    |
    | storage_open(resource)
    v
open byte source
```

The standard port owns one dynamically allocated wrapper for each open source. Include the port-only bridge; it uses the selected GIF allocator but is not an application API:

```c
#include "gif_porting.h"
#include "gif_porting_memory.h"

typedef struct PortStorageHandle {
    StorageHandle storage;
} PortStorageHandle;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    PortStorageHandle *handle;

    if (out_handle == NULL || source_identifier == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    handle = gif_porting_mem_alloc(sizeof(*handle));
    if (handle == NULL) {
        return GIF_PORTING_OUT_OF_MEMORY;
    }
    handle->storage = storage_open(source_identifier);
    if (handle->storage == NULL) {
        gif_porting_mem_free(handle);
        return GIF_PORTING_IO_ERROR;
    }

    *out_handle = handle;
    return GIF_PORTING_OK;
}
```

Open first clears its output, allocates one wrapper, passes the opaque application resource to the target, and publishes a non-`NULL` porting handle only after complete success. The real port replaces `StorageHandle` and `storage_open()` with its target equivalents.

### 4.2 Understand why a handle exists

The source identifier answers “which resource should be opened?” It does not necessarily contain the state of an already open stream, such as its current read position.

```text
source_identifier
    |
    | selects a resource
    v
gif_porting_mem_alloc()
    |
    | owns one open-stream wrapper
    v
PortStorageHandle
    |
    | storage_open() initializes target state
    v
GifPortingHandle
```

The decoder stores `GifPortingHandle` but never interprets it. The port wrapper may contain a file object, a memory-stream cursor, a flash-reader state object, or another target handle. It is opaque so none of those target types leak into the public decoder API.

### 4.3 Keep ownership in the porting layer

`gif_porting_mem_alloc()` and `gif_porting_mem_free()` are the standard ownership mechanism for a port handle. They are available only to port-owned source files through `gif_porting_memory.h`; applications neither include that header nor call its functions. Close the target source before releasing its wrapper. A failed wrapper allocation returns `GIF_PORTING_OUT_OF_MEMORY`, which `gif_decoder_open()` maps to `GIF_STATUS_OUT_OF_MEMORY`; a target open failure remains `GIF_PORTING_IO_ERROR`. In BUILTIN mode, add every simultaneously live wrapper to the pool budget. A product can still deliberately use a fixed slot pool, but it must provide equivalent independent per-stream ownership and capacity rules.

### 4.4 Read whenever the decoder needs input

The application never calls `gif_porting_read()` directly. Reads can occur in both public operations:

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
                   storage_read()
```

Map the imaginary target results to the real porting statuses:

```c
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    PortStorageHandle *port_handle = (PortStorageHandle *)handle;
    StorageReadResult result;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (port_handle == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

    result = storage_read(port_handle->storage,
                          destination,
                          requested_bytes,
                          actual_bytes);

    if (*actual_bytes > requested_bytes) {
        *actual_bytes = 0;
        return GIF_PORTING_IO_ERROR;
    }

    switch (result) {
    case STORAGE_READ_OK:
        return *actual_bytes != 0 ? GIF_PORTING_OK
                                  : GIF_PORTING_IO_ERROR;
    case STORAGE_READ_EOF:
        return GIF_PORTING_EOF;
    case STORAGE_READ_ERROR:
    default:
        return GIF_PORTING_IO_ERROR;
    }
}
```

`requested_bytes` is the maximum amount currently requested by the decoder. `actual_bytes` is the exact amount placed in `destination`. A positive short `STORAGE_READ_OK` is allowed: the decoder asks again for the remainder. An EOF result may accompany final valid bytes. An error result may also report bytes that were transferred before the failure.

Successful zero-byte progress is not allowed. Without this rule the decoder could repeatedly request data while the source never advances. The adapter therefore maps `STORAGE_READ_OK` with zero bytes to `GIF_PORTING_IO_ERROR`.

The decoder only requires forward, sequential reads. The target abstraction does not need seek, rewind, tell, file-size, directory, or GIF-specific operations.

### 4.5 Close when the decoder releases the source

The normal close path is:

```text
application
    |
    | gif_decoder_close(decoder)
    v
decoder
    |
    | gif_porting_close(handle)
    v
target
    |
    | storage_close(target state), then gif_porting_mem_free(handle)
    v
source released
```

The adapter closes the target source and releases the port-owned wrapper:

```c
void gif_porting_close(GifPortingHandle opaque_handle) {
    PortStorageHandle *handle = (PortStorageHandle *)opaque_handle;

    if (handle != NULL) {
        storage_close(handle->storage);
        gif_porting_mem_free(handle);
    }
}
```

The public decoder owns the handle after successful port open. Even when the source opens successfully but the GIF header is malformed, allocation fails, or initialization stops for another reason, that handle is closed exactly once. The application calls `gif_decoder_close()` for a returned decoder; it does not call the porting or target close function directly.

## 5. Select different resources at run time

`source_identifier` is not a compile-time library configuration. It means:

> This decoder instance should open this application-selected resource now.

For example, an application can repeatedly select and decode resources:

```c
for (;;) {
    const void *resource = application_select_gif();

    if (resource != NULL) {
        (void)decode_and_display(resource);
    }
}
```

The same compiled decoder and the same completed `gif_porting.c` can open resource A, decode and close it, then open resource B. No porting calls need to be added to the application entry point.

The resource's concrete meaning is an agreement between the application and the port:

- a filesystem port may use a pointer to a pathname;
- a memory port may use a pointer to a memory-source descriptor;
- a flash port may use an asset descriptor;
- another port may use a logical resource key.

Formally, `source_identifier` is opaque to the decoder. The decoder passes it unchanged to `gif_porting_open()` and does not store it after open. For portable application code, keep the referenced object valid and unchanged until `gif_decoder_close()`. A particular port may document a shorter lifetime if its open operation consumes or copies everything synchronously and never retains the original pointer.

## 6. Real-world FatFs example

FatFs is one concrete implementation of the generic model, not a requirement of the decoder:

| Teaching model | FatFs implementation |
| --- | --- |
| application resource | null-terminated pathname |
| `storage_open()` | `f_open()` |
| `StorageHandle` | `FIL` owned by a `GifFatFsHandle` |
| `storage_read()` | `f_read()` |
| `storage_close()` | `f_close()` |

For this port only, the application can select a pathname:

```c
(void)decode_and_display("0:/images/demo.gif");
```

The complete `port/gif_porting.c` follows the standard dynamic-handle pattern. Each open decoder owns one `FIL` wrapper:

```c
#include "gif_porting.h"
#include "gif_porting_memory.h"

#include "ff.h"

#include <limits.h>

typedef struct GifFatFsHandle {
    FIL file;
} GifFatFsHandle;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const char *path = (const char *)source_identifier;
    GifFatFsHandle *handle;
    FRESULT result;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (path == NULL) {
        return GIF_PORTING_IO_ERROR;
    }

    handle = (GifFatFsHandle *)gif_porting_mem_alloc(sizeof(*handle));
    if (handle == NULL) {
        return GIF_PORTING_OUT_OF_MEMORY;
    }

    result = f_open(&handle->file, path, FA_READ);
    if (result != FR_OK) {
        gif_porting_mem_free(handle);
        return GIF_PORTING_IO_ERROR;
    }

    *out_handle = handle;
    return GIF_PORTING_OK;
}

GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    UINT request;
    UINT bytes_read = 0;
    FRESULT result;
    GifFatFsHandle *port_handle = (GifFatFsHandle *)handle;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (port_handle == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

    request = requested_bytes > (size_t)UINT_MAX
                  ? (UINT)UINT_MAX
                  : (UINT)requested_bytes;
    result = f_read(&port_handle->file, destination, request, &bytes_read);
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
    GifFatFsHandle *port_handle = (GifFatFsHandle *)handle;

    if (port_handle != NULL) {
        (void)f_close(&port_handle->file);
        gif_porting_mem_free(port_handle);
    }
}
```

For an ordinary regular file, a successful FatFs read fills the request unless it reaches the end of the file. Consequently, `FR_OK` together with `bytes_read < request` means that the returned bytes are the final bytes and maps to `GIF_PORTING_EOF`; it is not an I/O error. If the file ends exactly on a full request boundary, that read returns `GIF_PORTING_OK` and the next read reports EOF, which is also valid.

The `UINT_MAX` limit prevents a `size_t` request from being silently narrowed when the storage API uses a smaller request type. A capped successful read lets the decoder request the remainder later.

Each `GifFatFsHandle` owns one `FIL`, so independently opened decoders do not share file state. Size the selected GIF allocator for every simultaneously live wrapper. FatFs and the application remain responsible for any storage-layer concurrency requirements.

### Select FatFs path A, then path B

The same port can use pathnames obtained at run time:

```c
char filename[128];

for (;;) {
    if (!application_receive_resource_name(filename, sizeof(filename))) {
        continue;
    }

    (void)decode_and_display(filename);
}
```

The application selects the pathname but does not call `f_open()`, `f_read()`, or `f_close()`. If the first input names GIF A, the decoder opens, decodes, and closes A. The same buffer can then name GIF B. This requires no recompilation, no change to `gif_porting.c`, and no FatFs glue in the application entry point.

The filename buffer remains valid throughout `decode_and_display()`, including decoder close, and can be reused afterward. This satisfies the conservative identifier lifetime rule. This FatFs implementation actually consumes the pathname synchronously in `f_open()` and does not retain it.

### Connect FatFs to the parent build

The parent build must make `ff.h` and the FatFs implementation available to the library target. Keep machine-specific absolute paths out of this repository. In a CMake parent project, one possible relationship is:

```cmake
add_subdirectory(path/to/giflib-embedded)

target_link_libraries(giflib_embedded PRIVATE platform_storage)
target_link_libraries(application PRIVATE
    giflib_embedded::giflib_embedded
)
```

Here `platform_storage` is supplied by the parent project. This build composition does not create another decoder porting location: FatFs calls and their error mapping still live only in `gif_porting.c`.

Filesystem mounting, device setup, drivers, and hardware initialization must already have been completed below the decoder's porting boundary.

## 7. Memory-backed example

A memory source demonstrates that the abstraction is a byte-source adapter, not a filesystem adapter. The application can select this descriptor:

```c
typedef struct MemoryGifSource {
    const uint8_t *data;
    size_t size;
} MemoryGifSource;

static const MemoryGifSource splash_gif = {
    .data = splash_gif_bytes,
    .size = sizeof(splash_gif_bytes),
};

(void)decode_and_display(&splash_gif);
```

The target integration may place the descriptor declaration in an application-owned header shared with `gif_porting.c`. The port allocates one independent sequential cursor for each open decoder:

```c
#include "gif_porting.h"
#include "gif_porting_memory.h"
#include "memory_gif_source.h"

#include <string.h>

typedef struct MemoryGifStream {
    const uint8_t *next;
    size_t remaining;
} MemoryGifStream;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const MemoryGifSource *source =
        (const MemoryGifSource *)source_identifier;
    MemoryGifStream *stream;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (source == NULL || source->data == NULL) {
        return GIF_PORTING_IO_ERROR;
    }

    stream = (MemoryGifStream *)gif_porting_mem_alloc(sizeof(*stream));
    if (stream == NULL) {
        return GIF_PORTING_OUT_OF_MEMORY;
    }

    stream->next = source->data;
    stream->remaining = source->size;
    *out_handle = stream;
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

    if (stream == NULL || destination == NULL || requested_bytes == 0) {
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
        gif_porting_mem_free(stream);
    }
}
```

Here open allocates and initializes a cursor, read copies the next sequential bytes, and close releases the port-owned state. Separate cursors keep mutable source position independent. The public decoder workflow is identical to the generic and FatFs examples.

## 8. Porting contract reference

The tutorial first explains why the abstraction exists. This section collects the formal rules for implementing any byte source.

### 8.1 Repository file and component ownership

- `include/gif_decoder.h` is the fixed application API. Do not add target byte-source types or calls to it.
- `src/gif_decoder.c` is the fixed public implementation. Do not add target initialization or byte-source calls to it.
- `src/gif_decoder_core.c` and `src/gif_decoder_core.h` are hidden decoder implementation. Applications and ports do not include or call them.
- `port/gif_porting.h` is the fixed, platform-neutral port contract and normally requires no changes.
- `port/gif_porting_memory.h` is the standard port-only bridge for a dynamically allocated handle. It is not installed and applications do not include it.
- `port/gif_porting.c` is the target implementation. Target includes, private handle state, open/read/close calls, dynamic-handle ownership, and error mapping belong here.
- `vendor/giflib/` contains upstream-derived parser and LZW code. A platform port does not modify it.

The port supplies input bytes only. The caller owns the output framebuffer. Display, cache policy, playback timing, user input, source initialization, and hardware control remain outside the porting layer. The standard bridge is solely for an open-source handle; it must not be used for an application framebuffer or unrelated application storage.

### 8.2 Source identifier rules

- Its meaning is defined by `gif_porting_open()`.
- The decoder passes it through without inspecting or copying it.
- It selects one resource for one decoder open operation.
- The referenced object should remain valid through `gif_decoder_close()` for portable application code.
- A port may document a shorter lifetime only when open copies or consumes all required information and never retains the original pointer.

### 8.3 Open rules

- Validate `out_handle` and clear it before doing any work.
- Validate and interpret `source_identifier` only inside the port.
- Acquire or initialize all open-source state before publishing the handle.
- Return `GIF_PORTING_OK` with a non-`NULL` handle only after complete success.
- On failure, release partial resources and leave the handle `NULL`.
- Return `GIF_PORTING_OUT_OF_MEMORY` when `gif_porting_mem_alloc()` cannot create a required handle; return `GIF_PORTING_IO_ERROR` for other open failures.
- `GIF_PORTING_EOF` is a read result and is never returned from open.

### 8.4 Handle, ownership, and concurrency rules

- After successful open, the decoder owns the handle and pairs it with exactly one close.
- The application does not inspect, reuse, or close the porting handle.
- The standard port allocates one independent wrapper with `gif_porting_mem_alloc()` for every successful open and releases it exactly once through `gif_porting_close()`; it shares the selected GIF allocator domain.
- A product that deliberately uses fixed source slots must reject open when its slots are exhausted and provide independent source position and mutable state for every live decoder.
- Close releases only resources owned by the port and accepts `NULL` safely.

### 8.5 Read rules

The real read signature is:

```c
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes);
```

Set `*actual_bytes` to zero before attempting the source operation. Then report the exact number of valid bytes written, never more than `requested_bytes`.

| Status | `actual_bytes` | Meaning |
| --- | ---: | --- |
| `GIF_PORTING_OK` | `1..requested_bytes` | Progress was made and more data may follow. |
| `GIF_PORTING_EOF` | `0..requested_bytes` | These are the final bytes; no future byte exists. |
| `GIF_PORTING_IO_ERROR` | `0..requested_bytes` | The source failed; any reported bytes are still valid. |

Important consequences:

- `GIF_PORTING_OK` with zero bytes is invalid. The decoder treats it as an I/O error rather than looping forever.
- A positive short `GIF_PORTING_OK` read is legal. The decoder asks again for the remainder.
- EOF may accompany final valid bytes. They are consumed before the source is treated as terminal.
- I/O error may also accompany valid bytes. They are counted, but the error remains terminal and is reported to the application.
- Do not map every short read to failure. Use the underlying source's meaning: temporary short progress, end of input, or actual failure.
- Only forward reads are required. Seek, rewind, tell, and file size are not part of the contract.

### 8.6 Lifecycle and error mapping

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

If port open cannot allocate a required dynamic handle, the public result is `GIF_STATUS_OUT_OF_MEMORY`; another port-open failure becomes `GIF_STATUS_IO_ERROR`. Unexpected EOF while parsing a required GIF structure becomes `GIF_STATUS_UNEXPECTED_EOF`. An actual source failure becomes `GIF_STATUS_IO_ERROR`.

Every successful port open is closed exactly once, including malformed GIFs, allocation failures, unsupported input discovered during initialization, and normal end of stream. A port-open failure that never publishes a handle is not followed by close; the open implementation must clean up its own partial work.

## 9. Verify the completed port

First perform an application walkthrough:

1. Initialize the underlying byte-source service outside the decoder.
2. Obtain a valid resource identifier from the application.
3. Open the decoder and check the returned canvas dimensions.
4. Bind a sufficiently large caller-owned framebuffer.
5. Call `gif_decoder_next_frame()` until `GIF_STATUS_END_OF_STREAM`.
6. Display each completed framebuffer in application code.
7. Close the decoder.
8. Select another resource and repeat without rebuilding or changing the port.

Then exercise the boundary cases:

1. A valid single-frame GIF opens, decodes, reaches end of stream, and closes.
2. A valid multi-frame GIF performs repeated sequential reads correctly.
3. A truncated GIF reports unexpected EOF rather than hanging.
4. A forced source failure reports `GIF_STATUS_IO_ERROR`.
5. A malformed GIF closes a successfully opened source exactly once.
6. Repeated open/decode/close cycles do not leak handles or pool slots.
7. If concurrency is supported, two decoders do not share mutable source state.
8. The port never reports `actual_bytes > requested_bytes`.
9. The port never returns `GIF_PORTING_OK` with zero bytes.
10. Target byte-source symbols appear only in `port/gif_porting.c` and the target's own source stack.

The repository host tests use a separate memory-backed test port to verify the same open/read/close contract, including short reads, EOF, injected errors, and close ownership.

## 10. Configure decoder memory

Memory configuration is deliberately separate from byte-source porting. The port uses `gif_porting_mem_alloc()` and `gif_porting_mem_free()` only to create and release its per-stream handle in the selected decoder allocator domain. They are not application APIs. Select the backend, size a BUILTIN pool, or implement the independent PRIVATE provider according to [MEMORY_CONFIGURATION.md](MEMORY_CONFIGURATION.md).

## 11. Diagnose common failures

### Image descriptor reads fail after the animation runs for a while

Check the port before changing giflib:

- confirm the source handle remains valid until decoder close;
- confirm the source position advances by exactly `actual_bytes`;
- confirm a short read is not reported as a successful full read;
- confirm EOF is returned only when no future byte exists;
- confirm `actual_bytes` is cleared on every call;
- confirm another subsystem does not reuse the source buffer or handle.

### The decoder hangs in the read bridge

The usual cause is `GIF_PORTING_OK` with `actual_bytes == 0`. Return EOF when the source has ended or I/O error when it cannot make progress.

### One resource works but another active decoder fails to open

Confirm that the port uses the standard dynamically allocated wrapper and that the selected allocator has capacity for every live decoder and source handle. A product that deliberately uses fixed source slots must either provide another independent slot or reject the additional open cleanly.

### Public code starts including target byte-source headers

Move those includes and calls back into `port/gif_porting.c`. The public config carries only the resource identifier. The application selects a resource but does not open or read it for the decoder.

## 12. Port acceptance checklist

A port is complete when all statements below are true:

- the application can select resource A, close it, then select resource B at run time;
- only `port/gif_porting.c` contains target byte-source integration;
- `port/gif_porting.h` remains unchanged and platform-neutral;
- when PRIVATE is selected, allocator integration is isolated to `port/gif_mem_private.c` and does not appear in the storage port;
- public and hidden decoder files remain unchanged;
- open returns a non-`NULL` handle only after complete success;
- read obeys the byte-count and terminal-status rules;
- close is `NULL`-safe and releases each successful open exactly once;
- concurrent decoder behavior is explicitly rejected or correctly supported;
- no seek or source-size operation is required;
- the application owns the framebuffer, display, and playback timing;
- target source initialization remains outside the decoder;
- host tests, target compilation, and target runtime checks pass.

With these checks complete, the application needs only `gif_decoder.h` from the decoder library. It does not need hidden decoder headers, giflib internals, `gif_porting.h`, or the target's low-level byte-source API header. An application may still use its own resource descriptor type to select what the port should open.
