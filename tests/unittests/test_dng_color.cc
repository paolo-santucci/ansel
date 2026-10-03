#include <cstdarg>
#include <cstddef>
#include <csetjmp>
#include <exiv2/exiv2.hpp>
#include <glib/gstdio.h>
#include <unistd.h>
#include <tiffio.h>
#include "metadata/dng_color.h"
#include "darktable.h"
#include "system/mem_alloc.h"
extern "C" {
#include <cmocka.h>
}

static char *path;

static int setup(void **state)
{
  const int fd = g_file_open_tmp("ansel-dng-color-XXXXXX", &path, NULL);
  assert_true(fd >= 0);
  close(fd);
  TIFF *tiff = TIFFOpen(path, "w");
  assert_non_null(tiff);
  TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 1);
  TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1);
  TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
  TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  uint8_t pixel = 0;
  assert_int_equal(TIFFWriteScanline(tiff, &pixel, 0, 0), 1);
  TIFFClose(tiff);
  auto image = Exiv2::ImageFactory::open(path);
  image->readMetadata();
  auto &data = image->exifData();
  data["Exif.Image.ColorMatrix1"].setValue("1/1 0/1 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  data["Exif.Image.CalibrationIlluminant1"] = uint16_t(21);
  Exiv2::URationalValue neutral;
  neutral.read("1/2 1/1 1/4");
  data["Exif.Image.AsShotNeutral"].setValue(&neutral);
  data["Exif.Image.AnalogBalance"].setValue("2/1 1/1 3/1");
  image->writeMetadata();
  return 0;
}

static int teardown(void **state)
{
  g_unlink(path);
  dt_free(path);
  return 0;
}

static void reads_neutral_and_analog_balance(void **state)
{
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), 0);
  assert_float_equal(image.wb_coeffs[0], 2, 1e-6);
  assert_float_equal(image.wb_coeffs[1], 1, 1e-6);
  assert_float_equal(image.wb_coeffs[2], 4, 1e-6);
  assert_float_equal(image.d65_color_matrix[0], 2, 1e-6);
  assert_float_equal(image.d65_color_matrix[4], 1, 1e-6);
  assert_float_equal(image.d65_color_matrix[8], 3, 1e-6);
}

static void calibration_order_is_not_commutative(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data["Exif.Image.ColorMatrix1"].setValue("1/1 0/1 0/1 0/1 2/1 0/1 0/1 0/1 3/1");
  data["Exif.Image.CameraCalibration1"].setValue("1/1 1/10 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), 0);
  assert_float_equal(image.d65_color_matrix[1], 0.4, 1e-6);
  assert_float_equal(image.d65_color_matrix[8], 9, 1e-6);
}

static void mismatched_calibration_signatures_ignore_calibration(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data["Exif.Image.CameraCalibration1"].setValue("2/1 0/1 0/1 0/1 2/1 0/1 0/1 0/1 2/1");
  data["Exif.Image.CameraCalibrationSignature"].setValue("99 97 109 101 114 97");
  data["Exif.Image.ProfileCalibrationSignature"].setValue("100 105 102 102 101 114 101 110 116");
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), 0);
  assert_float_equal(image.d65_color_matrix[0], 2, 1e-6);
}

static void invalid_neutral_fails(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  Exiv2::URationalValue neutral;
  neutral.read("0/1 1/1 1/1");
  source->exifData()["Exif.Image.AsShotNeutral"].setValue(&neutral);
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_DECODE_FAILED);
}

static void interpolates_dual_illuminant_at_d65(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data["Exif.Image.CalibrationIlluminant1"] = uint16_t(17);
  data["Exif.Image.CalibrationIlluminant2"] = uint16_t(22);
  data["Exif.Image.ColorMatrix2"].setValue("2/1 0/1 0/1 0/1 2/1 0/1 0/1 0/1 2/1");
  source->writeMetadata();
  dt_image_t image = {};
  const double weight = (1.0 / 6504.0 - 1.0 / 2850.0) / (1.0 / 7500.0 - 1.0 / 2850.0);
  assert_int_equal(dt_dng_color_read(&image, path), 0);
  assert_float_equal(image.d65_color_matrix[0], 2 * (1 + weight), 1e-6);
}

static void supports_as_shot_white_xy(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data.erase(data.findKey(Exiv2::ExifKey("Exif.Image.AsShotNeutral")));
  Exiv2::URationalValue white;
  white.read("3127/10000 3290/10000");
  data["Exif.Image.AsShotWhiteXY"].setValue(&white);
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_OK);
  assert_float_equal(image.wb_coeffs[0], 0.3290 / (2 * 0.3127), 1e-6);
  assert_float_equal(image.wb_coeffs[2], 0.3290 / (3 * (1 - 0.3127 - 0.3290)), 1e-6);
}

