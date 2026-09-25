#ifndef DT_IMAGEIO_X3F_H
#define DT_IMAGEIO_X3F_H

#include "imageio/imageio_core.h"

/** Experimental, fixed-as-shot sd Quattro conversion to scene-linear sRGB.
 * The result uses Ansel's RGB/HDR path, without Bayer demosaicing or a second
 * raw white balance. Sensor-domain white-balance editing is not implemented. */
dt_imageio_retval_t dt_imageio_open_x3f(dt_image_t *img, const char *filename, dt_mipmap_buffer_t *mbuf);

#endif
