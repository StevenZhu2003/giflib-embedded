# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded systems that need to play GIF resources from a sequential byte source. It retains giflib's mature parser and LZW decoder while placing fixed public, private, and porting boundaries between the decoder and platform-specific storage, display, timing, and operating-system services.

> **Upstream attribution:** This project incorporates and modifies substantial source code from giflib and the TLSF allocator. The original authors retain copyright in their respective code. giflib-derived sources retain their MIT license terms, while TLSF-derived sources retain Matthew Conte's BSD-3-Clause notice; the LVGL integration lineage is also recorded. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact file boundaries, upstream revisions and changes, omitted components, and project-original files.

The project is derived from giflib 6.1.3 and is currently under active development. The low-level `gif_lib.h` interface is retained internally during the migration; it is not intended to be the final application-facing API.

The input is consumed forward-only, so the complete GIF resource does not need to reside in RAM. The decoder intentionally composes each frame into a complete logical-screen framebuffer supplied and owned by the caller. This makes every successful frame ready for presentation and keeps display hand-off outside the library; it is a predictable composition boundary, not an attempt to target the smallest possible RAM footprint.

## Design goals

- Stream input through the target's forward-only porting implementation.
- Avoid `stdio`, filesystem APIs, an RTOS, and target-specific dependencies in the decoder core.
- Decode incrementally without `DGifSlurp()` or accumulating `SavedImages`.
- Composite frames into one complete framebuffer owned by the caller.
- Keep display, cache management, scheduling, and frame delays in the application, and storage access exclusively in `port/gif_porting.c`.
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
- Streaming non-interlaced RGB888/BGR888 composition with global and local palettes, image rectangles, transparency, timing metadata, and disposal methods 0/1.
- A caller-owned, complete logical-screen framebuffer and an opaque C99 decoder API.
- Four selectable decoder-memory backends: BUILTIN fixed pool, PRIVATE provider, LIBC, and LVGL. The default BUILTIN mode has no libc heap dependency.

Interlaced images, disposal methods 2/3, and Graphic Control Extension user-input requests currently return `GIF_STATUS_UNSUPPORTED_FEATURE` rather than being decoded with incorrect semantics. The planned work and its priority are only in [docs/TODO_LIST.md](docs/TODO_LIST.md).

## Documentation

- [User Guide](docs/USER_GUIDE.md): application configuration, public API, and one complete decode lifecycle.
- [Porting Guide](docs/PORTING_GUIDE.md): the platform-neutral byte-source contract and reference ports.
- [Memory Configuration](docs/MEMORY_CONFIGURATION.md): backend selection, fixed-pool sizing, lifecycle, and RAM planning.
- [Embedded player example](examples/embedded_player/README.md): a hosted, platform-independent reference application with a real animation resource.
- [Project TODO list](docs/TODO_LIST.md): planned work and priority; it is not a feature promise.

## Dependencies

The library requires C99 and basic memory/string operations such as `memset` and `memcpy`. Its public facade and hidden core do not open files or depend on a filesystem; only the user-supplied `port/gif_porting.c` performs source I/O. BUILTIN has no C-library heap dependency. The optional LVGL backend requires an application-provided LVGL 8.4 or 9.x library and uses only its public allocator API.

FatFs is not bundled, linked, or required by this repository. The Porting Guide contains a project-original FatFs integration example for users who already provide FatFs in a parent project. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the precise bundled versus referenced dependency boundary.

## Repository layout

- `include/`: the sole application-facing API and centralized configuration.
- `src/`: fixed facade, hidden decoder/compositor, and project-original memory wrappers.
- `port/`: storage port plus the PRIVATE allocator template when selected.
- `vendor/`: upstream-derived or modified third-party libraries, including the giflib parser/LZW code and the TLSF allocator.
- `tests/`: host-side regression tests and memory-backed test port.
- `examples/`: complete applications with their own porting implementation.
- `docs/`: guides, memory/configuration records, and the project roadmap.

## License and attribution

The repository is distributed under the MIT License; see [LICENSE](LICENSE). Portions derived from giflib retain their upstream copyright and license notices. The BUILTIN TLSF implementation retains Matthew Conte's BSD-3-Clause notice and records its LVGL modification lineage. See [the retained giflib license](vendor/giflib/COPYING) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
