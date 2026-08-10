/**
 * @file gif_decoder_core.h
 * @brief Private contract between the public facade and decoder core.
 *
 * This header is an implementation detail. Applications and platform ports
 * must not include it; use `gif_decoder.h` and implement `gif_porting.c`.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_DECODER_CORE_H
#define GIF_DECODER_CORE_H

#include "gif_decoder.h"
#include "gif_porting.h"

/**
 * @brief Open the decoder core over an already-open porting handle.
 *
 * On success, the decoder owns @p source_handle and closes it from
 * `gif_decoder_core_close()`. On failure, ownership remains with the caller.
 *
 * @param[in] source_handle   Handle returned by gif_porting_open().
 * @param[out] out_decoder    Receives the allocated decoder instance.
 * @param[out] out_stream     Receives logical-screen metadata.
 * @return Public decoder status for the open operation.
 */
GifStatus gif_decoder_core_open(GifPortingHandle source_handle,
                                GifDecoder **out_decoder,
                                GifStreamInfo *out_stream);

/**
 * @brief Bind caller-owned output storage to the hidden decoder core.
 *
 * @param[in,out] decoder Decoder instance returned by the core open function.
 * @param[in] surface     Output surface to validate and copy.
 * @return Public decoder status for the bind operation.
 */
GifStatus gif_decoder_core_bind_output(GifDecoder *decoder,
                                       const GifOutputSurface *surface);

/**
 * @brief Decode the next frame through the hidden streaming core.
 *
 * @param[in,out] decoder Decoder with a valid output surface.
 * @param[out] out_frame  Receives decoded-frame metadata.
 * @return Public decoder status for the frame operation.
 */
GifStatus gif_decoder_core_next_frame(GifDecoder *decoder,
                                      GifFrameInfo *out_frame);

/**
 * @brief Release the decoder core and its owned source context.
 *
 * @param[in,out] decoder Decoder to release, or `NULL`.
 */
void gif_decoder_core_close(GifDecoder *decoder);

#endif /* GIF_DECODER_CORE_H */
