#pragma once

#include "common/image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read camera-native DNG white balance and the calibrated D65 XYZ-to-camera matrix.
 * Returns an image-I/O result. Missing or unsupported calibration is an error rather than
 * permission to substitute a guessed camera profile. Source profile rendering remains deferred.
 */
dt_imageio_retval_t dt_dng_color_read(dt_image_t *img, const char *filename);

#ifdef __cplusplus
}
#endif
