#include "imageio/imageio_x3f.h"
#include "develop/imageop.h"
#include <rawdinal.h>
#include "metadata/exif.h"
#include "system/macros.h"
#include "system/mem_alloc.h"

#include <glib.h>
#include <math.h>
#include <string.h>

dt_imageio_retval_t dt_imageio_open_x3f(dt_image_t *img, const char *filename, dt_mipmap_buffer_t *mbuf)
{
  GError *error = NULL;
  GMappedFile *file = g_mapped_file_new(filename, FALSE, &error);
  if(IS_NULL_PTR(file))
  {
    dt_print(DT_DEBUG_ALWAYS, "[x3f] %s\n", !IS_NULL_PTR(error) ? error->message : "cannot map input");
    g_clear_error(&error);
    return DT_IMAGEIO_FILE_CORRUPTED;
  }

  rawdinal_image *decoded = NULL;
  rawdinal_info info = { 0 };
  char message[256] = { 0 };
  const int result = rawdinal_decode_with_clipping_v1((const uint8_t *)g_mapped_file_get_contents(file),
                                                       g_mapped_file_get_length(file), &decoded, &info,
                                                       message, sizeof(message));
  g_mapped_file_unref(file);
  if(result != 0)
  {
    dt_print(DT_DEBUG_ALWAYS, "[x3f] %s\n", message);
    return DT_IMAGEIO_FILE_CORRUPTED;
  }

  if(!img->exif_inited && info.exif_size > 0)
    dt_exif_read_from_blob(img, (uint8_t *)info.exif, (int)info.exif_size);
  g_strlcpy(img->exif_maker, "SIGMA", sizeof(img->exif_maker));
  g_strlcpy(img->exif_model, "sd Quattro", sizeof(img->exif_model));
  dt_image_refresh_makermodel(img);
  img->width = info.width;
  img->height = info.height;
  img->crop_x = img->crop_y = img->crop_width = img->crop_height = 0;
  img->dsc.channels = 4;
  img->dsc.datatype = TYPE_FLOAT;
  img->dsc.bpp = 4 * sizeof(float);
  img->dsc.cst = IOP_CS_RGB;
  img->dsc.filters = 0;
  img->flags &= ~(DT_IMAGE_RAW | DT_IMAGE_S_RAW | DT_IMAGE_LDR | DT_IMAGE_4BAYER);
  img->flags |= DT_IMAGE_HDR;
  img->loader = LOADER_X3F;
  img->raw_black_level = 0;
  img->raw_white_point = 1;
  for(int channel = 0; channel < 4; channel++)
  {
    img->wb_coeffs[channel] = 1.0f;
    img->raw_black_level_separate[channel] = 0;
    img->dsc.processed_maximum[channel] = 1.0f;
  }
  dt_free(img->profile);
  img->profile = NULL;
  img->profile_size = 0;
  const float xyz_to_linear_srgb[9] = { 3.2404542f, -1.5371385f, -0.4985314f, -0.9692660f, 1.8760108f,
                                        0.0415560f, 0.0556434f,  -0.2040259f, 1.0572252f };
  memcpy(img->d65_color_matrix, xyz_to_linear_srgb, sizeof(xyz_to_linear_srgb));

  dt_imageio_retval_t status = DT_IMAGEIO_OK;
  if(!IS_NULL_PTR(mbuf))
  {
    float *pixels = dt_mipmap_cache_alloc(mbuf, img);
    if(IS_NULL_PTR(pixels))
      status = DT_IMAGEIO_CACHE_FULL;
    else if(rawdinal_copy_rgba(decoded, pixels, (size_t)info.width * info.height * 4) != 0)
      status = DT_IMAGEIO_FILE_CORRUPTED;
    else
    {
      rawdinal_clipping_v1_info clipping = { 0 };
      if(rawdinal_get_clipping_v1(decoded, &clipping) != RAWDINAL_STATUS_OK
         || clipping.version != RAWDINAL_CLIPPING_V1_INFO_VERSION
         || clipping.width != info.width || clipping.height != info.height
         || clipping.width == 0 || clipping.height == 0
         || clipping.stride_bytes < clipping.width || IS_NULL_PTR(clipping.data)
         || clipping.height - 1 > (SIZE_MAX - clipping.width) / clipping.stride_bytes
         || clipping.byte_count < (clipping.height - 1) * clipping.stride_bytes + clipping.width)
        status = DT_IMAGEIO_FILE_CORRUPTED;
      for(size_t layer = 0; status == DT_IMAGEIO_OK && layer < 3; layer++)
      {
        const rawdinal_clipping_v1_plane *const plane = &clipping.planes[layer];
        if(plane->identity != layer
           || (plane->threshold_provenance != RAWDINAL_CLIPPING_V1_THRESHOLD_ESTIMATED_ENCODED_MAXIMUM
               && plane->threshold_provenance != RAWDINAL_CLIPPING_V1_THRESHOLD_CALIBRATED))
          status = DT_IMAGEIO_FILE_CORRUPTED;
      }
      if(status == DT_IMAGEIO_OK)
      {
        const float clipped = nextafterf(1.0f, INFINITY);
        __OMP_PARALLEL_FOR__()
        for(size_t row = 0; row < clipping.height; row++)
          for(size_t col = 0; col < clipping.width; col++)
            if(clipping.data[row * clipping.stride_bytes + col])
            {
              float *const pixel = pixels + (row * clipping.width + col) * 4;
              pixel[0] = pixel[1] = pixel[2] = clipped;
            }
      }
    }
  }
  rawdinal_free(decoded);
  return status;
}
