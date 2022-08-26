/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2009  Free Software Foundation, Inc.
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

/* SPECIAL NOTES!

   Please note following when reading the code below:

   - In this driver we assume that every memory can be accessed by same memory
     bus.  If there are different address spaces do not use this code as a base
     code for other archs.

   - Every function in this code assumes that bounds checking has been done in
     previous phase and they are opted out in here.  */

#include <grub/fbutil.h>
#include <grub/types.h>
#include <grub/video.h>

grub_video_color_t
get_pixel (struct grub_video_fbblit_info *source,
           unsigned int x, unsigned int y)
{
  grub_video_color_t color = 0;

  switch (source->mode_info->bpp)
    {
    case 32:
      color = *(grub_uint32_t *)grub_video_fb_get_video_ptr (source, x, y);
      break;

    case 24:
      {
        grub_uint8_t *ptr;
        ptr = grub_video_fb_get_video_ptr (source, x, y);
#ifdef GRUB_CPU_WORDS_BIGENDIAN
        color = ptr[2] | (ptr[1] << 8) | (ptr[0] << 16);
#else
        color = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
#endif
      }
      break;

    case 16:
    case 15:
      color = *(grub_uint16_t *)grub_video_fb_get_video_ptr (source, x, y);
      break;

    case 8:
      color = *(grub_uint8_t *)grub_video_fb_get_video_ptr (source, x, y);
      break;

    case 1:
      if (source->mode_info->blit_format == GRUB_VIDEO_BLIT_FORMAT_1BIT_PACKED)
        {
          int bit_index = y * source->mode_info->width + x;
          grub_uint8_t *ptr = source->data + bit_index / 8;
          int bit_pos = 7 - bit_index % 8;
          color = (*ptr >> bit_pos) & 0x01;
        }
      break;

    default:
      break;
    }

  return color;
}

void
set_pixel (struct grub_video_fbblit_info *source,
           unsigned int x, unsigned int y, grub_video_color_t color)
{
  switch (source->mode_info->bpp)
    {
    case 32:
      {
        grub_uint32_t *ptr;

        ptr = (grub_uint32_t *)grub_video_fb_get_video_ptr (source, x, y);

        *ptr = color;
      }
      break;

    case 24:
      {
        grub_uint8_t *ptr;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
        grub_uint8_t *colorptr = ((grub_uint8_t *)&color) + 1;
#else
        grub_uint8_t *colorptr = (grub_uint8_t *)&color;
#endif

        ptr = grub_video_fb_get_video_ptr (source, x, y);

        ptr[0] = colorptr[0];
        ptr[1] = colorptr[1];
        ptr[2] = colorptr[2];
      }
      break;

    case 16:
    case 15:
      {
        grub_uint16_t *ptr;

        ptr = (grub_uint16_t *)grub_video_fb_get_video_ptr (source, x, y);

        *ptr = (grub_uint16_t) (color & 0xFFFF);
      }
      break;

    case 8:
      {
        grub_uint8_t *ptr;

        ptr = (grub_uint8_t *)grub_video_fb_get_video_ptr (source, x, y);

        *ptr = (grub_uint8_t) (color & 0xFF);
      }
      break;

    case 1:
      if (source->mode_info->blit_format == GRUB_VIDEO_BLIT_FORMAT_1BIT_PACKED)
        {
          int bit_index = y * source->mode_info->width + x;
          grub_uint8_t *ptr = source->data + bit_index / 8;
          int bit_pos = 7 - bit_index % 8;
          *ptr = (*ptr & ~(1 << bit_pos)) | ((color & 0x01) << bit_pos);
        }
      break;

    default:
      break;
    }
}
