# Third-party notices and source boundary

This repository contains a substantial amount of source code derived from
giflib. The purpose of this document is to distinguish that upstream work from
code written specifically for `giflib-embedded` and to describe the changes
made during the embedded port.

The repository-level [LICENSE](LICENSE) applies to the project-original code
and modifications. It does not replace or remove any upstream copyright. The
giflib license text is retained in
[vendor/giflib/COPYING](vendor/giflib/COPYING), and available SPDX copyright
and license statements remain in the upstream-derived source files.

## giflib 6.1.3

Upstream project: <https://giflib.sourceforge.net/>

Upstream source: <https://sourceforge.net/p/giflib/code/>

License: MIT

The GIFLIB distribution is Copyright (c) 1997 Eric S. Raymond. The original
giflib code was written by Gershon Elber and was subsequently maintained and
developed by Eric S. Raymond, Toshio Kuratomi, and other contributors. Those
authors and contributors retain copyright in the code derived from giflib.

### Files substantially derived from giflib

The following files contain substantial upstream giflib implementation or API
code and must not be treated as project-original work:

- `vendor/giflib/dgif_lib.c` contains the callback-based GIF parser, record and
  extension reader, image descriptor handling, LZW setup/decompression,
  low-level scanline decode, close logic, and retained slurp-related
  implementation from giflib.
- `vendor/giflib/gifalloc.c` contains giflib color-map, extension-block, and
  saved-image allocation helpers.
- `vendor/giflib/gif_err.c` contains giflib's decoder error-to-string mapping.
- `vendor/giflib/gif_lib.h` contains the trimmed giflib data types, decoder
  declarations, error codes, extension definitions, and allocation helper
  declarations.
- `vendor/giflib/gif_lib_private.h` contains giflib's private decoder/LZW state
  and internal constants.

### Changes made to the giflib-derived code

The embedded port deliberately avoids stylistic rewrites of giflib's mature
parser and LZW decoder. Changes are limited to trimming, portability, and
correctness work:

- The encoder, command-line utilities, conversion tools, documentation build,
  upstream build system, and unrelated support modules are not included.
- Direct filename, file-descriptor, stdio, POSIX, and Windows file-opening
  paths are omitted. Input reaches the retained decoder only through giflib's
  callback entry point.
- `vendor/giflib/dgif_lib.c` no longer includes unused `stdio.h`, `fcntl.h`,
  `unistd.h`, or `io.h` platform headers.
- The callback-based decoding path and giflib LZW algorithm are retained; the
  new facade uses `DGifGetImageHeader()` and `DGifGetLine()` and does not use
  `DGifSlurp()` or `DGifGetImageDesc()`.
- Image pixel-count multiplication was changed to avoid signed overflow and to
  validate the target integer width before multiplication.
- `DGifOpen()` was changed to preserve the actual logical-screen read or
  allocation error instead of replacing it with a generic screen-descriptor
  error.
- `vendor/giflib/gif_lib_private.h` directly includes the standard integer
  header it needs, making the private header self-contained.
- A loop index type was aligned with giflib's signed color-count type to avoid
  a signed/unsigned compiler warning.
- The malformed SPDX copyright field in `vendor/giflib/gif_err.c` was corrected
  without changing the credited author.
- All giflib allocation sites now use the private `gif_mem_*` facade. The
  former OpenBSD `reallocarray` compatibility translation unit is not retained:
  its overflow and zero-size behavior is implemented once by
  `gif_mem_realloc_array()` before dispatch to the selected backend. As in the
  retained implementation, zero-size reallocarray returns `NULL` without
  releasing a non-null original allocation.
- Existing comments in giflib-derived code are intentionally retained in their
  upstream form. The project's Doxygen-compatible style applies to
  project-original files and to new comments that specifically document port
  changes; it is not used to rewrite upstream commentary for appearance.
The resulting files remain derived works of giflib even where individual lines
were modified for the port.

## TLSF allocator lineage

The BUILTIN memory backend contains a modified TLSF 3.1 implementation in
`vendor/tlsf/gif_tlsf.c` and `vendor/tlsf/gif_tlsf.h`. These files are derived
works, not project-original files, and retain Matthew Conte's full BSD-3-Clause
copyright and license notice in `gif_tlsf.h`.

- Original upstream: Matthew Conte's TLSF repository,
  <https://github.com/mattconte/tlsf>, commit
  `deff9ab509341f264addbd3c8ada533678591905` (`Update tlsf.c`, 2020-03-29).
- Adopted implementation baseline: LVGL's improved TLSF core from
  <https://github.com/lvgl/lvgl>, commit
  `940c86ae3ade38de8c28b9096a87848d82c6ac36` (2026-08-12),
  `src/stdlib/builtin/lv_tlsf.c` and `lv_tlsf.h`.
- LVGL attribution is lineage, not authorship: the allocator remains derived
  from Matthew Conte's TLSF and is not represented as an original LVGL work.

### Derived TLSF changes in this repository

The TLSF algorithm was taken from the LVGL-improved baseline and modified only
to fit this library's fixed-pool boundary:

- all exported TLSF identifiers are renamed to `gif_tlsf_*`, preventing symbol
  collisions when an application also links LVGL;
- LVGL headers, configuration, logging, assertions, string wrappers, global
  state, linked lists, OS/mutex integration, monitoring, and dynamic multi-pool
  management are removed;
