/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GRUB_BITMAP_HEADER
#define GRUB_BITMAP_HEADER	1

#include <grub/err.h>
#include <grub/symbol.h>
#include <grub/types.h>
#include <grub/video.h>

struct grub_video_bitmap
{
  /* Bitmap format description.  */
  struct grub_video_mode_info mode_info;

  /* Pointer to bitmap data formatted according to mode_info.  */
  void *data;
};

struct grub_video_bitmap_reader
{
  /* File extension for this bitmap type (including dot).  */
  const char *extension;

  /* Reader function to load bitmap.  */
  grub_err_t (*reader) (struct grub_video_bitmap **bitmap,
                        const char *filename);

  /* Next reader.  */
  struct grub_video_bitmap_reader *next;
};
typedef struct grub_video_bitmap_reader *grub_video_bitmap_reader_t;

void EXPORT_FUNC (grub_video_bitmap_reader_register) (grub_video_bitmap_reader_t reader);
void EXPORT_FUNC (grub_video_bitmap_reader_unregister) (grub_video_bitmap_reader_t reader);

grub_err_t EXPORT_FUNC (grub_video_bitmap_create) (struct grub_video_bitmap **bitmap,
						   unsigned int width, unsigned int height,
						   enum grub_video_blit_format blit_format);

grub_err_t EXPORT_FUNC (grub_video_bitmap_destroy) (struct grub_video_bitmap *bitmap);

grub_err_t EXPORT_FUNC (grub_video_bitmap_load) (struct grub_video_bitmap **bitmap,
						 const char *filename);

/* Return bitmap width.  */
static inline unsigned int
grub_video_bitmap_get_width (struct grub_video_bitmap *bitmap)
{
  if (!bitmap)
    return 0;

  return bitmap->mode_info.width;
}

/* Return bitmap height.  */
static inline unsigned int
grub_video_bitmap_get_height (struct grub_video_bitmap *bitmap)
{
  if (!bitmap)
    return 0;

  return bitmap->mode_info.height;
}

/* Originally placed in the "bitmap_scale.c".  */
static inline grub_err_t
verify_source_bitmap (struct grub_video_bitmap *src)
{
  /* Verify the simplifying assumptions. */
  if (src == 0)
    return grub_error (GRUB_ERR_BUG,
		       "null src bitmap in grub_video_bitmap_create_scaled");
  if (src->mode_info.red_field_pos % 8 != 0
      || src->mode_info.green_field_pos % 8 != 0
      || src->mode_info.blue_field_pos % 8 != 0
      || src->mode_info.reserved_field_pos % 8 != 0)
    return grub_error (GRUB_ERR_BUG, "src format not supported for scale");
  if (src->mode_info.width == 0 || src->mode_info.height == 0)
    return grub_error (GRUB_ERR_BUG, "source bitmap has a zero dimension");
  if (src->mode_info.bytes_per_pixel * 8 != src->mode_info.bpp)
    return grub_error (GRUB_ERR_BUG,
		       "bitmap to scale has inconsistent Bpp and bpp");
  return GRUB_ERR_NONE;
}

/* Originally placed in the "bitmap_scale.c".  */
static inline grub_err_t
verify_bitmaps (struct grub_video_bitmap *dst, struct grub_video_bitmap *src)
{
  /* Verify the simplifying assumptions. */
  if (dst == 0 || src == 0)
    return grub_error (GRUB_ERR_BUG, "null bitmap in scale function");
  if (dst->mode_info.red_field_pos % 8 != 0
      || dst->mode_info.green_field_pos % 8 != 0
      || dst->mode_info.blue_field_pos % 8 != 0
      || dst->mode_info.reserved_field_pos % 8 != 0)
    return grub_error (GRUB_ERR_BUG, "dst format not supported");
  if (src->mode_info.red_field_pos % 8 != 0
      || src->mode_info.green_field_pos % 8 != 0
      || src->mode_info.blue_field_pos % 8 != 0
      || src->mode_info.reserved_field_pos % 8 != 0)
    return grub_error (GRUB_ERR_BUG, "src format not supported");
  if (dst->mode_info.red_field_pos != src->mode_info.red_field_pos
      || dst->mode_info.red_mask_size != src->mode_info.red_mask_size
      || dst->mode_info.green_field_pos != src->mode_info.green_field_pos
      || dst->mode_info.green_mask_size != src->mode_info.green_mask_size
      || dst->mode_info.blue_field_pos != src->mode_info.blue_field_pos
      || dst->mode_info.blue_mask_size != src->mode_info.blue_mask_size
      || dst->mode_info.reserved_field_pos != src->mode_info.reserved_field_pos
      || dst->mode_info.reserved_mask_size != src->mode_info.reserved_mask_size)
    return grub_error (GRUB_ERR_BUG, "dst and src not compatible");
  if (dst->mode_info.bytes_per_pixel != src->mode_info.bytes_per_pixel)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "dst and src not compatible");
  if (dst->mode_info.width == 0 || dst->mode_info.height == 0
      || src->mode_info.width == 0 || src->mode_info.height == 0)
    return grub_error (GRUB_ERR_BUG, "bitmap has a zero dimension");

  return GRUB_ERR_NONE;
}

void EXPORT_FUNC (grub_video_bitmap_get_mode_info) (struct grub_video_bitmap *bitmap,
						    struct grub_video_mode_info *mode_info);

void *EXPORT_FUNC (grub_video_bitmap_get_data) (struct grub_video_bitmap *bitmap);

#endif /* ! GRUB_BITMAP_HEADER */
