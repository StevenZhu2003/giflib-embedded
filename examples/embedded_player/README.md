# Embedded GIF player example

This example is a portable reference application for a small embedded display, not a decoder test. It plays a project-original 128 x 64 device-status animation stored in read-only application memory, reuses a 24 KiB RGB888 framebuffer, presents each completed canvas to a display boundary, and applies each frame delay in application code.

```text
assets/device_boot.gif
    -> demo_animation.c                  application-owned encoded resource
    -> gif_porting.c                     sequential memory byte source
    -> gif_decoder_open()/next_frame()   decoder library
    -> example_framebuffer               caller-owned 128 x 64 RGB888 canvas
    -> platform display                  completed-frame transfer
    -> platform delay                    application playback policy
```

The animation and its C byte representation were created for this project and are not copied from external media. No platform SDK, filesystem, display library, or other third-party component is bundled or required.

The example does not impose a disposal-method configuration. It links the selected library build: the default remains method-3-trimmed, while a build configured with `-DGIFLIB_ENABLE_DISPOSAL_METHOD_3=ON` can also play a resource that uses Restore to Previous. `main.c` reserves a separate application-owned static snapshot sized for its maximum canvas, so the enabled build can accept a full-canvas method-3 rectangle without consuming the decoder allocator pool. The framebuffer remains application-owned in either configuration.

## Animation asset origin and license

`assets/device_boot.gif` was created locally and specifically for this repository. It was not downloaded from the Internet and does not contain or adapt an external photograph, illustration, icon, logo, font, or other media asset. Its frames were drawn programmatically from project-original geometric shapes and a project-selected color palette, then encoded as a 128 x 64 GIF.

A one-off local authoring script used Pillow 11.0.0 to draw and encode the animation. Neither that script nor Pillow is included in this repository, and Pillow is not a build, link, run-time, or target dependency. The [official Pillow documentation](https://pillow.readthedocs.io/en/stable/about.html#license) identifies it as MIT-CMU licensed.

The animation content is project-original work, Copyright (c) 2026 Steven Zhu, and is distributed under the repository's [MIT License](../../LICENSE). `demo_animation.c` is a byte-for-byte C representation of the same GIF rather than a separate external asset. The provenance boundary is also recorded in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## Application structure

- `main.c` owns the player lifecycle, fixed framebuffer, frame loop, display calls, and delay policy.
- `demo_animation.c` and `demo_animation.h` embed the real GIF asset as read-only C data. `assets/device_boot.gif` is the corresponding source asset.
- `memory_source.h` is the application resource descriptor passed through `GifDecoderConfig.source_identifier`.
- `gif_porting.c` adapts that descriptor to the library's forward-only byte source contract. It allocates one port-owned cursor per open decoder through the standard porting-memory bridge and contains no display or timing work.
- `example_platform.h` is the small display, time, and diagnostic boundary used by the player.
- `hosted_platform.c` is a standard-C reference backend. It writes completed frames as PPM images and uses `clock()` for the application delay.

The hosted backend's output-only `fopen()` calls are not part of the GIF input path, porting layer, or decoder library. An embedded build replaces this file, so the target can transfer the framebuffer directly to its display and wait with a hardware timer, scheduler, or event loop.

## Build and run on a host

From the repository root:

```sh
cmake -S . -B build/host/example \
  -DGIFLIB_BUILD_TESTS=OFF \
  -DGIFLIB_BUILD_EXAMPLES=ON
cmake --build build/host/example
./build/host/example/gif_embedded_player_example
```

Run the executable from the directory where you want its frame captures. It creates `gif_frame_000.ppm` through `gif_frame_011.ppm`; any standard image viewer can open these portable pixmap files. This file output is only a hosted stand-in for a physical display transfer.

## Move the example to a target

1. Keep the application flow in `main.c`, or fold `application_play_gif()` into the target's application task.
2. Implement the functions from `example_platform.h` in a target-owned source file. Present the supplied RGB888 framebuffer, implement the delay using the target's normal timing service, and route diagnostics to a log channel or a no-op function.
3. Keep `gif_porting.c` unchanged when GIF resources are compiled into memory. For flash, a filesystem, a network stream, or another source, implement the stable open/read/close contract in the one active porting file and retain its one-dynamic-handle-per-open ownership model. See the [Porting Guide](../../docs/PORTING_GUIDE.md).
4. Size `EXAMPLE_MAX_CANVAS_WIDTH`, `EXAMPLE_MAX_CANVAS_HEIGHT`, and the static framebuffer for the largest animation accepted by the product.
5. Replace `demo_animation.c` with product artwork or another resource descriptor. The public decoder calls and display loop do not change.

The display call receives a fully composited framebuffer. The application must not reuse or modify that memory until the display function returns, unless its target implementation explicitly copies the pixels or coordinates asynchronous DMA ownership. Playback looping, frame skipping, and minimum-delay policies also belong to the application rather than the decoder.
