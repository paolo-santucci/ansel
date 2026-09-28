#pragma once

#include "imageio/imageio_core.h"

/** Decode a capability-probed camera-native LinearRaw image.
 * Unsupported formats/features permit fallback; DECODE_FAILED and CACHE_FULL stop it.
 * The caller owns the image write lock and the optional mipmap allocation.
 */
dt_imageio_retval_t dt_imageio_open_rawdinal(dt_image_t *img, const char *filename,
                                           dt_mipmap_buffer_t *mbuf);
