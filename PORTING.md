# Platform porting boundary

The decoder has one platform integration point: `gif_porting.c`. A target port
must not add filesystem, device-driver, BSP, RTOS, cache, display, or timer code
to any other library file.

## File ownership

- `include/gif_decoder.h` and `gif_decoder.c` are the fixed application API.
  Applications include the header and call these functions, but do not modify
  either file for a new target.
- `gif_decoder_core.h` and `gif_decoder_core.c` are hidden implementation.
  Applications and ports must not include or call the core interface.
- `gif_porting.h` is the fixed platform contract. It deliberately contains no
  FatFs, stdio, BSP, or operating-system type and normally needs no changes.
- `gif_porting.c` is the only user-editable porting file. Platform includes,
  handle types, storage calls, and their error mapping all belong here.

The application still chooses which resource to decode and what to do with a
decoded frame. Resource selection, display, and frame delay are application
behavior; implementing storage access is platform porting.

## Source identifier

`GifDecoderConfig.source_identifier` is an opaque value passed once to
`gif_porting_open()`. The port defines its meaning:

- a FatFs or stdio port can interpret it as a path string;
- a memory port can interpret it as a memory-source descriptor;
- a flash port can interpret it as an asset record or logical resource key.

The configuration object itself is needed only during `gif_decoder_open()`.
For maximum portability, the object referenced by `source_identifier` should
remain valid until `gif_decoder_close()` unless the selected port explicitly
documents that it copies all required information during open.

## Required operations

The port implements exactly three operations:

1. `gif_porting_open()` resolves the source identifier and returns a non-NULL
   per-stream handle.
2. `gif_porting_read()` performs forward-only sequential reads and reports the
   exact byte count together with OK, EOF, or I/O error.
3. `gif_porting_close()` closes the source and releases only resources owned by
   the port.

Every successful `gif_porting_open()` is paired with exactly one close,
including cases where the GIF header is invalid or decoder allocation fails.
The decoder never requires seek, rewind, tell, file size, or filesystem
metadata. A successful short read is legal. EOF or I/O error may accompany
final valid bytes, but `actual_bytes` must never exceed `requested_bytes`.

The repository template intentionally returns `GIF_PORTING_IO_ERROR` until a
target implementation is supplied. This keeps the unported library buildable
without silently selecting stdio, FatFs, or another platform dependency.

## Minimal FatFs mapping

A simple single-stream FatFs port can keep one `FIL` object inside
`gif_porting.c`, treat `source_identifier` as `const char *`, and map operations
as follows:

```c
#include "gif_porting.h"
#include "ff.h"

#include <limits.h>

static FIL gif_file;
static int gif_file_is_open;

GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const char *path = (const char *)source_identifier;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;
    if (path == NULL || gif_file_is_open ||
        f_open(&gif_file, path, FA_READ) != FR_OK) {
        return GIF_PORTING_IO_ERROR;
    }
    gif_file_is_open = 1;
    *out_handle = &gif_file;
    return GIF_PORTING_OK;
}

GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    UINT bytes_read = 0;
    UINT request;
    FRESULT result;

    if (handle == NULL || destination == NULL || actual_bytes == NULL ||
        requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;
    request = requested_bytes > (size_t)UINT_MAX ? UINT_MAX
                                                 : (UINT)requested_bytes;
    result = f_read((FIL *)handle, destination, request, &bytes_read);
    *actual_bytes = (size_t)bytes_read;
    if (result != FR_OK) {
        return GIF_PORTING_IO_ERROR;
    }
    return bytes_read == request ? GIF_PORTING_OK : GIF_PORTING_EOF;
}

void gif_porting_close(GifPortingHandle handle) {
    if (handle != NULL) {
        (void)f_close((FIL *)handle);
        gif_file_is_open = 0;
    }
}
```

This example intentionally supports one active decoder. A target that needs
concurrent decoders can replace the single object with a port-owned pool or
another handle-management policy, still entirely inside `gif_porting.c`.
The example caps each FatFs request to `UINT_MAX`; the decoder's short-read
bridge automatically requests any remaining bytes.

FatFs disk initialization, Xilinx SD controller setup, and `diskio.c` remain
responsibilities of the BSP/FatFs installation. They are below this library's
three-function byte-source boundary and must not be copied into the decoder.
