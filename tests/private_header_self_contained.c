/**
 * @file private_header_self_contained.c
 * @brief Compile-time check that giflib's private header is self-contained.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_lib_private.h"

/**
 * @brief Reference a private type so the compiler validates its declaration.
 *
 * @return Size of GifFilePrivateType in bytes.
 */
size_t giflib_private_header_size(void) {
    return sizeof(GifFilePrivateType);
}
