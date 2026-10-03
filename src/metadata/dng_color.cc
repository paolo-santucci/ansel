#include "metadata/dng_color.h"
#include "common/logging.h"
#include <exiv2/exiv2.hpp>
#include <cmath>
#include <cfloat>
#include <stdexcept>
#include <string>
#include <lcms2.h>

namespace
{
bool read_values(const Exiv2::ExifData &data, const char *key, double *values, const long count)
{
  const auto tag = data.findKey(Exiv2::ExifKey(key));
  if(tag == data.end()) return false;
  if(tag->count() != count) throw std::runtime_error(std::string("invalid DNG calibration count: ") + key);
  for(long i = 0; i < count; i++)
  {
    values[i] = tag->toFloat(i);
    if(!std::isfinite(values[i])) throw std::runtime_error("non-finite DNG calibration");
  }
  return true;
}

double illuminant_temperature(const double illuminant)
{
  if(illuminant < 0 || illuminant > UINT16_MAX || std::trunc(illuminant) != illuminant) return 0;
  switch((int)illuminant)
  {
    case 17: case 3: return 2850.0;
    case 24: return 3200.0;
    case 21: case 19: case 10: return 6500.0;
    case 23: return 5000.0;
    case 20: case 18: case 1: case 4: case 9: return 5500.0;
    case 22: case 11: return 7500.0;
    case 12: return 6400.0;
    case 13: return 5050.0;
    case 14: case 2: return 4150.0;
    case 15: return 3525.0;
    case 16: return 2925.0;
    default: return 0;
  }
}

struct DngCalibration
{
  double color[2][9] = {};
  double camera[2][9] = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
  double analog[3] = { 1, 1, 1 };
};

/** Interpolate calibrated endpoints, as in DNG SDK FindXYZtoCamera, without cross-illuminant products. */
void calibrated_matrix(const DngCalibration &calibration, const double weight, double matrix[9])
{
  for(int row = 0; row < 3; row++)
    for(int col = 0; col < 3; col++)
    {
      matrix[row * 3 + col] = 0;
      for(int k = 0; k < 3; k++)
        matrix[row * 3 + col] += calibration.analog[row]
            * ((1.0 - weight) * calibration.camera[0][row * 3 + k] * calibration.color[0][k * 3 + col]
               + weight * calibration.camera[1][row * 3 + k] * calibration.color[1][k * 3 + col]);
    }
}
}

