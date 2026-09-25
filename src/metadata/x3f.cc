#include "metadata/x3f.h"
#include "external/x3f-rust/ffi/ansel_x3f.h"
#include "system/macros.h"

#include <cstring>
#include <glib.h>
#include <stdexcept>

std::unique_ptr<Exiv2::Image> dt_exif_open_x3f(const char *filename)
{
  const char *extension = strrchr(filename, '.');
  if(IS_NULL_PTR(extension) || g_ascii_strcasecmp(extension, ".x3f")) return nullptr;

  std::unique_ptr<GMappedFile, decltype(&g_mapped_file_unref)> file(g_mapped_file_new(filename, FALSE, nullptr),
                                                                    &g_mapped_file_unref);
  if(IS_NULL_PTR(file.get())) throw std::runtime_error("cannot map X3F metadata");
  const uint8_t *preview = nullptr;
  size_t size = 0;
  if(ansel_x3f_preview((const uint8_t *)g_mapped_file_get_contents(file.get()),
                       g_mapped_file_get_length(file.get()), &preview, &size)
     != 0)
    throw std::runtime_error("cannot locate X3F metadata preview");

#if EXIV2_TEST_VERSION(0, 28, 0)
  Exiv2::BasicIo::UniquePtr memory(new Exiv2::MemIo);
#else
  Exiv2::BasicIo::AutoPtr memory(new Exiv2::MemIo);
#endif
  if(memory->write(preview, size) != (long)size) throw std::runtime_error("cannot copy X3F metadata preview");
  memory->seek(0, Exiv2::BasicIo::beg);
  return std::unique_ptr<Exiv2::Image>(Exiv2::ImageFactory::open(std::move(memory)));
}
