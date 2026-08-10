/**
 * @file memory_source.h
 * @brief Application resource descriptor shared with the example memory port.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_EXAMPLE_MEMORY_SOURCE_H
#define GIF_EXAMPLE_MEMORY_SOURCE_H

#include <stddef.h>
#include <stdint.h>

/** @brief Immutable GIF bytes selected by the example application. */
typedef struct GifMemorySource {
    const uint8_t *data; /**< First byte of the encoded GIF stream. */
    size_t size;         /**< Number of accessible bytes at @p data. */
} GifMemorySource;

#endif /* GIF_EXAMPLE_MEMORY_SOURCE_H */
