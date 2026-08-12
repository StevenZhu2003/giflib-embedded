# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded
systems. It retains giflib's mature parser and LZW decoder while placing fixed
public, private, and porting boundaries between the decoder and
platform-specific storage, display, timing, and operating-system services.

> **Upstream attribution:** This project incorporates and modifies substantial
> source code from giflib and the TLSF allocator. The original authors retain
> copyright in their respective code. giflib-derived sources retain their MIT
> license terms, while TLSF-derived sources retain Matthew Conte's BSD-3-Clause
> notice; the LVGL integration lineage is also recorded. See
> [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact file
> boundaries, upstream revisions and changes, omitted components, and
> project-original files.

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
- Make decoder-owned dynamic memory explicit, bounded, and independent of the
  C library heap by default.
- Preserve the proven giflib parser and LZW implementation with minimal,
  documented changes.

```text
application
    |
include/gif_decoder.h + src/gif_decoder.c     fixed public API
    |
    +---- port/gif_porting.h ---------------- fixed port contract
    |              |
    |         port/gif_porting.c ------------ storage port implementation
    |
    +---- include/gif_config.h -------------- centralized build configuration
    |              |
    |         src/memory/gif_mem.c ---------- private allocator facade
    |              |
    |         BUILTIN: vendor/tlsf fixed pool / PRIVATE: port/gif_mem_private.c
    |         LIBC: src/memory/gif_mem_libc.c
    |
src/gif_decoder_core.h + .c ---------------- hidden implementation
    |
vendor/giflib/ ------------------------------ parser + LZW
```

## Supported today

- Forward-only GIF streams through one platform porting file, with explicit
  short-read, EOF, and I/O-error handling.
- Streaming non-interlaced RGB888/BGR888 composition with global/local palettes,
  image rectangles, transparency, timing metadata, and disposal methods 0/1.
- Caller-owned framebuffer, opaque decoder API, and C99 host/cross builds.
- Selectable BUILTIN fixed-pool, PRIVATE provider, or LIBC allocator backends;
  the default BUILTIN mode has no libc heap dependency.

Unsupported GIF features return `GIF_STATUS_UNSUPPORTED_FEATURE` rather than
being decoded with incorrect semantics. See [TODO_LIST.md](TODO_LIST.md) for
planned work and priority.

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

## Memory configuration

`include/gif_config.h` is the central configuration entry point for this and
future library-wide compile-time options. The default is the BUILTIN backend:
it owns one explicitly aligned 48 KiB TLSF pool, never expands it, never falls
back to a C library heap, and never stores the caller's framebuffer. Its
dynamic decoder memory consumption cannot exceed `GIF_MEM_POOL_SIZE`.

### RAM sizing and the framebuffer-balance recommendation

The caller-owned RGB888/BGR888 framebuffer requires:

```text
F = canvas_width × canvas_height × 3 bytes
```

The public streaming decoder does not retain decoded frames or `SavedImages`.
Its maximum live allocation payload, for a supported stream with both a global
and local 256-entry palette, is bounded by:

```text
D_payload(W) = sizeof(GifDecoder)
             + sizeof(GifFileType)
             + sizeof(GifFilePrivateType)
             + 2 × (sizeof(ColorMapObject) + 256 × sizeof(GifColorType))
             + W × sizeof(GifPixelType)
```

`W` is the maximum image width the application intends to decode; it is at
most the GIF canvas width. The two palette terms cover the point at which a
local colour table is active while the global table remains allocated. The row
buffer is one palette-index byte per image pixel, not a full RGB row.

For the verified 32-bit ARM build, this is approximately:

```text
D_payload(W) = 26,584 + W bytes
```

This payload formula excludes TLSF control/allocation metadata and a project
safety margin. Select the fixed pool with:

```text
GIF_MEM_POOL_SIZE >= D_payload(W)
                   + TLSF_control_and_allocation_metadata
                   + safety_margin
```

On the default 48 KiB pool in the current 32-bit ARM configuration, TLSF
control metadata is approximately 1,340 bytes; per-allocation metadata,
alignment rounding, and the pool sentinel must also be allowed for. A 4 KiB
or larger application-specific safety margin is a sensible starting point,
then validate with the application's largest intended GIFs and fragmentation
workload.

For static-RAM planning, distinguish this instantaneous payload from the
reserved pool. BUILTIN reserves exactly:

```text
P = GIF_MEM_POOL_SIZE
R_library = P
R_decoder_plus_framebuffer = P + F
```

Therefore the recommended balance condition is:

```text
F >= P
```

It means the visible image storage is at least as large as the complete
decoder-owned static allocation. It is a RAM-budget recommendation, not a
functional requirement: a 128 × 64 RGB888 framebuffer is 24 KiB and can still
decode correctly with the default 48 KiB pool. Conversely, merely checking
`F >= D_payload(W)` is insufficient for static memory accounting when the
configured pool `P` is larger than the current live payload.

With the default 48 KiB pool, `F >= P` corresponds to at least 16,384 RGB888
pixels. For example, 128 × 64 does not meet this balance target, whereas
240 × 320 does. Small-display products can instead choose a smaller pool after
measuring their real maximum width, palette use, failure paths, and margin.

For a CMake build, choose the backend at configuration time:

```sh
cmake -S . -B build/host/builtin -DGIF_MEM_BACKEND=BUILTIN
cmake -S . -B build/host/private -DGIF_MEM_BACKEND=PRIVATE
cmake -S . -B build/host/libc -DGIF_MEM_BACKEND=LIBC
```

`PRIVATE` compiles no TLSF code. It makes `port/gif_mem_private.c` the second
porting point; the application supplies only `gif_mem_private_malloc()`,
`gif_mem_private_realloc()`, and `gif_mem_private_free()`. The library keeps
zero-size behavior, overflow checking, clearing, and reallocarray semantics in
its private facade. In particular, `realloc(p, 0)` releases `p`, while the
retained giflib-compatible `realloc_array(p, 0, n)` returns `NULL` and leaves
`p` unchanged. The storage port and private allocator are independent.

`LIBC` uses the selected C runtime's `malloc()`, `realloc()`, and `free()`
only behind the same private facade. It is appropriate for hosted builds and
targets that deliberately choose a C-library heap. It does not make the
decoder core or retained giflib sources directly dependent on those functions,
and it does not alter the default no-libc-heap property of BUILTIN.

No backend is internally synchronized by this library. Applications must
serialize concurrent decoder activity, or make the selected provider safe.
LIBC has the thread-safety characteristics of its C runtime; PRIVATE has those
of the application-provided primitives.

The allocator facade is intentionally private at this stage. An evaluation of
a future optional application-facing memory service, including ownership,
lifecycle, alignment, OOM, U8g2-buffer, and future LVGL concerns, is available
in [docs/MEMORY_API_EVALUATION.md](docs/MEMORY_API_EVALUATION.md).

## Dependencies

The library requires C99 and basic memory/string operations such as `memset`
and `memcpy`. The default BUILTIN configuration has no C-library heap
dependency. The fixed facade and hidden core do not open files and have no
direct filesystem dependency; only the user-supplied `port/gif_porting.c` may
perform storage I/O.

FatFs is not bundled, linked, or required by this repository. The Porting Guide
contains a project-original FatFs integration example for users who already
provide FatFs in a parent project. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the precise bundled versus
referenced dependency boundary.

## Build and test

Configure and test on the host:

```sh
cmake -S . -B build/host/builtin -DGIFLIB_BUILD_TESTS=ON
cmake --build build/host/builtin
ctest --test-dir build/host/builtin --output-on-failure
```

For cross-compilation, provide a normal CMake toolchain file and disable tests
that cannot execute on the build host:

```sh
cmake -S . -B build/target/default \
  -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake \
  -DGIFLIB_BUILD_TESTS=OFF
cmake --build build/target/default
```

This produces the `giflib_embedded` static library. Toolchain installation
paths and target-specific compiler flags belong in the caller's toolchain file,
not in this repository.

Build the complete hosted example with:

```sh
cmake -S . -B build/host/example \
  -DGIFLIB_BUILD_TESTS=OFF \
  -DGIFLIB_BUILD_EXAMPLES=ON
cmake --build build/host/example
```

Run `gif_embedded_player_example` from the directory where the hosted display
backend should write its `gif_frame_*.ppm` captures. See the example README for
the application structure and target-integration steps.

The repository's `port/gif_porting.c` is a compile-safe, unconfigured template.
Implement its open/read/close bodies for the target before decoding. No other
library or application file needs platform storage glue.

`cmake --install build/host/builtin --prefix <destination>` installs only the static library
plus `gif_decoder.h` and `gif_config.h`; private giflib and allocator headers
are not installed.

## Repository layout

- `include/`: the sole application-facing API and centralized configuration.
- `src/`: fixed facade, hidden decoder/compositor, and project-original memory
  wrappers.
- `port/`: storage port plus the PRIVATE allocator template when selected.
- `vendor/`: upstream-derived or modified third-party libraries, including the
  giflib parser/LZW code and the TLSF allocator.
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
  `DGifOpen()`;
- routed all retained giflib allocation sites through the project-private
  allocator facade, retaining `reallocarray` overflow and zero-size semantics.

The encoder, command-line utilities, file-opening helpers, and upstream build
system are not included.

## License and attribution

The repository is distributed under the MIT License; see [LICENSE](LICENSE).
Portions derived from giflib retain their upstream copyright and license
notices. The BUILTIN TLSF implementation retains Matthew Conte's BSD-3-Clause
notice and records its LVGL modification lineage. See
[the retained giflib license](vendor/giflib/COPYING) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
