#include "imageio/imageio_x3f.h"
#include "develop/imageop.h"
#include "external/x3f-rust/ffi/ansel_x3f.h"
#include "metadata/exif.h"
#include "system/macros.h"
#include "system/mem_alloc.h"

#include <glib.h>
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

  ansel_x3f_image *decoded = NULL;
  ansel_x3f_info info = { 0 };
  char message[256] = { 0 };
  const int result = ansel_x3f_decode((const uint8_t *)g_mapped_file_get_contents(file),
                                      g_mapped_file_get_length(file), &decoded, &info, message, sizeof(message));
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
    else if(ansel_x3f_copy_rgba(decoded, pixels, (size_t)info.width * info.height * 4) != 0)
      status = DT_IMAGEIO_FILE_CORRUPTED;
  }
  ansel_x3f_free(decoded);
  return status;
}
