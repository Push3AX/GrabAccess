/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

#include <grub/bitmap.h>
#include <grub/types.h>
#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/bufio.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_bmp_header
{
  char char_b;
  char char_m;
  grub_uint32_t size;
  grub_uint16_t reserved[2];
  grub_uint32_t image_offset;
  grub_uint32_t header_size;
  grub_uint32_t pixel_width;
  grub_uint32_t pixel_height;
  grub_uint16_t planes;
  grub_uint16_t bit_per_pixel;
  grub_uint32_t compression_type;
  grub_uint32_t image_size;
  grub_uint32_t x_pixels_per_meter;
  grub_uint32_t y_pixels_per_meter;
  grub_uint32_t number_of_colors;
  grub_uint32_t important_colors;
} GRUB_PACKED;

struct bmp_data
{
  struct grub_bmp_header hdr;
  int bpp;
  grub_uint8_t pixel[4];
  struct grub_video_bitmap *bitmap;
  grub_file_t file;
  unsigned image_width;
  unsigned image_height;
};

static grub_err_t
fetch_pixel (struct bmp_data *data)
{
  if (grub_file_read (data->file, &data->pixel[0], data->bpp) != data->bpp)
    return grub_errno;

  return GRUB_ERR_NONE;
}

static grub_err_t
bmp_load_24 (struct bmp_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
  {
    ptr = data->bitmap->data;
    ptr += (data->image_height - 1 - y) * data->bitmap->mode_info.pitch;

    for (x = 0; x < data->image_width; x++)
    {
      grub_err_t err;
      err = fetch_pixel (data);
      if (err)
        return err;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
      ptr[0] = data->pixel[0];
      ptr[1] = data->pixel[1];
      ptr[2] = data->pixel[2];
#else
      ptr[0] = data->pixel[2];
      ptr[1] = data->pixel[1];
      ptr[2] = data->pixel[0];
#endif
      ptr += 3;
    }
  }
  return GRUB_ERR_NONE;
}

static grub_err_t
bmp_load_32 (struct bmp_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
  {
    ptr = data->bitmap->data;
    ptr += (data->image_height - 1 - y) * data->bitmap->mode_info.pitch;

    for (x = 0; x < data->image_width; x++)
    {
      grub_err_t err;
      err = fetch_pixel (data);
      if (err)
        return err;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
      ptr[0] = data->pixel[0];
      ptr[1] = data->pixel[1];
      ptr[2] = data->pixel[2];
      ptr[3] = data->pixel[3];
#else
      ptr[0] = data->pixel[2];
      ptr[1] = data->pixel[1];
      ptr[2] = data->pixel[0];
      ptr[3] = data->pixel[3];
#endif
      ptr += 4;
    }
  }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_reader_bmp (struct grub_video_bitmap **bitmap,
                       const char *filename)
{
  grub_ssize_t pos;
  struct bmp_data data;

  grub_memset (&data, 0, sizeof (data));

  data.file = grub_buffile_open (filename, GRUB_FILE_TYPE_PIXMAP, 0);
  if (! data.file)
    return grub_errno;

  /* Read BMP header from beginning of file.  */
  if (grub_file_read (data.file, &data.hdr, sizeof (data.hdr))
      != sizeof (data.hdr))
  {
    grub_file_close (data.file);
    return grub_errno;
  }

  pos = data.hdr.image_offset;
  grub_file_seek (data.file, pos);
  if (grub_errno != GRUB_ERR_NONE)
  {
    grub_file_close (data.file);
    return grub_errno;
  }

  data.image_width = grub_le_to_cpu16 (data.hdr.pixel_width);
  data.image_height = grub_le_to_cpu16 (data.hdr.pixel_height);

  data.bpp = data.hdr.bit_per_pixel / 8;

  /* Check that bitmap depth is supported.  */
  switch (data.hdr.bit_per_pixel)
  {
    case 24:
      grub_video_bitmap_create (bitmap, data.image_width,
                                data.image_height,
                                GRUB_VIDEO_BLIT_FORMAT_RGB_888);
      if (grub_errno != GRUB_ERR_NONE)
      {
        grub_file_close (data.file);
        return grub_errno;
      }

      data.bitmap = *bitmap;
      /* Load bitmap data.  */
      bmp_load_24 (&data);
      break;

    case 32:
      grub_video_bitmap_create (bitmap, data.image_width,
                                data.image_height,
                                GRUB_VIDEO_BLIT_FORMAT_RGBA_8888);
      if (grub_errno != GRUB_ERR_NONE)
      {
        grub_file_close (data.file);
        return grub_errno;
      }

      data.bitmap = *bitmap;
      /* Load bitmap data.  */
      bmp_load_32 (&data);
      break;

    default:
      grub_file_close (data.file);
      return grub_error (GRUB_ERR_BAD_FILE_TYPE,
                         "unsupported bitmap format (bpp=%d)",
                         data.hdr.bit_per_pixel);
  }

  /* If there was a loading problem, destroy bitmap.  */
  if (grub_errno != GRUB_ERR_NONE)
  {
    grub_video_bitmap_destroy (*bitmap);
    *bitmap = 0;
  }

  grub_file_close (data.file);
  return grub_errno;
}

static struct grub_video_bitmap_reader bmp_reader =
{
  .extension = ".bmp",
  .reader = grub_video_reader_bmp,
  .next = 0
};

GRUB_MOD_INIT(bmp)
{
  grub_video_bitmap_reader_register (&bmp_reader);
}

GRUB_MOD_FINI(bmp)
{
  grub_video_bitmap_reader_unregister (&bmp_reader);
}
