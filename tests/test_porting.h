/**
 * @file test_porting.h
 * @brief Shared in-memory source state for the host test port.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIFLIB_TEST_PORTING_H
#define GIFLIB_TEST_PORTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief In-memory byte stream used to exercise the porting contract. */
typedef struct MemorySource {
    const uint8_t *data;       /**< Immutable source bytes. */
    size_t size;               /**< Total number of source bytes. */
    size_t offset;             /**< Offset of the next unread byte. */
    size_t max_chunk;          /**< Maximum bytes returned per read. */
    size_t error_offset;       /**< Offset at which an I/O error is raised. */
    size_t zero_ok_offset;     /**< Offset at which one zero-byte OK is raised. */
    size_t read_calls;         /**< Number of read invocations. */
    size_t close_calls;        /**< Number of close invocations. */
    bool inject_open_error;    /**< Whether source open must fail. */
    bool inject_error;         /**< Whether fault injection is enabled. */
    bool inject_zero_ok;       /**< Whether one non-progressing OK is enabled. */
    bool zero_ok_emitted;      /**< Whether the configured zero-byte OK was returned. */
    bool eof_with_final_bytes; /**< Whether the last data also reports EOF. */
} MemorySource;

#endif /* GIFLIB_TEST_PORTING_H */
