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

#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/safemath.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* List of bitmap readers registered to system.  */
static grub_video_bitmap_reader_t bitmap_readers_list;

/* Register bitmap reader.  */
void
grub_video_bitmap_reader_register (grub_video_bitmap_reader_t reader)
{
  reader->next = bitmap_readers_list;
  bitmap_readers_list = reader;
}

/* Unregister bitmap reader.  */
void
grub_video_bitmap_reader_unregister (grub_video_bitmap_reader_t reader)
{
  grub_video_bitmap_reader_t *p, q;

  for (p = &bitmap_readers_list, q = *p; q; p = &(q->next), q = q->next)
    if (q == reader)
      {
        *p = q->next;
        break;
      }
}

/* Creates new bitmap, saves created bitmap on success to *bitmap.  */
grub_err_t
grub_video_bitmap_create (struct grub_video_bitmap **bitmap,
                          unsigned int width, unsigned int height,
                          enum grub_video_blit_format blit_format)
{
  struct grub_video_mode_info *mode_info;
  grub_size_t size;

  if (!bitmap)
    return grub_error (GRUB_ERR_BUG, "invalid argument");

  *bitmap = 0;

  if (width == 0 || height == 0)
    return grub_error (GRUB_ERR_BUG, "invalid argument");

  *bitmap = (struct grub_video_bitmap *)grub_malloc (sizeof (struct grub_video_bitmap));
  if (! *bitmap)
    return grub_errno;

  mode_info = &((*bitmap)->mode_info);

  /* Populate mode_info.  */
  mode_info->width = width;
  mode_info->height = height;
  mode_info->blit_format = blit_format;

  switch (blit_format)
    {
      case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
        mode_info->mode_type = GRUB_VIDEO_MODE_TYPE_RGB
                               | GRUB_VIDEO_MODE_TYPE_ALPHA;
        mode_info->bpp = 32;
        mode_info->bytes_per_pixel = 4;
        mode_info->number_of_colors = 256;
        mode_info->red_mask_size = 8;
        mode_info->red_field_pos = 0;
        mode_info->green_mask_size = 8;
        mode_info->green_field_pos = 8;
        mode_info->blue_mask_size = 8;
        mode_info->blue_field_pos = 16;
        mode_info->reserved_mask_size = 8;
        mode_info->reserved_field_pos = 24;
        break;

      case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
        mode_info->mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
        mode_info->bpp = 24;
        mode_info->bytes_per_pixel = 3;
        mode_info->number_of_colors = 256;
        mode_info->red_mask_size = 8;
        mode_info->red_field_pos = 0;
        mode_info->green_mask_size = 8;
        mode_info->green_field_pos = 8;
        mode_info->blue_mask_size = 8;
        mode_info->blue_field_pos = 16;
        mode_info->reserved_mask_size = 0;
        mode_info->reserved_field_pos = 0;
        break;

      case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
        mode_info->mode_type = GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
        mode_info->bpp = 8;
        mode_info->bytes_per_pixel = 1;
        mode_info->number_of_colors = 256;
        mode_info->red_mask_size = 0;
        mode_info->red_field_pos = 0;
        mode_info->green_mask_size = 0;
        mode_info->green_field_pos = 0;
        mode_info->blue_mask_size = 0;
        mode_info->blue_field_pos = 0;
        mode_info->reserved_mask_size = 0;
        mode_info->reserved_field_pos = 0;
        break;

      default:
        grub_free (*bitmap);
        *bitmap = 0;

        return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                           "unsupported bitmap format");
    }

  mode_info->pitch = width * mode_info->bytes_per_pixel;

  /* Calculate size needed for the data. */
  if (grub_mul (width, mode_info->bytes_per_pixel, &size) ||
      grub_mul (size, height, &size))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));
      goto fail;
    }

  (*bitmap)->data = grub_zalloc (size);
  if (! (*bitmap)->data)
    goto fail;

  return GRUB_ERR_NONE;

 fail:
  grub_free (*bitmap);
  *bitmap = NULL;

  return grub_errno;
}

/* Frees all resources allocated by bitmap.  */
grub_err_t
grub_video_bitmap_destroy (struct grub_video_bitmap *bitmap)
{
  if (! bitmap)
    return GRUB_ERR_NONE;

  grub_free (bitmap->data);
  grub_free (bitmap);

  return GRUB_ERR_NONE;
}

/* Match extension to filename.  */
static int
match_extension (const char *filename, const char *ext)
{
  int pos;
  int ext_len;

  pos = grub_strlen (filename);
  ext_len = grub_strlen (ext);

  if (! pos || ! ext_len || ext_len > pos)
    return 0;

  pos -= ext_len;

  return grub_strcasecmp (filename + pos, ext) == 0;
}

/* Loads bitmap using registered bitmap readers.  */
grub_err_t
grub_video_bitmap_load (struct grub_video_bitmap **bitmap,
                        const char *filename)
{
  grub_video_bitmap_reader_t reader = bitmap_readers_list;

  if (!bitmap)
    return grub_error (GRUB_ERR_BUG, "invalid argument");

  *bitmap = 0;

  while (reader)
    {
      if (match_extension (filename, reader->extension))
        return reader->reader (bitmap, filename);

      reader = reader->next;
    }

  return grub_error (GRUB_ERR_BAD_FILE_TYPE,
		     /* TRANSLATORS: We're speaking about bitmap images like
			JPEG or PNG.  */
		     N_("bitmap file `%s' is of"
			" unsupported format"), filename);
}

/* Return mode info for bitmap.  */
void grub_video_bitmap_get_mode_info (struct grub_video_bitmap *bitmap,
                                      struct grub_video_mode_info *mode_info)
{
  if (!bitmap)
    return;

  *mode_info = bitmap->mode_info;
}

/* Return pointer to bitmap's raw data.  */
void *grub_video_bitmap_get_data (struct grub_video_bitmap *bitmap)
{
  if (!bitmap)
    return 0;

  return bitmap->data;
}

