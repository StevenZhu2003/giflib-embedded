/**
 * @file gif_config.h
 * @brief Library-wide compile-time configuration for giflib-embedded.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 *
 * Applications may override these macros through their compiler definitions
 * or by supplying an edited installed configuration header. Configuration is
 * intentionally centralized here so future library options do not leak into
 * giflib-derived parser sources.
 */

#ifndef GIF_CONFIG_H
#define GIF_CONFIG_H

/** @brief Select the library-owned fixed-pool allocator. */
#define GIF_MEM_USE_BUILTIN 1

/** @brief Select the application-provided allocator primitives. */
#define GIF_MEM_USE_PRIVATE 2

/** @brief Select the memory backend when no application override is supplied. */
#ifndef GIF_MEM_BACKEND
#define GIF_MEM_BACKEND GIF_MEM_USE_BUILTIN
#endif

#if GIF_MEM_BACKEND != GIF_MEM_USE_BUILTIN && \
    GIF_MEM_BACKEND != GIF_MEM_USE_PRIVATE
#error "GIF_MEM_BACKEND must be GIF_MEM_USE_BUILTIN or GIF_MEM_USE_PRIVATE"
#endif

/**
 * @brief Bytes managed by the BUILTIN allocator's single fixed pool.
 *
 * 48 KiB is the initial embedded recommendation. The framebuffer is always
 * application-owned and is not allocated from this pool.
 */
#ifndef GIF_MEM_POOL_SIZE
#define GIF_MEM_POOL_SIZE (48U * 1024U)
#endif

/**
 * @brief Required alignment of the BUILTIN allocator pool in bytes.
 *
 * The default supports 64-bit objects while remaining suitable for common
 * 32-bit embedded targets. The value must be a power of two and at least 8.
 */
#ifndef GIF_MEM_POOL_ALIGNMENT
#define GIF_MEM_POOL_ALIGNMENT 8U
#endif

#if GIF_MEM_POOL_SIZE == 0U
#error "GIF_MEM_POOL_SIZE must be greater than zero"
#endif

#if GIF_MEM_POOL_ALIGNMENT < 8U || \
    (GIF_MEM_POOL_ALIGNMENT & (GIF_MEM_POOL_ALIGNMENT - 1U)) != 0U
#error "GIF_MEM_POOL_ALIGNMENT must be a power of two and at least 8"
#endif

#endif /* GIF_CONFIG_H */
