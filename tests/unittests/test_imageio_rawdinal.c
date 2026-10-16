#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <rawdinal.h>
#include "imageio/imageio_rawdinal.h"
#include "metadata/dng_color.h"
#include "metadata/exif.h"
#include "system/mem_alloc.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/iop_order.h"

extern void dt_iop_highlights__reload_defaults(dt_iop_module_t *module);
extern int dt_iop_rawprepare__default_colorspace(dt_iop_module_t *module, dt_dev_pixelpipe_t *pipe,
                                                const dt_dev_pixelpipe_iop_t *piece);
extern void dt_iop_rawprepare__init_pipe(dt_iop_module_t *module, dt_dev_pixelpipe_t *pipe,
                                        dt_dev_pixelpipe_iop_t *piece);
extern void dt_iop_rawprepare__cleanup_pipe(dt_iop_module_t *module, dt_dev_pixelpipe_t *pipe,
                                           dt_dev_pixelpipe_iop_t *piece);
extern void dt_iop_rawprepare__output_format(dt_iop_module_t *module, dt_dev_pixelpipe_t *pipe,
                                            dt_dev_pixelpipe_iop_t *piece, dt_iop_buffer_dsc_t *dsc);

static rawdinal_raw_v1_info descriptor;
static rawdinal_raw_v1_metadata metadata;
static int probe_status, decode_status, info_status, raw_metadata_status, classification, releases, allocations,
    color_status, decode_calls;
static uint32_t codec, capability_bits, capabilities_version;
static int handle_token;
static gboolean allocation_failure;
static float output[8];
static const float samples[] = { -0.5f, 0.5f, 1.5f, 2.0f, 0.0f, 0.25f };
static const float nonfinite_samples[] = { -0.5f, 0.5f, NAN, 2.0f, 0.0f, 0.25f };
static const uint8_t components[] = { 1, 2, 3 };
static const uint8_t profile_gain_table_map[] = { 0 };
static char *filename;

int32_t rawdinal_raw_v1_probe(const uint8_t *data, size_t length, rawdinal_raw_v1_probe_info *info,
                              char *error, size_t error_capacity)
{
  *info = (rawdinal_raw_v1_probe_info){ .version = 1, .classification = classification,
                                     .container = RAWDINAL_RAW_V1_CONTAINER_DNG,
                                     .codec = codec };
  return probe_status;
}

int32_t rawdinal_raw_v1_get_capabilities(rawdinal_raw_v1_capabilities *info)
{
  *info = (rawdinal_raw_v1_capabilities){ .version = capabilities_version, .codec_bits = capability_bits };
  return RAWDINAL_STATUS_OK;
}

int32_t rawdinal_raw_v1_decode(const uint8_t *data, size_t length, rawdinal_raw_v1_image **image,
                               char *error, size_t error_capacity)
{
  decode_calls++;
  *image = decode_status == RAWDINAL_STATUS_OK ? (rawdinal_raw_v1_image *)&handle_token : NULL;
  return decode_status;
}

int32_t rawdinal_raw_v1_get_info(const rawdinal_raw_v1_image *image, rawdinal_raw_v1_info *info)
{
  *info = descriptor;
  return info_status;
}

int32_t rawdinal_raw_v1_get_metadata(const rawdinal_raw_v1_image *image, rawdinal_raw_v1_metadata *info)
{
  *info = metadata;
  return raw_metadata_status;
}

void rawdinal_raw_v1_free(rawdinal_raw_v1_image *image)
{
  if(!IS_NULL_PTR(image)) releases++;
}

void *dt_mipmap_cache_alloc(dt_mipmap_buffer_t *buf, const dt_image_t *img)
{
  allocations++;
  return allocation_failure ? NULL : output;
}

dt_imageio_retval_t dt_dng_color_read(dt_image_t *img, const char *path)
{
  return color_status;
}

int dt_exif_read(dt_image_t *img, const char *path)
{
  return 0;
}

void dt_exif_read_usercrop(dt_image_t *img, const char *path)
{
}

