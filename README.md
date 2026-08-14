# Portable GIF Decoder for Embedded Systems Based on giflib

`giflib-embedded` is a decoder-only, platform-neutral GIF library for embedded applications. It retains giflib's parser and LZW decoder, reads a forward-only byte source through one porting layer, and composes decoded frames into an application-owned RGB888, BGR888, or RGB565 framebuffer.

The library deliberately does not open files, own a display, schedule playback, or manage application buffers. The application supplies the storage port, framebuffer, display hand-off, and frame timing. A complete GIF resource therefore does not need to reside in RAM, while the complete logical-screen framebuffer is an intentional composition boundary: every successful frame is ready to present.

> **Upstream attribution:** This project incorporates and modifies substantial source code from giflib and the TLSF allocator. The original authors retain copyright in their respective code. giflib-derived sources retain their MIT license terms, while TLSF-derived sources retain Matthew Conte's BSD-3-Clause notice; the LVGL integration lineage is also recorded. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for exact file boundaries, upstream revisions, changes, omitted components, and project-original files.

## Suitable use

- Decode GIF streams from any sequential platform byte source; no `stdio`, pathname, filesystem, RTOS, or vendor SDK is required in the decoder.
- Present complete RGB888, BGR888, or RGB565 frames from a framebuffer that remains owned by the application.
- Use a bounded, library-owned pool by default, or select PRIVATE, LIBC, or LVGL allocation backends at build time.
- Use optional, compile-time-trimmable Restore-to-Previous (disposal method 3) support only for products that accept method-3 GIFs and provide its application-owned snapshot storage; the default binary omits it.

The current decoder supports global and local palettes, transparency, interlace, image rectangles, timing metadata, and disposal methods 0, 1, and 2. Restore-to-previous (disposal method 3) is an optional build feature: it is disabled by default and enabled with `GIF_ENABLE_DISPOSAL_METHOD_3=1` (or CMake `-DGIFLIB_ENABLE_DISPOSAL_METHOD_3=ON`). An enabled method-3 frame needs a distinct caller-owned snapshot in `GifOutputSurface`; if it is absent or too small, decoding returns `GIF_STATUS_BUFFER_TOO_SMALL`. Plain Text extensions and Graphic Control Extension user-input requests return `GIF_STATUS_UNSUPPORTED_FEATURE`; method 3 does so when its optional feature is disabled.

## Quick start

1. Choose one allocator backend. The supplied CMake build selects `BUILTIN` by default; use `-DGIF_MEM_BACKEND=PRIVATE`, `LIBC`, or `LVGL` when appropriate. Add `-DGIFLIB_ENABLE_DISPOSAL_METHOD_3=ON` only when Restore-to-Previous support is required.
2. Implement the three `gif_porting_open/read/close` functions for the product's byte source. The standard port allocates one independent handle for every open decoder stream.
3. Include only `gif_decoder.h` and `gif_config.h` in application code, then follow the decode lifecycle in the User Guide.

For a hosted reference build with the example:

```powershell
cmake -S . -B build/host -DGIFLIB_BUILD_EXAMPLES=ON
cmake --build build/host
```

## Documentation

- [User Guide](docs/USER_GUIDE.md) — application-facing configuration, API reference, and decode lifecycle tutorial.
- [Porting Guide](docs/PORTING_GUIDE.md) — authoritative storage/byte-source contract and platform-port examples.
- [Memory Configuration](docs/MEMORY_CONFIGURATION.md) — backend selection, pool sizing profiles, and RAM boundaries.
- [BUILTIN Pool Sizing and Fragmentation Study](docs/BUILTIN_POOL_SIZING_STUDY.md) — evidence and methodology behind the BUILTIN sizing profiles.
- [Pool sizing calculator](tools/estimate_builtin_pool.py) — a planning utility; validate the final target and actual GIF corpus separately.
- [Host Validation](docs/HOST_VALIDATION.md) — local corpus provenance, compatibility matrix, host instrumentation, and fuzzing workflow/evidence.
- [Embedded player example](examples/embedded_player/README.md) — a small, platform-independent reference application.
- [Project TODO list](docs/TODO_LIST.md) — planned work only, not feature promises.

## License

Project-original code is released under the [MIT License](LICENSE). giflib-derived code retains its upstream notices; the BUILTIN TLSF implementation retains Matthew Conte's BSD-3-Clause notice. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [vendor/giflib/COPYING](vendor/giflib/COPYING).
