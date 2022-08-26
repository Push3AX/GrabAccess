/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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

#include <grub/video_fb.h>
#include <grub/fbfill.h>
#include <grub/fbutil.h>
#include <grub/types.h>
#include <grub/safemath.h>
#include <grub/video.h>

/* Generic filler that works for every supported mode.  */
static void
grub_video_fbfill (struct grub_video_fbblit_info *dst,
		   grub_video_color_t color, int x, int y,
		   unsigned int width, unsigned int height)
{
  unsigned int i, j;

  for (j = 0; j < height; j++)
    for (i = 0; i < width; i++)
      set_pixel (dst, x + i, y + j, color);
}

/* Optimized filler for direct color 32 bit modes.  It is assumed that color
   is already mapped to destination format.  */
static void
grub_video_fbfill_direct32 (struct grub_video_fbblit_info *dst,
			    grub_video_color_t color, int x, int y,
			    unsigned int width, unsigned int height)
{
  unsigned int i, j;
  grub_uint32_t *dstptr;
  grub_size_t rowskip;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  if (grub_mul (dst->mode_info->bytes_per_pixel, width, &rowskip) ||
      grub_sub (dst->mode_info->pitch, rowskip, &rowskip))
    return;

  /* Get the start address.  */
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        *dstptr++ = color;

      /* Advance the dest pointer to the right location on the next line.  */
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, rowskip);
    }
}

/* Optimized filler for direct color 24 bit modes.  It is assumed that color
   is already mapped to destination format.  */
static void
grub_video_fbfill_direct24 (struct grub_video_fbblit_info *dst,
			    grub_video_color_t color, int x, int y,
			    unsigned int width, unsigned int height)
{
  unsigned int i, j;
  grub_size_t rowskip;
  grub_uint8_t *dstptr;
#ifndef GRUB_CPU_WORDS_BIGENDIAN
  grub_uint8_t fill0 = (grub_uint8_t)((color >> 0) & 0xFF);
  grub_uint8_t fill1 = (grub_uint8_t)((color >> 8) & 0xFF);
  grub_uint8_t fill2 = (grub_uint8_t)((color >> 16) & 0xFF);
#else
  grub_uint8_t fill2 = (grub_uint8_t)((color >> 0) & 0xFF);
  grub_uint8_t fill1 = (grub_uint8_t)((color >> 8) & 0xFF);
  grub_uint8_t fill0 = (grub_uint8_t)((color >> 16) & 0xFF);
#endif
  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  if (grub_mul (dst->mode_info->bytes_per_pixel, width, &rowskip) ||
      grub_sub (dst->mode_info->pitch, rowskip, &rowskip))
    return;

  /* Get the start address.  */
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          *dstptr++ = fill0;
          *dstptr++ = fill1;
          *dstptr++ = fill2;
        }

      /* Advance the dest pointer to the right location on the next line.  */
      dstptr += rowskip;
    }
}

/* Optimized filler for direct color 16 bit modes.  It is assumed that color
   is already mapped to destination format.  */
static void
grub_video_fbfill_direct16 (struct grub_video_fbblit_info *dst,
			    grub_video_color_t color, int x, int y,
			    unsigned int width, unsigned int height)
{
  unsigned int i, j;
  grub_size_t rowskip;
  grub_uint16_t *dstptr;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  if (grub_mul (dst->mode_info->bytes_per_pixel, width, &rowskip) ||
      grub_sub (dst->mode_info->pitch, rowskip, &rowskip))
    return;

  /* Get the start address.  */
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
	*dstptr++ = color;

      /* Advance the dest pointer to the right location on the next line.  */
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, rowskip);
    }
}

/* Optimized filler for index color.  It is assumed that color
   is already mapped to destination format.  */
static void
grub_video_fbfill_direct8 (struct grub_video_fbblit_info *dst,
			   grub_video_color_t color, int x, int y,
			   unsigned int width, unsigned int height)
{
  unsigned int i, j;
  grub_size_t rowskip;
  grub_uint8_t *dstptr;
  grub_uint8_t fill = (grub_uint8_t)color & 0xFF;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  if (grub_mul (dst->mode_info->bytes_per_pixel, width, &rowskip) ||
      grub_sub (dst->mode_info->pitch, rowskip, &rowskip))
    return;

  /* Get the start address.  */
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        *dstptr++ = fill;

      /* Advance the dest pointer to the right location on the next line.  */
      dstptr += rowskip;
    }
}

void
grub_video_fb_fill_dispatch (struct grub_video_fbblit_info *target,
			     grub_video_color_t color, int x, int y,
			     unsigned int width, unsigned int height)
{
  /* Try to figure out more optimized version.  Note that color is already
     mapped to target format so we can make assumptions based on that.  */
  switch (target->mode_info->bytes_per_pixel)
    {
    case 4:
      grub_video_fbfill_direct32 (target, color, x, y,
				  width, height);
      return;
    case 3:
      grub_video_fbfill_direct24 (target, color, x, y,
				  width, height);
      return;
    case 2:
      grub_video_fbfill_direct16 (target, color, x, y,
                                        width, height);
      return;
    case 1:
      grub_video_fbfill_direct8 (target, color, x, y,
				       width, height);
      return;
    }

  /* No optimized version found, use default (slow) filler.  */
  grub_video_fbfill (target, color, x, y, width, height);
}