static int setup(void **state)
{
  const int fd = g_file_open_tmp("ansel-rawdinal-XXXXXX", &filename, NULL);
  assert_true(fd >= 0);
  assert_int_equal(write(fd, "test", 4), 4);
  close(fd);
  probe_status = decode_status = info_status = raw_metadata_status = color_status = releases = allocations = decode_calls = 0;
  classification = RAWDINAL_RAW_V1_PROBE_SUPPORTED;
  codec = RAWDINAL_RAW_V1_CODEC_LOSSLESS_JPEG;
  capability_bits = RAWDINAL_RAW_V1_CODEC_BIT_LOSSLESS_JPEG;
  capabilities_version = RAWDINAL_RAW_V1_CAPABILITIES_VERSION;
  allocation_failure = FALSE;
  descriptor = (rawdinal_raw_v1_info){
    .version = 1, .width = 2, .height = 1, .channels = 3, .stride_samples = 6, .sample_count = 6,
    .samples = samples, .component_ids = components, .component_id_count = 3,
    .active_area_right = 2, .active_area_bottom = 1,
    .linearization = RAWDINAL_RAW_V1_PROCESSING_APPLIED,
    .black_subtraction = RAWDINAL_RAW_V1_PROCESSING_APPLIED,
    .white_normalization = RAWDINAL_RAW_V1_PROCESSING_APPLIED,
    .white_balance = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED,
    .color_conversion = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED,
    .scene_linear = 1, .camera_native = 1, .already_demosaiced = 1 };
  metadata = (rawdinal_raw_v1_metadata){ .version = 1, .valid_area = { 0, 0, 1, 2 } };
  return 0;
}

static int teardown(void **state)
{
  g_unlink(filename);
  dt_free(filename);
  return 0;
}

static void linear_raw_preserves_samples(void **state)
{
  dt_image_t img = { .flags = DT_IMAGE_RAW | DT_IMAGE_MOSAIC | DT_IMAGE_LDR | DT_IMAGE_4BAYER };
  dt_mipmap_buffer_t buf = { 0 };
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), DT_IMAGEIO_OK);
  dt_image_buffer_resolve_flags(&img);
  assert_int_equal(dt_image_pipe_class(&img), DT_IMAGE_PIPE_LINEAR_RAW);
  assert_false(dt_image_needs_demosaic(&img));
  assert_true(dt_image_needs_rawprepare(&img));
  assert_int_equal(img.dsc.cst, IOP_CS_RAW);
  assert_int_equal(img.loader, LOADER_RAWDINAL);
  assert_int_equal(img.raw_white_point, 1);
  assert_int_equal(img.raw_black_level, 0);
  assert_int_equal(allocations, 1);
  assert_int_equal(releases, 1);
  for(int i = 0; i < 6; i++) assert_float_equal(output[(i / 3) * 4 + i % 3], samples[i], 0);
  assert_float_equal(output[3], 0, 0);
  assert_float_equal(output[7], 0, 0);
  dt_develop_t dev = { .image_storage = img };
  dt_iop_module_t highlights = { .dev = &dev };
  dt_iop_highlights__reload_defaults(&highlights);
  assert_false(highlights.default_enabled);
  dev.image_storage.loader = LOADER_RAWSPEED;
  dt_iop_highlights__reload_defaults(&highlights);
  assert_true(highlights.default_enabled);
}

static void errors_have_stable_fallback_semantics(void **state)
{
  const struct
  {
    int status;
    dt_imageio_retval_t result;
  } cases[] = { { RAWDINAL_STATUS_NOT_RECOGNIZED, DT_IMAGEIO_UNSUPPORTED_FORMAT },
                 { RAWDINAL_STATUS_UNSUPPORTED, DT_IMAGEIO_UNSUPPORTED_FEATURE },
                 { RAWDINAL_STATUS_ALLOCATION, DT_IMAGEIO_CACHE_FULL },
                 { RAWDINAL_STATUS_ERROR, DT_IMAGEIO_DECODE_FAILED },
                 { RAWDINAL_STATUS_INVALID_INPUT, DT_IMAGEIO_DECODE_FAILED },
                { RAWDINAL_STATUS_RESOURCE_LIMIT, DT_IMAGEIO_DECODE_FAILED },
                { RAWDINAL_STATUS_PANIC, DT_IMAGEIO_DECODE_FAILED } };
  for(size_t i = 0; i < G_N_ELEMENTS(cases); i++)
  {
    dt_image_t img = { 0 };
    dt_mipmap_buffer_t buf = { 0 };
    decode_status = cases[i].status;
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), cases[i].result);
    probe_status = cases[i].status;
    decode_status = RAWDINAL_STATUS_OK;
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), cases[i].result);
    probe_status = RAWDINAL_STATUS_OK;
  }
  assert_int_equal(allocations, 0);
  assert_int_equal(releases, 0);
}

