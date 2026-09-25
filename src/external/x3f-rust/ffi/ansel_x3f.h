#ifndef ANSEL_X3F_H
#define ANSEL_X3F_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ansel_x3f_image ansel_x3f_image;

typedef struct ansel_x3f_info
{
  uint32_t width;
  uint32_t height;
  const uint8_t *exif;
  size_t exif_size;
} ansel_x3f_info;

/** Borrow the camera JPEG for metadata or thumbnails. The returned storage belongs
 * to the input mapping and expires with it. Returns zero on success. */
int32_t ansel_x3f_preview(const uint8_t *data, size_t length, const uint8_t **preview, size_t *preview_length);

/** Decode to experimental linear sRGB. Returns zero on success. Input storage is
 * borrowed only during the call. The caller releases *output with ansel_x3f_free.
 * All output pointers must refer to writable, nonoverlapping storage. The EXIF
 * pointer in info is borrowed from the handle and expires when it is released. */
int32_t ansel_x3f_decode(const uint8_t *data, size_t length, ansel_x3f_image **output,
                        ansel_x3f_info *info, char *error, size_t error_capacity);

/** Copy to width*height*4 caller-owned floats without clipping. Alpha is one.
 * The image handle must remain live and the destination must not overlap it. */
int32_t ansel_x3f_copy_rgba(const ansel_x3f_image *image, float *destination, size_t float_count);

/** Release exactly once after all reads. Null is accepted. */
void ansel_x3f_free(ansel_x3f_image *image);

#ifdef __cplusplus
}
#endif

#endif
