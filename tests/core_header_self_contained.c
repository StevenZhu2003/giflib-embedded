/**
 * @file core_header_self_contained.c
 * @brief Compile-time check that the hidden core header is self-contained.
 */

#include "gif_decoder_core.h"

/** @brief Function-pointer type matching the hidden core open declaration. */
typedef GifStatus (*GifDecoderCoreOpenFunction)(GifPortingHandle,
                                                GifDecoder **,
                                                GifStreamInfo *);

/**
 * @brief Reference the hidden open declaration without supporting includes.
 *
 * @return Address of the hidden open function.
 */
GifDecoderCoreOpenFunction gif_decoder_core_header_symbol(void) {
    return gif_decoder_core_open;
}