static void rejects_invalid_descriptors_before_allocation(void **state)
{
  const rawdinal_raw_v1_info valid = descriptor;
  for(int variant = 0; variant < 10; variant++)
  {
    dt_image_t img = { 0 };
    dt_mipmap_buffer_t buf = { 0 };
    descriptor = valid;
    switch(variant)
    {
      case 0: descriptor.width = UINT32_MAX; break;
      case 1: descriptor.stride_samples = SIZE_MAX; break;
      case 2: descriptor.sample_count = 5; break;
      case 3: descriptor.samples = NULL; break;
      case 4: descriptor.white_balance = RAWDINAL_RAW_V1_PROCESSING_APPLIED; break;
      case 5: descriptor.opcode_list_2 = RAWDINAL_RAW_V1_PROCESSING_UNKNOWN; break;
      case 6: descriptor.active_area_bottom = 2; break;
      case 7: descriptor.channels = 4; break;
      case 8: descriptor.profile_tone_curve = RAWDINAL_RAW_V1_PROCESSING_APPLIED; break;
      case 9: descriptor.default_crop_origin_present = 1; descriptor.default_crop_origin_x = 2; break;
    }
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf),
                     variant == 4 || variant == 5 || variant == 8 ? DT_IMAGEIO_UNSUPPORTED_FEATURE
                                                                 : DT_IMAGEIO_DECODE_FAILED);
  }
  assert_int_equal(allocations, 0);
  assert_int_equal(releases, 10);
}

static void allocation_failure_releases_handle(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  allocation_failure = TRUE;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), DT_IMAGEIO_CACHE_FULL);
  assert_int_equal(releases, 1);
}

static void probe_classifications_map_to_fallbacks_without_decoding(void **state)
{
  const struct
  {
    uint32_t classification;
    dt_imageio_retval_t result;
  } cases[] = { { RAWDINAL_RAW_V1_PROBE_NOT_RECOGNIZED, DT_IMAGEIO_UNSUPPORTED_FORMAT },
                { RAWDINAL_RAW_V1_PROBE_RECOGNIZED_UNSUPPORTED, DT_IMAGEIO_UNSUPPORTED_FEATURE } };
  for(size_t i = 0; i < G_N_ELEMENTS(cases); i++)
  {
    dt_image_t img = { 0 };
    classification = cases[i].classification;
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), cases[i].result);
  }
  assert_int_equal(decode_calls, 0);
  assert_int_equal(releases, 0);
}

static void supported_codecs_are_capability_gated(void **state)
{
  const struct
  {
    uint32_t codec, bit;
  } codecs[] = { { RAWDINAL_RAW_V1_CODEC_UNCOMPRESSED, RAWDINAL_RAW_V1_CODEC_BIT_UNCOMPRESSED },
                 { RAWDINAL_RAW_V1_CODEC_LOSSLESS_JPEG, RAWDINAL_RAW_V1_CODEC_BIT_LOSSLESS_JPEG },
                 { RAWDINAL_RAW_V1_CODEC_JPEG_XL, RAWDINAL_RAW_V1_CODEC_BIT_JPEG_XL },
                 { RAWDINAL_RAW_V1_CODEC_LOSSY_JPEG, RAWDINAL_RAW_V1_CODEC_BIT_LOSSY_JPEG } };
  for(size_t i = 0; i < G_N_ELEMENTS(codecs); i++)
  {
    dt_image_t img = { 0 };
    codec = codecs[i].codec;
    capability_bits = codecs[i].bit;
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_OK);
  }
  codec = RAWDINAL_RAW_V1_CODEC_JPEG_XL;
  capability_bits = RAWDINAL_RAW_V1_CODEC_BIT_LOSSLESS_JPEG;
  dt_image_t img = { 0 };
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  codec = RAWDINAL_RAW_V1_CODEC_UNKNOWN;
  capability_bits = UINT32_MAX;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  assert_int_equal(decode_calls, 4);
}

static void identity_linearization_opcode_and_profile_gain_map_states_are_accepted(void **state)
{
  const uint32_t states[] = { RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT, RAWDINAL_RAW_V1_PROCESSING_APPLIED,
                              RAWDINAL_RAW_V1_PROCESSING_SKIPPED_OPTIONAL,
                              RAWDINAL_RAW_V1_PROCESSING_PARTIALLY_APPLIED };
  descriptor.linearization = RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT;
  for(size_t i = 0; i < G_N_ELEMENTS(states); i++)
  {
    dt_image_t img = { 0 };
    descriptor.opcode_list_1 = states[i];
    descriptor.opcode_list_2 = states[i];
    descriptor.opcode_list_3 = states[i];
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_OK);
  }
  descriptor.profile_gain_table_map_processing = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED;
  descriptor.profile_gain_table_map = profile_gain_table_map;
  descriptor.profile_gain_table_map_length = sizeof(profile_gain_table_map);
  dt_image_t img = { 0 };
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_OK);
  descriptor.opcode_list_1 = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  descriptor.opcode_list_1 = RAWDINAL_RAW_V1_PROCESSING_UNKNOWN;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  descriptor.opcode_list_1 = RAWDINAL_RAW_V1_PROCESSING_NOT_PRESENT;
  descriptor.linearization = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  descriptor.linearization = RAWDINAL_RAW_V1_PROCESSING_UNKNOWN;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
}

