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

#include <grub/bitmap.h>
#include <grub/types.h>
#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/bufio.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define dump_int_field(x) grub_dprintf ("tga", #x " = %d (0x%04x)\n", x, x);

enum
{
  GRUB_TGA_IMAGE_TYPE_NONE = 0,
  GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_INDEXCOLOR = 1,
  GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_TRUECOLOR = 2,
  GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_BLACK_AND_WHITE = 3,
  GRUB_TGA_IMAGE_TYPE_RLE_INDEXCOLOR = 9,
  GRUB_TGA_IMAGE_TYPE_RLE_TRUECOLOR = 10,
  GRUB_TGA_IMAGE_TYPE_RLE_BLACK_AND_WHITE = 11,
};

enum
{
  GRUB_TGA_COLOR_MAP_TYPE_NONE = 0,
  GRUB_TGA_COLOR_MAP_TYPE_INCLUDED = 1
};

enum
{
  GRUB_TGA_IMAGE_ORIGIN_RIGHT = 0x10,
  GRUB_TGA_IMAGE_ORIGIN_TOP   = 0x20
};

struct grub_tga_header
{
  grub_uint8_t id_length;
  grub_uint8_t color_map_type;
  grub_uint8_t image_type;

  /* Color Map Specification.  */
  grub_uint16_t color_map_first_index;
  grub_uint16_t color_map_length;
  grub_uint8_t color_map_bpp;

  /* Image Specification.  */
  grub_uint16_t image_x_origin;
  grub_uint16_t image_y_origin;
  grub_uint16_t image_width;
  grub_uint16_t image_height;
  grub_uint8_t image_bpp;
  grub_uint8_t image_descriptor;
} GRUB_PACKED;

struct tga_data
{
  struct grub_tga_header hdr;
  int uses_rle;
  int pktlen;
  int bpp;
  int is_rle;
  grub_uint8_t pixel[4];
  grub_uint8_t palette[256][3];
  struct grub_video_bitmap *bitmap;
  grub_file_t file;
  unsigned image_width;
  unsigned image_height;
};

static grub_err_t
fetch_pixel (struct tga_data *data)
{
  if (!data->uses_rle)
    {
      if (grub_file_read (data->file, &data->pixel[0], data->bpp)
	  != data->bpp)
	return grub_errno;
      return GRUB_ERR_NONE;
    }
  if (!data->pktlen)
    {
      grub_uint8_t type;
      if (grub_file_read (data->file, &type, sizeof (type)) != sizeof(type))
	return grub_errno;
      data->is_rle = !!(type & 0x80);
      data->pktlen = (type & 0x7f) + 1;
      if (data->is_rle && grub_file_read (data->file, &data->pixel[0], data->bpp)
	  != data->bpp)
	return grub_errno;
    }
  if (!data->is_rle && grub_file_read (data->file, &data->pixel[0], data->bpp)
      != data->bpp)
    return grub_errno;
  data->pktlen--;
  return GRUB_ERR_NONE;
}

static grub_err_t
tga_load_palette (struct tga_data *data)
{
  grub_size_t len = grub_le_to_cpu32 (data->hdr.color_map_length) * 3;

  if (len > sizeof (data->palette))
    len = sizeof (data->palette);
  
  if (grub_file_read (data->file, &data->palette, len)
      != (grub_ssize_t) len)
    return grub_errno;

#ifndef GRUB_CPU_WORDS_BIGENDIAN
  {
    grub_size_t i;
    for (i = 0; 3 * i < len; i++)
      {
	grub_uint8_t t;
	t = data->palette[i][0];
	data->palette[i][0] = data->palette[i][2];
	data->palette[i][2] = t;
      }
  }
#endif
  return GRUB_ERR_NONE;
}

