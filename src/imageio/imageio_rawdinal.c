#include "imageio/imageio_rawdinal.h"
#include "develop/imageop.h"
#include "metadata/exif.h"
#include "metadata/dng_color.h"
#include "system/macros.h"
#include "system/mem_alloc.h"
#include <rawdinal.h>
#include <limits.h>
#include <math.h>

dt_imageio_retval_t dt_imageio_open_rawdinal(dt_image_t *img, const char *filename,
                                           dt_mipmap_buffer_t *mbuf)
{
  GError *error = NULL;
  GMappedFile *file = g_mapped_file_new(filename, FALSE, &error);
  if(IS_NULL_PTR(file))
  {
    dt_print(DT_DEBUG_IMAGEIO, "[rawdinal] %s\n", error->message);
    g_clear_error(&error);
    return DT_IMAGEIO_DECODE_FAILED;
  }

  rawdinal_raw_v1_image *decoded = NULL;
  rawdinal_raw_v1_probe_info probe = { 0 };
  rawdinal_raw_v1_capabilities capabilities = { 0 };
  rawdinal_raw_v1_info info = { 0 };
  char message[256] = { 0 };
  dt_imageio_retval_t result = DT_IMAGEIO_DECODE_FAILED;
  const uint8_t *input = (const uint8_t *)g_mapped_file_get_contents(file);
  const size_t length = g_mapped_file_get_length(file);
  int status = rawdinal_raw_v1_probe(input, length, &probe, message, sizeof(message));
  if(status == RAWDINAL_STATUS_UNSUPPORTED || status == RAWDINAL_STATUS_NOT_RECOGNIZED)
    result = DT_IMAGEIO_UNSUPPORTED_FEATURE;
  if(status != RAWDINAL_STATUS_OK) goto cleanup;
  if(probe.version != RAWDINAL_RAW_V1_PROBE_INFO_VERSION) goto cleanup;
  if(probe.classification == RAWDINAL_RAW_V1_PROBE_NOT_RECOGNIZED
     || probe.classification == RAWDINAL_RAW_V1_PROBE_RECOGNIZED_UNSUPPORTED)
  {
    result = DT_IMAGEIO_UNSUPPORTED_FORMAT;
    goto cleanup;
  }
  if(probe.classification != RAWDINAL_RAW_V1_PROBE_SUPPORTED
     || probe.container != RAWDINAL_RAW_V1_CONTAINER_DNG) goto cleanup;
  status = rawdinal_raw_v1_get_capabilities(&capabilities);
  if(status != RAWDINAL_STATUS_OK || capabilities.version != RAWDINAL_RAW_V1_CAPABILITIES_VERSION)
    goto cleanup;
  const uint32_t codec = probe.codec == RAWDINAL_RAW_V1_CODEC_LOSSLESS_JPEG
                            ? RAWDINAL_RAW_V1_CODEC_BIT_LOSSLESS_JPEG
                            : probe.codec == RAWDINAL_RAW_V1_CODEC_JPEG_XL
                                  ? RAWDINAL_RAW_V1_CODEC_BIT_JPEG_XL : 0;
  if(!(capabilities.codec_bits & codec))
  {
    result = DT_IMAGEIO_UNSUPPORTED_FEATURE;
    goto cleanup;
  }
  status = rawdinal_raw_v1_decode(input, length, &decoded, message, sizeof(message));
  if(status == RAWDINAL_STATUS_UNSUPPORTED || status == RAWDINAL_STATUS_NOT_RECOGNIZED)
    result = DT_IMAGEIO_UNSUPPORTED_FEATURE;
  if(status != RAWDINAL_STATUS_OK || IS_NULL_PTR(decoded)) goto cleanup;
  status = rawdinal_raw_v1_get_info(decoded, &info);
  if(status != RAWDINAL_STATUS_OK) goto cleanup;

  if(info.version != RAWDINAL_RAW_V1_INFO_VERSION || info.width == 0 || info.height == 0
     || info.width > INT_MAX || info.height > INT_MAX || info.channels != 3
     || IS_NULL_PTR(info.samples) || IS_NULL_PTR(info.component_ids) || info.component_id_count != 3
     || SIZE_MAX / info.width < 4 * sizeof(float)
     || info.height > SIZE_MAX / ((size_t)info.width * 4 * sizeof(float))
     || info.stride_samples < (size_t)info.width * 3
     || info.stride_samples > SIZE_MAX / sizeof(float)
     || info.stride_samples > SIZE_MAX / info.height
     || info.sample_count < (size_t)(info.height - 1) * info.stride_samples + (size_t)info.width * 3
     || info.sample_count > SIZE_MAX / sizeof(float)) goto cleanup;

  if(info.scene_linear != RAWDINAL_RAW_V1_TRUE || info.camera_native != RAWDINAL_RAW_V1_TRUE
     || info.already_demosaiced != RAWDINAL_RAW_V1_TRUE
     || (info.colorimetric_reference_present
         && info.colorimetric_reference != RAWDINAL_RAW_V1_COLORIMETRIC_REFERENCE_SCENE_REFERRED)
     || info.linearization != RAWDINAL_RAW_V1_PROCESSING_APPLIED
     || info.black_subtraction != RAWDINAL_RAW_V1_PROCESSING_APPLIED
     || info.white_normalization != RAWDINAL_RAW_V1_PROCESSING_APPLIED
     || info.white_balance != RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED
     || info.color_conversion != RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED
     || info.demosaic != RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT
     || info.opcode_list_1 != RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT
     || info.opcode_list_2 != RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT
     || info.opcode_list_3 != RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT) goto cleanup;

  const uint32_t deferred[] = { info.default_crop, info.orientation_processing, info.baseline_exposure,
                               info.profile_gain_table_map_processing, info.profile_tone_curve,
                               info.semantic_masks };
  for(size_t i = 0; i < G_N_ELEMENTS(deferred); i++)
    if(deferred[i] != RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT
       && deferred[i] != RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED) goto cleanup;

  if(info.active_area_top >= info.active_area_bottom || info.active_area_left >= info.active_area_right
     || info.active_area_bottom > info.height || info.active_area_right > info.width) goto cleanup;
  const uint32_t active_width = info.active_area_right - info.active_area_left;
  const uint32_t active_height = info.active_area_bottom - info.active_area_top;
  const uint32_t x = info.default_crop_origin_present ? info.default_crop_origin_x : 0;
  const uint32_t y = info.default_crop_origin_present ? info.default_crop_origin_y : 0;
  if(x >= active_width || y >= active_height) goto cleanup;
  const uint32_t width = info.default_crop_size_present ? info.default_crop_size_width : active_width - x;
  const uint32_t height = info.default_crop_size_present ? info.default_crop_size_height : active_height - y;
  if(width == 0 || height == 0 || width > active_width - x || height > active_height - y
     || (info.orientation_present && (info.orientation < 1 || info.orientation > 8))) goto cleanup;

  if(!img->exif_inited && dt_exif_read(img, filename)) goto cleanup;
  const dt_imageio_retval_t color_status = dt_dng_color_read(img, filename);
  if(color_status != DT_IMAGEIO_OK)
  {
    result = color_status;
    goto cleanup;
  }
  dt_exif_read_usercrop(img, filename);
  img->width = info.width;
  img->height = info.height;
  img->crop_x = info.active_area_left + x;
  img->crop_y = info.active_area_top + y;
  img->crop_width = info.width - img->crop_x - width;
  img->crop_height = info.height - img->crop_y - height;
  img->dsc.channels = 4;
  img->dsc.datatype = TYPE_FLOAT;
  img->dsc.bpp = 4 * sizeof(float);
  img->dsc.cst = IOP_CS_RAW;
  img->dsc.filters = 0;
  img->flags &= ~(DT_IMAGE_RAW | DT_IMAGE_LDR | DT_IMAGE_MOSAIC | DT_IMAGE_4BAYER);
  img->flags |= DT_IMAGE_S_RAW | DT_IMAGE_HDR;
  img->raw_black_level = 0;
  img->raw_white_point = 1;
  img->fuji_rotation_pos = 0;
  if(info.orientation_present) img->orientation = dt_image_orientation_to_flip_bits(info.orientation);
  for(int c = 0; c < 4; c++)
  {
    img->raw_black_level_separate[c] = 0;
    img->dsc.processed_maximum[c] = 1.0f;
  }
  dt_free(img->profile);
  img->profile = NULL;
  img->profile_size = 0;
  g_list_free_full(img->dng_gain_maps, dt_free_gpointer);
  img->dng_gain_maps = NULL;
  img->exif_correction.type = LS_VENDOR_NONE;
  if(!IS_NULL_PTR(mbuf))
  {
    float *pixels = dt_mipmap_cache_alloc(mbuf, img);
    if(IS_NULL_PTR(pixels))
    {
      result = DT_IMAGEIO_CACHE_FULL;
      goto cleanup;
    }
    int invalid = 0;
    __OMP_PARALLEL_FOR__(reduction(|:invalid))
    for(size_t row = 0; row < info.height; row++)
      for(size_t col = 0; col < info.width; col++)
      {
        const float *in = info.samples + row * info.stride_samples + col * 3;
        float *out = pixels + (row * info.width + col) * 4;
        for(int c = 0; c < 3; c++)
        {
          out[c] = in[c];
          invalid |= !isfinite(in[c]);
        }
        out[3] = 0.0f;
      }
    if(invalid) goto cleanup;
  }
  img->loader = LOADER_RAWDINAL;
  result = DT_IMAGEIO_OK;

cleanup:
  if(result == DT_IMAGEIO_DECODE_FAILED || result == DT_IMAGEIO_CACHE_FULL)
    dt_print(DT_DEBUG_IMAGEIO, "[rawdinal] %s: status %d, %s\n", filename, status, message);
  rawdinal_raw_v1_free(decoded);
  g_mapped_file_unref(file);
  return result;
}
