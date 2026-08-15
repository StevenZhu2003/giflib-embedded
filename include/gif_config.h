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

/** @brief Select the C library heap allocator. */
#define GIF_MEM_USE_LIBC 3

/** @brief Select the allocator exported by an initialized LVGL instance. */
#define GIF_MEM_USE_LVGL 4

/** @brief Select the memory backend when no application override is supplied. */
#ifndef GIF_MEM_BACKEND
#define GIF_MEM_BACKEND GIF_MEM_USE_BUILTIN
#endif

#if GIF_MEM_BACKEND != GIF_MEM_USE_BUILTIN && \
    GIF_MEM_BACKEND != GIF_MEM_USE_PRIVATE && \
    GIF_MEM_BACKEND != GIF_MEM_USE_LIBC && \
    GIF_MEM_BACKEND != GIF_MEM_USE_LVGL
#error "GIF_MEM_BACKEND must select GIF_MEM_USE_BUILTIN, GIF_MEM_USE_PRIVATE, GIF_MEM_USE_LIBC, or GIF_MEM_USE_LVGL"
#endif

/**
 * @brief Bytes managed by the BUILTIN allocator's single fixed pool.
 *
 * 48 KiB is a build default, not a universal production recommendation. Size
 * the pool from the declared product envelope and validation evidence. The
 * framebuffer is always application-owned and is not allocated from this pool.
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

/**
 * @brief Enable GIF disposal method 3 (Restore to Previous) support.
 *
 * The default preserves the smallest decoder and the existing policy: a GIF
 * requesting disposal method 3 returns GIF_STATUS_UNSUPPORTED_FEATURE. Set to
 * 1 to include the rectangle snapshot path required by the feature. Enabled
 * BUILTIN builds must budget for the largest pending image-rectangle snapshot;
 * the caller-owned framebuffer remains separate.
 */
#ifndef GIF_ENABLE_DISPOSAL_METHOD_3
#define GIF_ENABLE_DISPOSAL_METHOD_3 0
#endif

#if GIF_ENABLE_DISPOSAL_METHOD_3 != 0 && GIF_ENABLE_DISPOSAL_METHOD_3 != 1
#error "GIF_ENABLE_DISPOSAL_METHOD_3 must be 0 or 1"
#endif

/**
 * @brief Enable synchronous FIFO-backed burst reads from the platform source.
 *
 * The default preserves direct forward-only reads through gif_porting_read().
 * Set to 1 to select the optional internal FIFO adapter once that feature is
 * included by the library build. The FIFO belongs to the decoder allocator
 * domain; it is never supplied by the application.
 */
#ifndef GIF_ENABLE_BURST_READ
#define GIF_ENABLE_BURST_READ 0
#endif

#if GIF_ENABLE_BURST_READ != 0 && GIF_ENABLE_BURST_READ != 1
#error "GIF_ENABLE_BURST_READ must be 0 or 1"
#endif

/** @brief Bytes in one enabled decoder's private burst-read FIFO. */
#ifndef GIF_BURST_READ_FIFO_SIZE
#define GIF_BURST_READ_FIFO_SIZE 1024U
#endif

/** @brief Refill threshold for one enabled decoder's burst-read FIFO. */
#ifndef GIF_BURST_READ_LOW_WATERMARK
#define GIF_BURST_READ_LOW_WATERMARK 256U
#endif

#if GIF_ENABLE_BURST_READ
#if GIF_BURST_READ_FIFO_SIZE == 0U
#error "GIF_BURST_READ_FIFO_SIZE must be greater than zero when BURST_READ is enabled"
#endif

#if GIF_BURST_READ_LOW_WATERMARK >= GIF_BURST_READ_FIFO_SIZE
#error "GIF_BURST_READ_LOW_WATERMARK must be smaller than GIF_BURST_READ_FIFO_SIZE"
#endif
#endif

#endif /* GIF_CONFIG_H */