static void crop_and_orientation_are_deferred(void **state)
{
  dt_image_t img = { 0 };
  descriptor.default_crop = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED;
  descriptor.default_crop_origin_present = 1;
  descriptor.default_crop_origin_x = 1;
  descriptor.default_crop_size_present = 1;
  descriptor.default_crop_size_width = descriptor.default_crop_size_height = 1;
  metadata.crop_origin_present = 1;
  metadata.crop_size_present = 1;
  metadata.crop_origin[0] = 1.0;
  metadata.crop_origin[1] = 0.0;
  metadata.crop_size[0] = metadata.crop_size[1] = 1.0;
  descriptor.orientation_present = 1;
  descriptor.orientation = 6;
  descriptor.orientation_processing = RAWDINAL_RAW_V1_PROCESSING_UNAPPLIED;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_OK);
  assert_int_equal(img.width, 2);
  assert_int_equal(img.crop_x, 1);
  assert_int_equal(img.crop_width, 0);
  assert_int_equal(img.orientation, ORIENTATION_ROTATE_CW_90_DEG);
  assert_int_equal(allocations, 0);
  assert_int_equal(releases, 1);
}

static void dng_color_failure_releases_handle(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  color_status = DT_IMAGEIO_DECODE_FAILED;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), DT_IMAGEIO_DECODE_FAILED);
  assert_int_equal(allocations, 0);
  assert_int_equal(releases, 1);
}

static void trimmed_valid_area_is_unsupported_and_fractional_crops_are_checked(void **state)
{
  dt_image_t img = { 0 };
  metadata.valid_area[0] = 0;
  metadata.valid_area[1] = 1;
  metadata.valid_area[2] = 1;
  metadata.valid_area[3] = 2;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  metadata.valid_area[1] = 0;

  descriptor.default_crop_origin_present = 1;
  descriptor.default_crop_size_present = 1;
  descriptor.default_crop_size_width = 2;
  descriptor.default_crop_size_height = 1;
  metadata.crop_origin_present = 1;
  metadata.crop_origin[0] = 0.5;
  metadata.crop_origin[1] = 0.0;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  descriptor.default_crop_origin_present = 0;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  descriptor.default_crop_origin_present = 1;
  metadata.crop_origin[0] = 0.0;
  metadata.crop_size_present = 1;
  metadata.crop_size[0] = 1.5;
  metadata.crop_size[1] = 1.0;
  descriptor.default_crop_size_present = 0;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  descriptor.default_crop_size_present = 1;
  metadata.crop_origin[0] = NAN;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  metadata.crop_origin[0] = 0.0;
  metadata.crop_size_present = 1;
  metadata.crop_size[0] = 1.5;
  metadata.crop_size[1] = 1.0;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  metadata.crop_size[0] = INFINITY;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  metadata.crop_size[0] = 2.0;
  metadata.valid_area[3] = 3;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  metadata.valid_area[3] = 2;
  metadata.crop_origin[0] = 2.0;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
}

static void nonfull_active_area_is_unsupported_before_allocation(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  descriptor.active_area_left = 1;
  metadata.valid_area[1] = 1;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  assert_int_equal(allocations, 0);
  assert_int_equal(releases, 1);
}

static void abi_version_mismatches_are_decode_failures(void **state)
{
  dt_image_t img = { 0 };
  capabilities_version++;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  capabilities_version--;
  descriptor.version++;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  descriptor.version--;
  metadata.version++;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
  assert_int_equal(releases, 2);
}

static void nonfinite_samples_are_decode_failures(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  descriptor.samples = nonfinite_samples;
  assert_int_equal(dt_imageio_open_rawdinal(&img, filename, &buf), DT_IMAGEIO_DECODE_FAILED);
  assert_int_equal(allocations, 1);
  assert_int_equal(releases, 1);
}