static void interpolates_calibrated_endpoints_without_cross_terms(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data["Exif.Image.CalibrationIlluminant1"] = uint16_t(17);
  data["Exif.Image.CalibrationIlluminant2"] = uint16_t(22);
  data["Exif.Image.CameraCalibration1"].setValue("1/1 1/2 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  data["Exif.Image.CameraCalibration2"].setValue("1/1 1/4 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  data["Exif.Image.ColorMatrix2"].setValue("1/1 0/1 0/1 0/1 3/1 0/1 0/1 0/1 1/1");
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_OK);
  const double weight = (1.0 / 6504.0 - 1.0 / 2850.0) / (1.0 / 7500.0 - 1.0 / 2850.0);
  assert_float_equal(image.d65_color_matrix[1], 1.0 + 0.5 * weight, 1e-6);
}

static void supports_single_calibration_across_standard_illuminants(void **state)
{
  const uint16_t illuminants[] = { 1, 2, 3, 4, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 };
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  for(const uint16_t illuminant : illuminants)
  {
    source->exifData()["Exif.Image.CalibrationIlluminant1"] = illuminant;
    source->writeMetadata();
    dt_image_t image = {};
    assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_OK);
    assert_float_equal(image.d65_color_matrix[0], 2, 1e-6);
    assert_float_equal(image.d65_color_matrix[4], 1, 1e-6);
    assert_float_equal(image.d65_color_matrix[8], 3, 1e-6);
  }
}

static void standard_light_b_and_c_select_the_d65_endpoint(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  auto &data = source->exifData();
  data["Exif.Image.CalibrationIlluminant1"] = uint16_t(18);
  data["Exif.Image.CalibrationIlluminant2"] = uint16_t(19);
  data["Exif.Image.ColorMatrix2"].setValue("2/1 0/1 0/1 0/1 2/1 0/1 0/1 0/1 2/1");
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_OK);
  assert_float_equal(image.d65_color_matrix[0], 4, 1e-6);
}

static void single_calibration_does_not_require_a_known_illuminant(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  source->exifData()["Exif.Image.CalibrationIlluminant1"] = uint16_t(0);
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_OK);
  assert_float_equal(image.d65_color_matrix[0], 2, 1e-6);
}

static void unsupported_profiles_permit_fallback(void **state)
{
  auto source = Exiv2::ImageFactory::open(path);
  source->readMetadata();
  source->exifData()["Exif.Image.ForwardMatrix1"].setValue("1/1 0/1 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  source->writeMetadata();
  dt_image_t image = {};
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_UNSUPPORTED_FEATURE);
  auto &data = source->exifData();
  data.erase(data.findKey(Exiv2::ExifKey("Exif.Image.ForwardMatrix1")));
  data["Exif.Image.CalibrationIlluminant1"] = uint16_t(0);
  data["Exif.Image.CalibrationIlluminant2"] = uint16_t(21);
  data["Exif.Image.ColorMatrix2"].setValue("1/1 0/1 0/1 0/1 1/1 0/1 0/1 0/1 1/1");
  source->writeMetadata();
  assert_int_equal(dt_dng_color_read(&image, path), DT_IMAGEIO_UNSUPPORTED_FEATURE);
}

int main(void)
{
  darktable.unmuted = DT_DEBUG_IMAGEIO;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(reads_neutral_and_analog_balance, setup, teardown),
    cmocka_unit_test_setup_teardown(calibration_order_is_not_commutative, setup, teardown),
    cmocka_unit_test_setup_teardown(mismatched_calibration_signatures_ignore_calibration, setup, teardown),
    cmocka_unit_test_setup_teardown(invalid_neutral_fails, setup, teardown),
    cmocka_unit_test_setup_teardown(interpolates_dual_illuminant_at_d65, setup, teardown),
    cmocka_unit_test_setup_teardown(supports_as_shot_white_xy, setup, teardown),
    cmocka_unit_test_setup_teardown(interpolates_calibrated_endpoints_without_cross_terms, setup, teardown),
    cmocka_unit_test_setup_teardown(supports_single_calibration_across_standard_illuminants, setup, teardown),
    cmocka_unit_test_setup_teardown(standard_light_b_and_c_select_the_d65_endpoint, setup, teardown),
    cmocka_unit_test_setup_teardown(single_calibration_does_not_require_a_known_illuminant, setup, teardown),
    cmocka_unit_test_setup_teardown(unsupported_profiles_permit_fallback, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
