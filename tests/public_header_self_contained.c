/**
 * @file public_header_self_contained.c
 * @brief Compile-time check that the public facade header is self-contained.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

/**
 * @brief Reference the public opaque type without any supporting includes.
 *
 * @return A null decoder pointer used only for compile-time validation.
 */
GifDecoder *gif_decoder_public_header_check(void) {
    return NULL;
}
