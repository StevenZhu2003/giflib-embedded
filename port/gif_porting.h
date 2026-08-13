/**
 * @file gif_porting.h
 * @brief Stable platform contract implemented only by gif_porting.c.
 *
 * Ordinary applications must not call this interface directly. Platform
 * maintainers implement the declared operations in `gif_porting.c`; this
 * header is platform-neutral and must not require target-specific editing.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_PORTING_H
#define GIF_PORTING_H

#include <stddef.h>
#include <stdint.h>

/** @brief Opaque token representing one open platform byte source. */
typedef void *GifPortingHandle;

/** @brief Results returned across the platform byte-source boundary. */
typedef enum GifPortingStatus {
    GIF_PORTING_OK = 0, /**< Operation succeeded; bytes may be present. */
    GIF_PORTING_EOF = 1, /**< Final bytes were returned or input ended. */
    GIF_PORTING_IO_ERROR = 2, /**< Source open or read operation failed. */
    GIF_PORTING_OUT_OF_MEMORY = 3 /**< Port handle allocation failed. */
} GifPortingStatus;

/**
 * @brief Open the platform resource selected by an opaque identifier.
 *
 * The port defines the meaning of @p source_identifier. A filesystem port may
 * treat it as a path string, while a memory or flash port may interpret it as
 * a descriptor. Successful calls must return a non-NULL handle.
 *
 * @param[in] source_identifier Application-selected resource identifier.
 * @param[out] out_handle       Receives the open platform source handle.
 * @return `GIF_PORTING_OK` on success, `GIF_PORTING_OUT_OF_MEMORY` when a
 *         dynamic port handle cannot be allocated, or `GIF_PORTING_IO_ERROR`
 *         for another open failure.
 */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle);

/**
 * @brief Read sequential bytes from an open platform source.
 *
 * A successful short read is legal. `GIF_PORTING_EOF` and
 * `GIF_PORTING_IO_ERROR` may accompany final valid bytes. The implementation
 * must never report more than @p requested_bytes.
 *
 * @param[in] handle            Handle returned by gif_porting_open().
 * @param[out] destination      Buffer that receives source bytes.
 * @param[in] requested_bytes   Maximum number of bytes to return.
 * @param[out] actual_bytes     Number of bytes placed in @p destination.
 * @return Status associated with the read operation.
 */
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes);

/**
 * @brief Close one platform byte source and release its port-owned resources.
 *
 * @param[in,out] handle Handle returned by gif_porting_open(), or `NULL`.
 */
void gif_porting_close(GifPortingHandle handle);

#endif /* GIF_PORTING_H */