- the LVGL bounded-pool first-level-index optimization is retained and bound to
  `GIF_MEM_POOL_SIZE`, reducing allocator metadata for the configured fixed
  pool;
- LVGL's alignment-overflow protection, oversized `realloc()` protection, and
  pool-size boundary fixes are retained;
- the project adds safe null/size checks around construction and emits no
  allocator log output from the library runtime.

`src/memory/gif_mem_builtin.c` and `.h` are project-original MIT-licensed
single-pool wrappers. They select, align, initialize, and use the derived TLSF
core; they do not copy LVGL's memory manager. `src/memory/gif_mem.c` and `.h`
are project-original MIT-licensed backend-neutral semantic wrappers.

The complete fixed-source and delta record is retained in
[docs/TLSF_LINEAGE.md](docs/TLSF_LINEAGE.md).

## Code written for giflib-embedded

The following files were written for this project and are not copied from
giflib:

- `include/gif_decoder.h` defines the opaque, platform-independent public API,
  source-selection config, output-surface types, frame information, and public
  status model. `include/gif_config.h` is the project-original centralized
  compile-time configuration entry point.
- `src/gif_decoder.c` implements the fixed application facade and dispatch
  between the platform port and hidden decoder. `src/gif_decoder_core.c` and
  `src/gif_decoder_core.h` implement the private short-read bridge, error
  mapping, output validation, RGB888/BGR888 streaming compositor, frame-scoped
  GCE delay and transparency state, and explicit unsupported-feature handling.
- `port/gif_porting.c` and `port/gif_porting.h` define the project-original
  platform integration skeleton and its stable open/read/close contract. They
  contain no source copied from a filesystem, device driver, or storage
  implementation.
- `port/gif_mem_private.c` and `port/gif_mem_private.h` are project-original
  templates for the PRIVATE allocator provider. They intentionally contain no
  allocator fallback or target implementation.
- `CMakeLists.txt` defines this project's host, cross-compilation,
  installation, example, and test builds.
- `tests/private_header_self_contained.c`,
  `tests/public_header_self_contained.c`,
  `tests/core_header_self_contained.c`,
  `tests/porting_header_self_contained.c`,
  `tests/memory_header_self_contained.c`, `tests/test_allocator.c`,
  `tests/test_memory.c`, `tests/test_memory_builtin.c`, `tests/test_decoder.c`,
  `tests/test_porting.c`, `tests/test_porting.h`, and `tests/test_regression.c`
  are this project's portability and regression tests. Their small in-memory
  GIF byte fixtures were created for these tests.
- `examples/embedded_player/main.c`, `gif_porting.c`, `memory_source.h`,
  `example_platform.h`, `hosted_platform.c`, and `README.md` form a
  project-original embedded player reference application. The hosted backend
  uses only the C standard library and does not incorporate a platform SDK or
  display-library source.
- `examples/embedded_player/assets/device_boot.gif` is a project-original
  128 x 64 device-status animation created for this repository; it is not
  downloaded, copied, or adapted from external media and contains no external
  artwork, icon, logo, font, or brand asset. Its frames were generated locally
  from project-original geometric shapes and colors. `demo_animation.c` and
  `demo_animation.h` are its mechanically generated embedded C representation
  and declarations.
- `README.md`, `docs/PORTING_GUIDE.md`, `docs/TLSF_LINEAGE.md`,
  `docs/COMMENTING_STYLE.md`, `.gitignore`, `LICENSE`, and this notice were
  written or selected for this repository, except where the TLSF lineage record
  quotes third-party provenance and license facts.

Project-original work is Copyright (c) 2026 Steven Zhu and is licensed under
the MIT License in [LICENSE](LICENSE).

## External authoring tools used but not bundled

### Pillow 11.0.0

A one-off local script used Pillow to draw the project-original geometric
animation and encode `examples/embedded_player/assets/device_boot.gif`. No
Pillow source, binary, generated wrapper, or authoring script is included in
this repository. Pillow is not used by CMake, the decoder library, the example
build, tests, or target applications. The committed GIF contains original
project artwork rather than Pillow sample media or other third-party content.

Pillow is an authoring tool only, not a distributed project component. The
official Pillow documentation states that Pillow is licensed under the
MIT-CMU License: <https://pillow.readthedocs.io/en/stable/about.html#license>.

## External integrations referenced but not bundled

### FatFs

The Porting Guide refers to the FatFs API and contains project-original adapter
code showing how `gif_porting_open()`, `gif_porting_read()`, and
`gif_porting_close()` can call `f_open()`, `f_read()`, and `f_close()`. This
repository does not contain a FatFs source file, header, binary, sample project,
or copied FatFs implementation code. Its library, tests, and embedded player
example do not link to FatFs.

FatFs is therefore an optional external integration rather than a bundled
third-party component of this repository. Users who supply FatFs in a parent
project are responsible for the license and attribution requirements of the
specific FatFs version they redistribute. FatFs is developed by ChaN and is
distributed under its own permissive terms. The upstream project and official
license note are available at <https://elm-chan.org/fsw/ff/> and
<https://elm-chan.org/fsw/ff/doc/appnote.html#license>.

## No endorsement

The names of giflib, its maintainers, and its contributors are used only to
describe provenance and attribution. This modified embedded port is not an
official giflib release and is not endorsed by the upstream authors.