static void image_and_metadata_getter_failures_are_decode_failures(void **state)
{
  const int statuses[] = { RAWDINAL_STATUS_ERROR, RAWDINAL_STATUS_PANIC, RAWDINAL_STATUS_UNSUPPORTED,
                           RAWDINAL_STATUS_INVALID_INPUT, RAWDINAL_STATUS_RESOURCE_LIMIT,
                           RAWDINAL_STATUS_ALLOCATION, RAWDINAL_STATUS_NOT_RECOGNIZED };
  for(size_t i = 0; i < G_N_ELEMENTS(statuses); i++)
  {
    dt_image_t img = { 0 };
    info_status = statuses[i];
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
    info_status = RAWDINAL_STATUS_OK;
    raw_metadata_status = statuses[i];
    assert_int_equal(dt_imageio_open_rawdinal(&img, filename, NULL), DT_IMAGEIO_DECODE_FAILED);
    raw_metadata_status = RAWDINAL_STATUS_OK;
  }
  assert_int_equal(releases, 14);
}

static void preparation_publishes_camera_rgb_without_demosaic(void **state)
{
  dt_develop_t dev = { 0 };
  assert_int_equal(dt_imageio_open_rawdinal(&dev.image_storage, filename, NULL), DT_IMAGEIO_OK);
  dt_iop_order_entry_t order = { .operation = "rawprepare", .o.iop_order = 1 };
  dt_dev_pixelpipe_t pipe = { .dev = &dev, .iop_order_list = g_list_append(NULL, &order) };
  dt_iop_module_t module = { .dev = &dev, .op = "rawprepare",
                            .default_colorspace = dt_iop_rawprepare__default_colorspace };
  dt_dev_pixelpipe_iop_t piece = { .module = &module,
                                  .roi_in = { .scale = 1 }, .dsc_in = dev.image_storage.dsc };
  dt_iop_rawprepare__init_pipe(&module, &pipe, &piece);
  dt_iop_rawprepare__output_format(&module, &pipe, &piece, &piece.dsc_out);
  assert_int_equal(piece.dsc_out.cst, IOP_CS_RGB);
  assert_int_equal(piece.dsc_out.channels, 4);
  assert_int_equal(piece.dsc_out.filters, 0);
  dev.image_storage.flags = DT_IMAGE_RAW | DT_IMAGE_MOSAIC;
  dev.image_storage.dsc.filters = 0x94949494u;
  dev.image_storage.dsc.channels = 1;
  piece.dsc_in = dev.image_storage.dsc;
  dt_iop_rawprepare__output_format(&module, &pipe, &piece, &piece.dsc_out);
  assert_int_equal(piece.dsc_out.cst, IOP_CS_RAW);
  assert_int_equal(piece.dsc_out.channels, 1);
  assert_int_equal(piece.dsc_out.filters, dev.image_storage.dsc.filters);
  dt_iop_rawprepare__cleanup_pipe(&module, &pipe, &piece);
  g_list_free(pipe.iop_order_list);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(linear_raw_preserves_samples, setup, teardown),
    cmocka_unit_test_setup_teardown(errors_have_stable_fallback_semantics, setup, teardown),
    cmocka_unit_test_setup_teardown(rejects_invalid_descriptors_before_allocation, setup, teardown),
    cmocka_unit_test_setup_teardown(allocation_failure_releases_handle, setup, teardown),
    cmocka_unit_test_setup_teardown(probe_classifications_map_to_fallbacks_without_decoding, setup, teardown),
    cmocka_unit_test_setup_teardown(supported_codecs_are_capability_gated, setup, teardown),
    cmocka_unit_test_setup_teardown(identity_linearization_opcode_and_profile_gain_map_states_are_accepted, setup, teardown),
    cmocka_unit_test_setup_teardown(crop_and_orientation_are_deferred, setup, teardown),
    cmocka_unit_test_setup_teardown(dng_color_failure_releases_handle, setup, teardown),
    cmocka_unit_test_setup_teardown(trimmed_valid_area_is_unsupported_and_fractional_crops_are_checked, setup, teardown),
    cmocka_unit_test_setup_teardown(nonfull_active_area_is_unsupported_before_allocation, setup, teardown),
    cmocka_unit_test_setup_teardown(image_and_metadata_getter_failures_are_decode_failures, setup, teardown),
    cmocka_unit_test_setup_teardown(abi_version_mismatches_are_decode_failures, setup, teardown),
    cmocka_unit_test_setup_teardown(nonfinite_samples_are_decode_failures, setup, teardown),
    cmocka_unit_test_setup_teardown(preparation_publishes_camera_rgb_without_demosaic, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
