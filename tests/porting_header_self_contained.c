/**
 * @file porting_header_self_contained.c
 * @brief Compile-time check that the porting contract is self-contained.
 */

#include "gif_porting.h"

/**
 * @brief Reference a porting type to validate its declaration.
 *
 * @return Size of GifPortingHandle in bytes.
 */
size_t gif_porting_header_size(void) {
    return sizeof(GifPortingHandle);
}
