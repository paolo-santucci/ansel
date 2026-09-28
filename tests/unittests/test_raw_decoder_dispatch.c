#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include "imageio/imageio_core.h"
#include "common/conf.h"
#include "system/mem_alloc.h"

extern dt_imageio_retval_t dt_imageio_open_raw(dt_image_t *img, const char *path, dt_mipmap_buffer_t *buf);

static dt_imageio_retval_t speed_result, rawdinal_result, libraw_result;
static int speed_calls, rawdinal_calls, libraw_calls, x3f_calls;
static gboolean force_libraw;

char *dt_conf_get_string(const char *key)
{
  return g_strdup(force_libraw && !strcmp(key, "libraw/extensions") ? "dng" : "");
}

dt_imageio_retval_t dt_imageio_open_rawspeed(dt_image_t *img, const char *path, dt_mipmap_buffer_t *buf)
{
  speed_calls++;
  return speed_result;
}

dt_imageio_retval_t dt_imageio_open_rawdinal(dt_image_t *img, const char *path, dt_mipmap_buffer_t *buf)
{
  rawdinal_calls++;
  return rawdinal_result;
}

dt_imageio_retval_t dt_imageio_open_libraw(dt_image_t *img, const char *path, dt_mipmap_buffer_t *buf)
{
  libraw_calls++;
  return libraw_result;
}

dt_imageio_retval_t dt_imageio_open_x3f(dt_image_t *img, const char *path, dt_mipmap_buffer_t *buf)
{
  x3f_calls++;
  return DT_IMAGEIO_OK;
}

void dt_control_log(const char *format, ...)
{
}

static int setup(void **state)
{
  speed_calls = rawdinal_calls = libraw_calls = x3f_calls = 0;
  force_libraw = FALSE;
  speed_result = rawdinal_result = DT_IMAGEIO_UNSUPPORTED_FORMAT;
  libraw_result = DT_IMAGEIO_OK;
  return 0;
}

static void supported_rawspeed_has_priority(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  speed_result = DT_IMAGEIO_OK;
  assert_int_equal(dt_imageio_open_raw(&img, "test.dng", &buf), DT_IMAGEIO_OK);
  assert_int_equal(speed_calls, 1);
  assert_int_equal(rawdinal_calls, 0);
  assert_int_equal(libraw_calls, 0);
}

static void rawdinal_precedes_libraw(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  speed_result = DT_IMAGEIO_FILE_CORRUPTED;
  rawdinal_result = DT_IMAGEIO_OK;
  assert_int_equal(dt_imageio_open_raw(&img, "test.dng", &buf), DT_IMAGEIO_OK);
  assert_int_equal(speed_calls, 1);
  assert_int_equal(rawdinal_calls, 1);
  assert_int_equal(libraw_calls, 0);
}

static void unsupported_reaches_libraw(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  assert_int_equal(dt_imageio_open_raw(&img, "test.dng", &buf), DT_IMAGEIO_OK);
  assert_int_equal(rawdinal_calls, 1);
  assert_int_equal(libraw_calls, 1);
}

static void corrupt_owned_input_stops_outer_fallback(void **state)
{
  char *path = NULL;
  const int fd = g_file_open_tmp("ansel-dispatch-XXXXXX", &path, NULL);
  assert_true(fd >= 0);
  close(fd);
  char *dng = g_strconcat(path, ".dng", NULL);
  assert_int_equal(g_rename(path, dng), 0);
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  rawdinal_result = DT_IMAGEIO_DECODE_FAILED;
  assert_int_equal(dt_imageio_open(&img, dng, &buf), DT_IMAGEIO_DECODE_FAILED);
  assert_int_equal(speed_calls, 1);
  assert_int_equal(rawdinal_calls, 1);
  assert_int_equal(libraw_calls, 0);
  g_unlink(dng);
  dt_free(path);
  dt_free(dng);
}

static void x3f_keeps_direct_route(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  assert_int_equal(dt_imageio_open_raw(&img, "test.X3F", &buf), DT_IMAGEIO_OK);
  assert_int_equal(x3f_calls, 1);
  assert_int_equal(speed_calls, 0);
  assert_int_equal(rawdinal_calls, 0);
}

static void explicit_libraw_override_is_preserved(void **state)
{
  dt_image_t img = { 0 };
  dt_mipmap_buffer_t buf = { 0 };
  force_libraw = TRUE;
  assert_int_equal(dt_imageio_open_raw(&img, "test.dng", &buf), DT_IMAGEIO_OK);
  assert_int_equal(libraw_calls, 1);
  assert_int_equal(speed_calls, 0);
  assert_int_equal(rawdinal_calls, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup(supported_rawspeed_has_priority, setup),
    cmocka_unit_test_setup(rawdinal_precedes_libraw, setup),
    cmocka_unit_test_setup(unsupported_reaches_libraw, setup),
    cmocka_unit_test_setup(corrupt_owned_input_stops_outer_fallback, setup),
    cmocka_unit_test_setup(x3f_keeps_direct_route, setup),
    cmocka_unit_test_setup(explicit_libraw_override_is_preserved, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
