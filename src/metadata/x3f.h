#ifndef DT_METADATA_X3F_H
#define DT_METADATA_X3F_H

#include <exiv2/exiv2.hpp>
#include <memory>

/** Open an owned in-memory camera JPEG for X3F metadata reads. Returns null for
 * other extensions; malformed X3F raises an exception caught by the EXIF caller.
 * The original file is never modified. Sensor decoding is independent of this path. */
std::unique_ptr<Exiv2::Image> dt_exif_open_x3f(const char *filename);

#endif
