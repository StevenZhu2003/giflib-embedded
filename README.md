# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded
systems. It retains giflib's mature parser and LZW decoder while placing a
small portability boundary between the decoder and platform-specific storage,
display, timing, and operating-system services.

> **Upstream attribution:** This project incorporates and modifies a
> substantial amount of source code from giflib. The original authors retain
> copyright in that code. See
> [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact file boundary,
> upstream changes, omitted components, and project-original files.

The project is derived from giflib 6.1.3 and is currently under active
development. The low-level `gif_lib.h` interface is retained internally during
the migration; it is not intended to be the final application-facing API.

## Design goals

- Stream input through a caller-supplied byte source.
- Avoid `stdio`, POSIX file APIs, FatFs, an RTOS, and vendor BSP dependencies in
  the decoder core.
- Decode incrementally without `DGifSlurp()` or accumulating `SavedImages`.
- Composite frames into a framebuffer owned by the caller.
- Keep display, cache management, frame delays, and storage adapters in the
  application or platform layer.
- Preserve the proven giflib parser and LZW implementation with minimal,
  documented changes.

```text
platform byte source
        |
   read callback
        |
========================
  portability boundary
========================
        |
  public decoder facade
        |
 streaming compositor
        |
 private trimmed giflib
    parser + LZW
```

## Current status

Completed:

- independent C99 host and cross-compilation build;
- host-side regression and allocation-balance tests;
- removal of unused platform file-I/O headers from the decoder core;
- private-header self-containment;
- image pixel-count overflow protection;
- preservation of the underlying `DGifOpen()` error;
- an opaque public decoder facade;
- a short-read-aware byte-source callback with distinct EOF and I/O errors;
- platform-independent stream information and status mapping;
- caller-owned output surfaces with checked capacity and arbitrary stride;
- streaming non-interlaced RGB888 and BGR888 frame composition;
- global and local color tables, partial image rectangles, and disposal 0/1.

The next stage will add Graphic Control Extension state, transparency, and
animation timing metadata. Later stages will add disposal modes 2/3, RGBA
output, and interlace handling. Allocator abstraction and platform adapters are
intentionally deferred until the streaming architecture is stable.

## Public API

Applications include only `gif_decoder.h`. The decoder is opaque, and the
application supplies input through `GifReadCallback`:

```c
#include <gif_decoder.h>

GifDecoderConfig config = {
    .read = application_read,
    .io_context = &application_source,
};
GifDecoder *decoder = NULL;
GifStreamInfo stream;

GifStatus status = gif_decoder_open(&config, &decoder, &stream);
if (status == GIF_STATUS_OK) {
    /* Allocate a framebuffer using the returned canvas dimensions. */
    GifOutputSurface surface = {
        .pixels = framebuffer,
        .capacity_bytes = framebuffer_size,
        .stride_bytes = framebuffer_stride,
        .pixel_format = GIF_PIXEL_RGB888,
    };
    GifFrameInfo frame;

    if (gif_decoder_bind_output(decoder, &surface) == GIF_STATUS_OK) {
        while ((status = gif_decoder_next_frame(decoder, &frame)) ==
               GIF_STATUS_OK) {
            display_framebuffer(surface.pixels);
        }
    }
    gif_decoder_close(decoder);
}
```

The read callback reports both a `GifReadStatus` and the actual byte count.
`GIF_READ_OK` may return a non-zero short read; the internal bridge continues
reading until giflib's request is satisfied. `GIF_READ_EOF` and
`GIF_READ_IO_ERROR` remain distinct and are mapped to public decoder statuses.
The callback does not receive or expose any giflib type.

At the current stage, interlaced images, transparency, non-zero frame delays,
user-input GCEs, disposal modes 2/3, and Plain Text Extensions return
`GIF_STATUS_UNSUPPORTED_FEATURE`. They are not silently decoded with incorrect
semantics.

## Dependencies

The library requires a C99 compiler and the basic C runtime facilities used by
the trimmed giflib core, including `malloc`, `calloc`, `realloc`, `free`, and
memory/string operations. It does not open files and has no direct dependency
on a filesystem implementation.

## Build and test

Configure and test on the host:

```sh
cmake -S . -B build -DGIFLIB_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For cross-compilation, provide a normal CMake toolchain file and disable tests
that cannot execute on the build host:

```sh
cmake -S . -B build/target \
  -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake \
  -DGIFLIB_BUILD_TESTS=OFF
cmake --build build/target
```

This produces the `giflib_embedded` static library. Toolchain installation
paths and target-specific compiler flags belong in the caller's toolchain file,
not in this repository.

`cmake --install build --prefix <destination>` installs only the static library
and the public `gif_decoder.h` header; private giflib headers are not installed.

## Repository layout

- `include/gif_decoder.h`: the only application-facing public header.
- `gif_decoder.c`: opaque facade and byte-source bridge.
- `dgif_lib.c`, `gifalloc.c`, `gif_err.c`: trimmed giflib decoder core.
- `gif_lib.h`, `gif_lib_private.h`: low-level and private giflib interfaces.
- `openbsd-reallocarray.c`: overflow-safe allocation compatibility helper.
- `tests/`: host-side regression tests.
- `CMakeLists.txt`: host and cross-compilation build definition.
- `COMMENTING_STYLE.md`: repository-wide source documentation convention.

## Source documentation

Project-original C code uses a Doxygen-compatible embedded-library style for
files, functions, callbacks, types, constants, fields, and non-obvious
implementation decisions. The complete convention is recorded in
[COMMENTING_STYLE.md](COMMENTING_STYLE.md). Existing comments in giflib-derived
files intentionally remain in their upstream form and are not restyled merely
for visual consistency.

## Upstream changes

Changes to the upstream-derived parser/LZW code are deliberately limited to
portability and correctness fixes:

- removed unused stdio, POSIX, and Windows file-I/O headers;
- made the private header self-contained for fixed-width integer macros;
- made image pixel-count multiplication safe on 32-bit targets;
- retained the specific logical-screen read or allocation error from
  `DGifOpen()`.

The encoder, command-line utilities, file-opening helpers, and upstream build
system are not included.

## License and attribution

The repository is distributed under the MIT License; see [LICENSE](LICENSE).
Portions derived from giflib retain their upstream copyright and license
notices. See [COPYING](COPYING) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
