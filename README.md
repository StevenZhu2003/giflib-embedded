# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded systems. It retains giflib's mature parser and LZW decoder while placing fixed public, private, and porting boundaries between the decoder and platform-specific storage, display, timing, and operating-system services.

> **Upstream attribution:** This project incorporates and modifies substantial source code from giflib and the TLSF allocator. The original authors retain copyright in their respective code. giflib-derived sources retain their MIT license terms, while TLSF-derived sources retain Matthew Conte's BSD-3-Clause notice; the LVGL integration lineage is also recorded. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact file boundaries, upstream revisions and changes, omitted components, and project-original files.

The project is derived from giflib 6.1.3 and is currently under active development. The low-level `gif_lib.h` interface is retained internally during the migration; it is not intended to be the final application-facing API.

## Design goals

- Stream input through the target's forward-only porting implementation.
- Avoid `stdio`, filesystem APIs, an RTOS, and target-specific dependencies in the decoder core.
- Decode incrementally without `DGifSlurp()` or accumulating `SavedImages`.
- Composite frames into a framebuffer owned by the caller.
- Keep display, cache management, and frame delays in the application, and storage access exclusively in `port/gif_porting.c`.
- Make decoder-owned dynamic memory explicit, bounded, and independent of the C library heap by default.
- Preserve the proven giflib parser and LZW implementation with minimal, documented changes.

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

- Forward-only GIF streams through one platform porting file, with explicit short-read, EOF, and I/O-error handling.
- Streaming non-interlaced RGB888/BGR888 composition with global/local palettes, image rectangles, transparency, timing metadata, and disposal methods 0/1.
- Caller-owned framebuffer, opaque decoder API, and C99 host/cross builds.
- Selectable BUILTIN fixed-pool, PRIVATE provider, or LIBC allocator backends; the default BUILTIN mode has no libc heap dependency.

Unsupported GIF features return `GIF_STATUS_UNSUPPORTED_FEATURE` rather than being decoded with incorrect semantics. See [docs/TODO_LIST.md](docs/TODO_LIST.md) for planned work and priority.

## Getting started

The complete application integration and API tutorial is [docs/USER_GUIDE.md](docs/USER_GUIDE.md). It covers memory backend selection, the single storage-port boundary, decoder lifecycle, and status handling. The guide intentionally leaves output-storage, display, and timing policy to the application.

Fixed-pool sizing and static-RAM planning are retained separately in [docs/MEMORY_CONFIGURATION.md](docs/MEMORY_CONFIGURATION.md).

For a runnable reference application, see [examples/embedded_player](examples/embedded_player/README.md). The detailed platform byte-source contract is in [docs/PORTING_GUIDE.md](docs/PORTING_GUIDE.md).

## Dependencies

The library requires C99 and basic memory/string operations such as `memset` and `memcpy`. The default BUILTIN configuration has no C-library heap dependency. The fixed facade and hidden core do not open files and have no direct filesystem dependency; only the user-supplied `port/gif_porting.c` may perform storage I/O.

FatFs is not bundled, linked, or required by this repository. The Porting Guide contains a project-original FatFs integration example for users who already provide FatFs in a parent project. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the precise bundled versus referenced dependency boundary.

## Repository layout

- `include/`: the sole application-facing API and centralized configuration.
- `src/`: fixed facade, hidden decoder/compositor, and project-original memory wrappers.
- `port/`: storage port plus the PRIVATE allocator template when selected.
- `vendor/`: upstream-derived or modified third-party libraries, including the giflib parser/LZW code and the TLSF allocator.
- `tests/`: host-side regression tests and memory-backed test port.
- `examples/`: complete applications with their own porting implementation.
- `docs/`: the user guide, detailed porting guide, roadmap, design records, and source documentation convention.
- `docs/USER_GUIDE.md`: the single application API and integration tutorial.
- `CMakeLists.txt`: host and cross-compilation build definition.

## Source documentation

Project-original C code uses a Doxygen-compatible embedded-library style for files, functions, callbacks, types, constants, fields, and non-obvious implementation decisions. The complete convention is recorded in [docs/COMMENTING_STYLE.md](docs/COMMENTING_STYLE.md). Existing comments in giflib-derived files intentionally remain in their upstream form and are not restyled merely for visual consistency.

## Upstream changes

Changes to the upstream-derived parser/LZW code are deliberately limited to portability and correctness fixes:

- removed unused stdio, POSIX, and Windows file-I/O headers;
- made the private header self-contained for fixed-width integer macros;
- made image pixel-count multiplication safe on 32-bit targets;
- retained the specific logical-screen read or allocation error from `DGifOpen()`;
- routed all retained giflib allocation sites through the project-private allocator facade, retaining `reallocarray` overflow and zero-size semantics.

The encoder, command-line utilities, file-opening helpers, and upstream build system are not included.

## License and attribution

The repository is distributed under the MIT License; see [LICENSE](LICENSE). Portions derived from giflib retain their upstream copyright and license notices. The BUILTIN TLSF implementation retains Matthew Conte's BSD-3-Clause notice and records its LVGL modification lineage. See [the retained giflib license](vendor/giflib/COPYING) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
