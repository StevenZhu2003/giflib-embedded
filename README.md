# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded
systems. It retains giflib's mature parser and LZW decoder while placing fixed
public, private, and porting boundaries between the decoder and
platform-specific storage, display, timing, and operating-system services.

> **Upstream attribution:** This project incorporates and modifies a
> substantial amount of source code from giflib. The original authors retain
> copyright in that code. See
> [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact file boundary,
> upstream changes, omitted components, and project-original files.

The project is derived from giflib 6.1.3 and is currently under active
development. The low-level `gif_lib.h` interface is retained internally during
the migration; it is not intended to be the final application-facing API.

## Design goals

- Stream input through the target's forward-only porting implementation.
- Avoid `stdio`, filesystem APIs, an RTOS, and target-specific dependencies in
  the decoder core.
- Decode incrementally without `DGifSlurp()` or accumulating `SavedImages`.
- Composite frames into a framebuffer owned by the caller.
- Keep display, cache management, and frame delays in the application, and
  storage access exclusively in `port/gif_porting.c`.
- Preserve the proven giflib parser and LZW implementation with minimal,
  documented changes.

```text
application
    |
include/gif_decoder.h + src/gif_decoder.c     fixed public API
    |
    +---- port/gif_porting.h ---------------- fixed port contract
    |              |
    |         port/gif_porting.c ------------ only editable port file
    |
src/gif_decoder_core.h + .c ---------------- hidden implementation
    |
vendor/giflib/ ------------------------------ parser + LZW
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
- a short-read-aware porting bridge with distinct EOF and I/O errors;
- platform-independent stream information and status mapping;
- caller-owned output surfaces with checked capacity and arbitrary stride;
- streaming non-interlaced RGB888 and BGR888 frame composition;
- global and local color tables, partial image rectangles, and disposal 0/1;
- streaming Graphic Control Extension state with frame-scoped delay and
  transparency;
- transparent palette pixels that preserve the existing composited canvas;
- public animation timing metadata in milliseconds without decoder-side waits;
- fixed public, hidden-core, and single-file platform-porting boundaries.

The next stage will add disposal mode 2. Later stages will add RGBA output,
disposal mode 3, and interlace handling. Allocator abstraction and optional
reusable storage-adapter modules remain deferred until the streaming
architecture is stable; examples may still include a local port implementation
to demonstrate the stable three-function contract.

## Public API

Applications include only `gif_decoder.h`. The decoder is opaque, and the
application selects a source without including a filesystem or driver header:

```c
#include <gif_decoder.h>

const void *selected_resource = application_select_gif();
GifDecoderConfig config = {
    .source_identifier = selected_resource,
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
            application_delay_ms(frame.delay_ms);
        }
    }
    gif_decoder_close(decoder);
}
```

The target implementation in `port/gif_porting.c` decides how to interpret the
opaque source identifier and supplies forward-only bytes. The application does
not implement a callback and does not receive or expose a giflib, filesystem,
stdio, or device-driver type. See
[the porting guide](docs/PORTING_GUIDE.md) for the complete three-function
contract, implementation tutorial, and verification procedure.

The application owns `application_select_gif()`, `display_framebuffer()`, and
`application_delay_ms()`; they are not decoder APIs. Frame delay, including a
zero delay, is reported exactly in milliseconds and the application chooses
how or whether to wait.

At the current stage, interlaced images, user-input GCEs, disposal modes 2/3,
and Plain Text Extensions return `GIF_STATUS_UNSUPPORTED_FEATURE`. They are not
silently decoded with incorrect semantics.

## Complete example

The buildable [embedded GIF player example](examples/embedded_player/README.md)
is structured as a small real application rather than a decoder test. It plays
a project-original 128 x 64 animation from read-only memory, reuses a 24 KiB
RGB888 framebuffer, hands every completed canvas to a display boundary, and
applies frame delays in application code. The hosted backend writes standard
PPM frame captures; an embedded project replaces only that display/time backend
with its own implementation. No platform SDK or filesystem is required.
The bundled animation was created specifically for this repository and was not
downloaded or adapted from external media; its detailed provenance and license
are documented in the example README and `THIRD_PARTY_NOTICES.md`.

## Dependencies

The library requires a C99 compiler and the basic C runtime facilities used by
the trimmed giflib core, including `malloc`, `calloc`, `realloc`, `free`, and
memory/string operations. The fixed facade and hidden core do not open files
and have no direct filesystem dependency; only the user-supplied
`port/gif_porting.c` may do so.

FatFs is not bundled, linked, or required by this repository. The Porting Guide
contains a project-original FatFs integration example for users who already
provide FatFs in a parent project. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the precise bundled versus
referenced dependency boundary.

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

Build the complete hosted example with:

```sh
cmake -S . -B build/example \
  -DGIFLIB_BUILD_TESTS=OFF \
  -DGIFLIB_BUILD_EXAMPLES=ON
cmake --build build/example
```

Run `gif_embedded_player_example` from the directory where the hosted display
backend should write its `gif_frame_*.ppm` captures. See the example README for
the application structure and target-integration steps.

The repository's `port/gif_porting.c` is a compile-safe, unconfigured template.
Implement its open/read/close bodies for the target before decoding. No other
library or application file needs platform storage glue.

`cmake --install build --prefix <destination>` installs only the static library
and the public `gif_decoder.h` header; private giflib headers are not installed.

## Repository layout

- `include/`: the sole application-facing header.
- `src/`: fixed public facade plus hidden decoder/compositor implementation.
- `port/`: stable port contract and the only target-editable source file.
- `vendor/giflib/`: upstream-derived parser, LZW implementation, and license.
- `tests/`: host-side regression tests and memory-backed test port.
- `examples/`: complete applications with their own porting implementation.
- `docs/`: the detailed porting guide and source documentation convention.
- `CMakeLists.txt`: host and cross-compilation build definition.

## Source documentation

Project-original C code uses a Doxygen-compatible embedded-library style for
files, functions, callbacks, types, constants, fields, and non-obvious
implementation decisions. The complete convention is recorded in
[docs/COMMENTING_STYLE.md](docs/COMMENTING_STYLE.md). Existing comments in
giflib-derived files intentionally remain in their upstream form and are not
restyled merely for visual consistency.

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
notices. See [the retained giflib license](vendor/giflib/COPYING) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
