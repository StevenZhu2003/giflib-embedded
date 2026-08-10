# Third-party notices and source boundary

This repository contains a substantial amount of source code derived from
giflib. The purpose of this document is to distinguish that upstream work from
code written specifically for `giflib-embedded` and to describe the changes
made during the embedded port.

The repository-level [LICENSE](LICENSE) applies to the project-original code
and modifications. It does not replace or remove any upstream copyright. The
giflib license text is retained in [COPYING](COPYING), and available SPDX
copyright and license statements remain in the upstream-derived source files.

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

- `dgif_lib.c` contains the callback-based GIF parser, record and extension
  reader, image descriptor handling, LZW setup/decompression, low-level scanline
  decode, close logic, and retained slurp-related implementation from giflib.
- `gifalloc.c` contains giflib color-map, extension-block, and saved-image
  allocation helpers.
- `gif_err.c` contains giflib's decoder error-to-string mapping.
- `gif_lib.h` contains the trimmed giflib data types, decoder declarations,
  error codes, extension definitions, and allocation helper declarations.
- `gif_lib_private.h` contains giflib's private decoder/LZW state and internal
  constants.
- `openbsd-reallocarray.c` is the reallocarray compatibility implementation
  shipped by giflib and attributed upstream to Otto Moerbeek.

### Changes made to the giflib-derived code

The embedded port deliberately avoids stylistic rewrites of giflib's mature
parser and LZW decoder. Changes are limited to trimming, portability, and
correctness work:

- The encoder, command-line utilities, conversion tools, documentation build,
  upstream build system, and unrelated support modules are not included.
- Direct filename, file-descriptor, stdio, POSIX, and Windows file-opening
  paths are omitted. Input reaches the retained decoder only through giflib's
  callback entry point.
- `dgif_lib.c` no longer includes unused `stdio.h`, `fcntl.h`, `unistd.h`, or
  `io.h` platform headers.
- The callback-based decoding path and giflib LZW algorithm are retained; the
  new facade uses `DGifGetImageHeader()` and `DGifGetLine()` and does not use
  `DGifSlurp()` or `DGifGetImageDesc()`.
- Image pixel-count multiplication was changed to avoid signed overflow and to
  validate the target integer width before multiplication.
- `DGifOpen()` was changed to preserve the actual logical-screen read or
  allocation error instead of replacing it with a generic screen-descriptor
  error.
- `gif_lib_private.h` directly includes the standard integer header it needs,
  making the private header self-contained.
- A loop index type was aligned with giflib's signed color-count type to avoid
  a signed/unsigned compiler warning.
- The malformed SPDX copyright field in `gif_err.c` was corrected without
  changing the credited author.
- The retained `openbsd-reallocarray.c` variant removes `errno` and
  `sys/types.h` dependencies for the embedded core. It still performs the
  upstream overflow check and returns `NULL` for overflow or zero-size input,
  but it does not set `errno`.

The resulting files remain derived works of giflib even where individual lines
were modified for the port.

## Code written for giflib-embedded

The following files were written for this project and are not copied from
giflib:

- `include/gif_decoder.h` defines the opaque, platform-independent public API,
  byte-source contract, output-surface types, frame information, and public
  status model.
- `gif_decoder.c` implements the public facade, short-read bridge, error
  mapping, output validation, RGB888/BGR888 streaming compositor, and explicit
  unsupported-feature handling. It calls the retained giflib decoder but is
  not an upstream giflib source file.
- `CMakeLists.txt` defines this project's host, cross-compilation, test, and
  installation build.
- `tests/private_header_self_contained.c`,
  `tests/public_header_self_contained.c`, `tests/test_allocator.c`,
  `tests/test_decoder.c`, and `tests/test_regression.c` are this project's
  portability and regression tests. Their small in-memory GIF byte fixtures
  were created for these tests.
- `README.md`, `.gitignore`, `LICENSE`, and this notice were written or selected
  for this repository.

Project-original work is Copyright (c) 2026 Steven Zhu and is licensed under
the MIT License in [LICENSE](LICENSE).

## No endorsement

The names of giflib, its maintainers, and its contributors are used only to
describe provenance and attribution. This modified embedded port is not an
official giflib release and is not endorsed by the upstream authors.