dt_imageio_retval_t dt_dng_color_read(dt_image_t *img, const char *filename)
{
  try
  {
    auto source = Exiv2::ImageFactory::open(filename);
    if(IS_NULL_PTR(source.get())) return DT_IMAGEIO_DECODE_FAILED;
    source->readMetadata();
    const auto &data = source->exifData();
    const char *unsupported[] = { "Exif.Image.ForwardMatrix1", "Exif.Image.ForwardMatrix2",
                                  "Exif.Image.ColorMatrix3" };
    for(const char *key : unsupported)
      if(data.findKey(Exiv2::ExifKey(key)) != data.end())
      {
        dt_print(DT_DEBUG_IMAGEIO, "[DNG color] %s: unsupported profile tag %s\n", filename, key);
        return DT_IMAGEIO_UNSUPPORTED_FEATURE;
      }
    DngCalibration calibration;
    double illuminants[2] = { 0, 0 };
    double neutral[3] = { 0, 0, 0 };
    if(!read_values(data, "Exif.Image.ColorMatrix1", calibration.color[0], 9)
       || !read_values(data, "Exif.Image.CalibrationIlluminant1", illuminants, 1))
      throw std::runtime_error("missing DNG camera matrix");
    const bool dual = read_values(data, "Exif.Image.ColorMatrix2", calibration.color[1], 9);
    if(dual && !read_values(data, "Exif.Image.CalibrationIlluminant2", illuminants + 1, 1))
      throw std::runtime_error("missing second DNG illuminant");
    read_values(data, "Exif.Image.AnalogBalance", calibration.analog, 3);
    const auto camera_signature = data.findKey(Exiv2::ExifKey("Exif.Image.CameraCalibrationSignature"));
    const auto profile_signature = data.findKey(Exiv2::ExifKey("Exif.Image.ProfileCalibrationSignature"));
    const std::string camera = camera_signature == data.end() ? "" : camera_signature->toString();
    const std::string profile = profile_signature == data.end() ? "" : profile_signature->toString();
    if(camera == profile)
    {
      read_values(data, "Exif.Image.CameraCalibration1", calibration.camera[0], 9);
      read_values(data, "Exif.Image.CameraCalibration2", calibration.camera[1], 9);
    }
    const double first = illuminant_temperature(illuminants[0]);
    const double second = dual ? illuminant_temperature(illuminants[1]) : first;
    if(dual && (first == 0 || second == 0)) return DT_IMAGEIO_UNSUPPORTED_FEATURE;
    double weight = 0;
    if(dual && first != second)
      weight = std::fmax(0.0, std::fmin(1.0, (1.0 / 6504.0 - 1.0 / first) / (1.0 / second - 1.0 / first)));
    double matrix[9];
    calibrated_matrix(calibration, weight, matrix);
    const double determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7])
                             - matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6])
                             + matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    if(!std::isfinite(determinant) || std::fabs(determinant) < 1e-12)
      throw std::runtime_error("singular DNG camera matrix");
    if(!read_values(data, "Exif.Image.AsShotNeutral", neutral, 3))
    {
      double xy[2];
      if(!read_values(data, "Exif.Image.AsShotWhiteXY", xy, 2)
         || xy[0] <= 0 || xy[1] <= 0 || xy[0] + xy[1] >= 1)
        throw std::runtime_error("missing or invalid DNG as-shot white");
      cmsCIExyY white = { xy[0], xy[1], 1.0 };
      double shot_weight = 0;
      if(dual && first != second)
      {
        double temperature;
        if(!cmsTempFromWhitePoint(&temperature, &white)) return DT_IMAGEIO_UNSUPPORTED_FEATURE;
        shot_weight = std::fmax(0.0, std::fmin(1.0,
            (1.0 / temperature - 1.0 / first) / (1.0 / second - 1.0 / first)));
      }
      double shot_matrix[9];
      calibrated_matrix(calibration, shot_weight, shot_matrix);
      const double xyz[3] = { xy[0] / xy[1], 1, (1 - xy[0] - xy[1]) / xy[1] };
      for(int row = 0; row < 3; row++)
        for(int col = 0; col < 3; col++) neutral[row] += shot_matrix[row * 3 + col] * xyz[col];
    }
    for(int c = 0; c < 3; c++)
      if(neutral[c] <= 0 || calibration.analog[c] <= 0)
        throw std::runtime_error("invalid DNG neutral or analog balance");
    double scale[2] = { 1, 1 };
    for(const auto &tag : data)
    {
      if(tag.tag() != 262 || tag.count() != 1 || tag.toLong() != 34892) continue;
      const std::string group = "Exif." + tag.groupName() + ".";
      const auto subfile = data.findKey(Exiv2::ExifKey(group + "NewSubfileType"));
      if(subfile == data.end() || subfile->count() != 1 || subfile->toLong() != 0) continue;
      read_values(data, (group + "DefaultScale").c_str(), scale, 2);
      break;
    }
    if(scale[0] <= 0 || scale[1] <= 0) throw std::runtime_error("invalid DNG default scale");
    for(int i = 0; i < 9; i++)
      if(!std::isfinite(matrix[i]) || std::fabs(matrix[i]) > FLT_MAX)
        throw std::runtime_error("DNG matrix exceeds float range");
    for(int c = 0; c < 3; c++)
      if(!std::isfinite(neutral[c]) || neutral[1] / neutral[c] > FLT_MAX
         || neutral[1] / neutral[c] < FLT_MIN)
        throw std::runtime_error("DNG white balance exceeds float range");
    if(scale[0] / scale[1] > FLT_MAX || scale[0] / scale[1] < FLT_MIN)
      throw std::runtime_error("DNG default scale exceeds float range");
    for(int c = 0; c < 3; c++) img->wb_coeffs[c] = neutral[1] / neutral[c];
    img->wb_coeffs[3] = img->wb_coeffs[1];
    for(int i = 0; i < 9; i++) img->d65_color_matrix[i] = matrix[i];
    img->pixel_aspect_ratio = scale[0] / scale[1];
    return DT_IMAGEIO_OK;
  }
  catch(const std::exception &error)
  {
    dt_print(DT_DEBUG_IMAGEIO, "[DNG color] %s: %s\n", filename, error.what());
    return DT_IMAGEIO_DECODE_FAILED;
  }
}
