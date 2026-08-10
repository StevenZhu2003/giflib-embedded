# Third-party notices

This project contains source code derived from third-party open-source
projects. The notices below are provided for attribution and do not replace
the license text in [COPYING](COPYING) or the copyright notices retained in
individual source files.

## giflib 6.1.3

Upstream project: <https://giflib.sourceforge.net/>

Upstream source: <https://sourceforge.net/p/giflib/code/>

Files derived from giflib include:

- `dgif_lib.c`
- `gifalloc.c`
- `gif_err.c`
- `gif_lib.h`
- `gif_lib_private.h`
- `openbsd-reallocarray.c`

License: MIT

The GIFLIB distribution is Copyright (c) 1997 Eric S. Raymond.

giflib was originally written by Gershon Elber and has been maintained and
developed by Eric S. Raymond, Toshio Kuratomi, and other contributors. The
upstream-derived files retain their SPDX identifiers and available copyright
statements. The giflib MIT license text is preserved verbatim in
[COPYING](COPYING).

The decoder in this repository is a modified, decoder-only distribution. It
removes the encoder, utilities, direct file-opening helpers, and upstream build
system, and adds embedded portability and correctness changes documented in
the project README.

## `openbsd-reallocarray.c`

Copyright (C) 2008 Otto Moerbeek <otto@drijf.net>

License identifier in the giflib upstream file: MIT

This compatibility implementation is distributed as part of giflib and keeps
the upstream SPDX copyright and license statements in the source file.