static grub_err_t
tga_load_index_color (struct tga_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
    {
      ptr = data->bitmap->data;
      if ((data->hdr.image_descriptor & GRUB_TGA_IMAGE_ORIGIN_TOP) != 0)
        ptr += y * data->bitmap->mode_info.pitch;
      else
        ptr += (data->image_height - 1 - y) * data->bitmap->mode_info.pitch;

      for (x = 0; x < data->image_width; x++)
        {
	  grub_err_t err;
	  err = fetch_pixel (data);
          if (err)
            return err;

          ptr[0] = data->palette[data->pixel[0]][0];
          ptr[1] = data->palette[data->pixel[0]][1];
          ptr[2] = data->palette[data->pixel[0]][2];

          ptr += 3;
        }
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
tga_load_grayscale (struct tga_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
    {
      ptr = data->bitmap->data;
      if ((data->hdr.image_descriptor & GRUB_TGA_IMAGE_ORIGIN_TOP) != 0)
        ptr += y * data->bitmap->mode_info.pitch;
      else
        ptr += (data->image_height - 1 - y) * data->bitmap->mode_info.pitch;

      for (x = 0; x < data->image_width; x++)
        {
	  grub_err_t err;
	  err = fetch_pixel (data);
          if (err)
            return err;

          ptr[0] = data->pixel[0];
          ptr[1] = data->pixel[0];
          ptr[2] = data->pixel[0];

          ptr += 3;
        }
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
tga_load_truecolor_R8G8B8 (struct tga_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
    {
      ptr = data->bitmap->data;
      if ((data->hdr.image_descriptor & GRUB_TGA_IMAGE_ORIGIN_TOP) != 0)
        ptr += y * data->bitmap->mode_info.pitch;
      else
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
tga_load_truecolor_R8G8B8A8 (struct tga_data *data)
{
  unsigned int x;
  unsigned int y;
  grub_uint8_t *ptr;

  for (y = 0; y < data->image_height; y++)
    {
      ptr = data->bitmap->data;
      if ((data->hdr.image_descriptor & GRUB_TGA_IMAGE_ORIGIN_TOP) != 0)
        ptr += y * data->bitmap->mode_info.pitch;
      else
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
grub_video_reader_tga (struct grub_video_bitmap **bitmap,
                       const char *filename)
{
  grub_ssize_t pos;
  struct tga_data data;

  grub_memset (&data, 0, sizeof (data));

  data.file = grub_buffile_open (filename, GRUB_FILE_TYPE_PIXMAP, 0);
  if (! data.file)
    return grub_errno;

  /* TGA Specification states that we SHOULD start by reading
     ID from end of file, but we really don't care about that as we are
     not going to support developer area & extensions at this point.  */

  /* Read TGA header from beginning of file.  */
  if (grub_file_read (data.file, &data.hdr, sizeof (data.hdr))
      != sizeof (data.hdr))
    {
      grub_file_close (data.file);
      return grub_errno;
    }

  /* Skip ID field.  */
  pos = grub_file_tell (data.file);
  pos += data.hdr.id_length;
  grub_file_seek (data.file, pos);
  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_file_close (data.file);
      return grub_errno;
    }

  grub_dprintf("tga", "tga: header\n");
  dump_int_field(data.hdr.id_length);
  dump_int_field(data.hdr.color_map_type);
  dump_int_field(data.hdr.image_type);
  dump_int_field(data.hdr.color_map_first_index);
  dump_int_field(data.hdr.color_map_length);
  dump_int_field(data.hdr.color_map_bpp);
  dump_int_field(data.hdr.image_x_origin);
  dump_int_field(data.hdr.image_y_origin);
  dump_int_field(data.hdr.image_width);
  dump_int_field(data.hdr.image_height);
  dump_int_field(data.hdr.image_bpp);
  dump_int_field(data.hdr.image_descriptor);

  data.image_width = grub_le_to_cpu16 (data.hdr.image_width);
  data.image_height = grub_le_to_cpu16 (data.hdr.image_height);

  /* Check that bitmap encoding is supported.  */
  switch (data.hdr.image_type)
    {
    case GRUB_TGA_IMAGE_TYPE_RLE_TRUECOLOR:
    case GRUB_TGA_IMAGE_TYPE_RLE_BLACK_AND_WHITE:
    case GRUB_TGA_IMAGE_TYPE_RLE_INDEXCOLOR:
      data.uses_rle = 1;
      break;
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_TRUECOLOR:
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_BLACK_AND_WHITE:
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_INDEXCOLOR:
      data.uses_rle = 0;
      break;

    default:
      grub_file_close (data.file);
      return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			 "unsupported bitmap format (unknown encoding %d)", data.hdr.image_type);
    }

  data.bpp = data.hdr.image_bpp / 8;

  /* Check that bitmap depth is supported.  */
  switch (data.hdr.image_type)
    {
    case GRUB_TGA_IMAGE_TYPE_RLE_BLACK_AND_WHITE:
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_BLACK_AND_WHITE:
      if (data.hdr.image_bpp != 8)
	{
	  grub_file_close (data.file);
	  return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			     "unsupported bitmap format (bpp=%d)",
			     data.hdr.image_bpp);
	}
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
      tga_load_grayscale (&data);
      break;

    case GRUB_TGA_IMAGE_TYPE_RLE_INDEXCOLOR:
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_INDEXCOLOR:
      if (data.hdr.image_bpp != 8
	  || data.hdr.color_map_bpp != 24
	  || data.hdr.color_map_first_index != 0)
	{
	  grub_file_close (data.file);
	  return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			     "unsupported bitmap format (bpp=%d)",
			     data.hdr.image_bpp);
	}
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
      tga_load_palette (&data);
      tga_load_index_color (&data);
      break;

    case GRUB_TGA_IMAGE_TYPE_RLE_TRUECOLOR:
    case GRUB_TGA_IMAGE_TYPE_UNCOMPRESSED_TRUECOLOR:
      switch (data.hdr.image_bpp)
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
	  tga_load_truecolor_R8G8B8 (&data);
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
	  tga_load_truecolor_R8G8B8A8 (&data);
	  break;

	default:
	  grub_file_close (data.file);
	  return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			     "unsupported bitmap format (bpp=%d)",
			     data.hdr.image_bpp);
	}
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

static struct grub_video_bitmap_reader tga_reader = {
  .extension = ".tga",
  .reader = grub_video_reader_tga,
  .next = 0
};

GRUB_MOD_INIT(tga)
{
  grub_video_bitmap_reader_register (&tga_reader);
}

GRUB_MOD_FINI(tga)
{
  grub_video_bitmap_reader_unregister (&tga_reader);
}
