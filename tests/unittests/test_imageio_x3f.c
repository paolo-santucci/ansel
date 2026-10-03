#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <rawdinal.h>

#include "imageio/imageio_x3f.h"

static int handle_token;
static int releases;
static int clipping_status;
static uint32_t clipping_version;
static uint32_t clipping_identity;
static uint32_t clipping_provenance;
static size_t clipping_stride;
static size_t clipping_byte_count;
static float output[8];
static const float rgba[] = { 0.2f, 0.4f, 0.6f, 1.0f, 1.1f, -0.1f, 0.25f, 1.0f };
static const uint8_t clipping_mask[] = { 1, 0 };
static char *filename;

int32_t rawdinal_decode_with_clipping_v1(const uint8_t *data, size_t length, rawdinal_image **image,
                                        rawdinal_info *info, char *error, size_t error_capacity)
{
  *image = (rawdinal_image *)&handle_token;
  *info = (rawdinal_info){ .width = 2, .height = 1 };
  return RAWDINAL_STATUS_OK;
}

int32_t rawdinal_copy_rgba(const rawdinal_image *image, float *destination, size_t float_count)
{
  assert_int_equal(float_count, 8);
  memcpy(destination, rgba, sizeof(rgba));
  return RAWDINAL_STATUS_OK;
}

int32_t rawdinal_get_clipping_v1(const rawdinal_image *image, rawdinal_clipping_v1_info *info)
{
  if(clipping_status != RAWDINAL_STATUS_OK) return clipping_status;
  *info = (rawdinal_clipping_v1_info){
    .version = clipping_version,
    .width = 2,
    .height = 1,
    .stride_bytes = clipping_stride,
    .byte_count = clipping_byte_count,
    .data = clipping_mask,
    .planes = {
      { .identity = clipping_identity, .threshold_provenance = clipping_provenance },
      { .identity = RAWDINAL_SENSOR_V1_MIDDLE,
        .threshold_provenance = RAWDINAL_CLIPPING_V1_THRESHOLD_ESTIMATED_ENCODED_MAXIMUM },
      { .identity = RAWDINAL_SENSOR_V1_TOP,
        .threshold_provenance = RAWDINAL_CLIPPING_V1_THRESHOLD_ESTIMATED_ENCODED_MAXIMUM }
    }
  };
  return RAWDINAL_STATUS_OK;
}

void rawdinal_free(rawdinal_image *image)
{
  if(image) releases++;
}

void *dt_mipmap_cache_alloc(dt_mipmap_buffer_t *buf, const dt_image_t *img)
{
  return output;
}

static int setup(void **state)
{
  const int fd = g_file_open_tmp("ansel-x3f-XXXXXX", &filename, NULL);
  assert_true(fd >= 0);
  assert_true(g_close(fd, NULL));
  assert_true(g_file_set_contents(filename, "test", 4, NULL));
  clipping_status = RAWDINAL_STATUS_OK;
  clipping_version = RAWDINAL_CLIPPING_V1_INFO_VERSION;
  clipping_identity = RAWDINAL_SENSOR_V1_BOTTOM;
  clipping_provenance = RAWDINAL_CLIPPING_V1_THRESHOLD_ESTIMATED_ENCODED_MAXIMUM;
  clipping_stride = 2;
  clipping_byte_count = 2;
  releases = 0;
  memset(output, 0, sizeof(output));
  return 0;
}

static int teardown(void **state)
{
  g_unlink(filename);
  g_free(filename);
  return 0;
}

static void source_clipping_invalidates_the_complete_rgb_triplet(void **state)
{
  dt_image_t image = { 0 };
  dt_mipmap_buffer_t buffer = { 0 };
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_OK);
  const float clipped = nextafterf(1.0f, INFINITY);
  assert_float_equal(output[0], clipped, 0);
  assert_float_equal(output[1], clipped, 0);
  assert_float_equal(output[2], clipped, 0);
  assert_float_equal(output[3], 1.0f, 0);
  assert_memory_equal(output + 4, rgba + 4, 4 * sizeof(float));
  assert_float_equal(image.dsc.processed_maximum[0], 1.0f, 0);
  assert_int_equal(releases, 1);
}

static void malformed_clipping_descriptor_rejects_the_decode(void **state)
{
  dt_image_t image = { 0 };
  dt_mipmap_buffer_t buffer = { 0 };
  clipping_version = 0;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  clipping_version = RAWDINAL_CLIPPING_V1_INFO_VERSION;
  clipping_identity = RAWDINAL_SENSOR_V1_MIDDLE;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  clipping_identity = RAWDINAL_SENSOR_V1_BOTTOM;
  clipping_provenance = 0;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  clipping_provenance = RAWDINAL_CLIPPING_V1_THRESHOLD_CALIBRATED;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_OK);
  clipping_status = RAWDINAL_STATUS_ERROR;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  assert_int_equal(releases, 5);
}

static void malformed_clipping_storage_rejects_the_decode(void **state)
{
  dt_image_t image = { 0 };
  dt_mipmap_buffer_t buffer = { 0 };
  clipping_stride = 1;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  clipping_stride = 2;
  clipping_byte_count = 1;
  assert_int_equal(dt_imageio_open_x3f(&image, filename, &buffer), DT_IMAGEIO_FILE_CORRUPTED);
  assert_int_equal(releases, 2);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(source_clipping_invalidates_the_complete_rgb_triplet, setup, teardown),
    cmocka_unit_test_setup_teardown(malformed_clipping_descriptor_rejects_the_decode, setup, teardown),
    cmocka_unit_test_setup_teardown(malformed_clipping_storage_rejects_the_decode, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
