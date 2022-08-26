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
#include <grub/fbblit.h>
#include <grub/fbutil.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/video.h>

/* Generic replacing blitter (slow).  Works for every supported format.  */
static void
grub_video_fbblit_replace (struct grub_video_fbblit_info *dst,
			   struct grub_video_fbblit_info *src,
			   int x, int y, int width, int height,
			   int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t src_red;
  grub_uint8_t src_green;
  grub_uint8_t src_blue;
  grub_uint8_t src_alpha;
  grub_video_color_t src_color;
  grub_video_color_t dst_color;

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
	{
	  src_color = get_pixel (src, i + offset_x, j + offset_y);

	  grub_video_fb_unmap_color_int (src, src_color, &src_red, &src_green,
					 &src_blue, &src_alpha);

	  dst_color = grub_video_fb_map_rgba (src_red, src_green,
					      src_blue, src_alpha);

	  set_pixel (dst, x + i, y + j, dst_color);
	}
    }
}

/* Block copy replacing blitter.  Works with modes multiple of 8 bits.  */
static void
grub_video_fbblit_replace_directN (struct grub_video_fbblit_info *dst,
				   struct grub_video_fbblit_info *src,
				   int x, int y, int width, int height,
				   int offset_x, int offset_y)
{
  int j;
  grub_uint32_t *srcptr;
  grub_uint32_t *dstptr;
  int bpp;

  bpp = src->mode_info->bytes_per_pixel;
  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      grub_memmove (dstptr, srcptr, width * bpp);
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, src->mode_info->pitch);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dst->mode_info->pitch);
    }
}

/* Optimized replacing blitter for 1-bit to 32bit.  */
static void
grub_video_fbblit_replace_32bit_1bit (struct grub_video_fbblit_info *dst,
				      struct grub_video_fbblit_info *src,
				      int x, int y,
				      int width, int height,
				      int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint32_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint32_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr & srcmask)
	    *dstptr = fgcolor;
	  else
	    *dstptr = bgcolor;
	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr++;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}


#ifdef GRUB_HAVE_UNALIGNED_ACCESS
/* Optimized replacing blitter for 1-bit to 24-bit.  */
static void
grub_video_fbblit_replace_24bit_1bit (struct grub_video_fbblit_info *dst,
				      struct grub_video_fbblit_info *src,
				      int x, int y,
				      int width, int height,
				      int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint32_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width - 1; i++)
        {
	  if (*srcptr & srcmask)
	    *(grub_uint32_t *) dstptr = fgcolor;
	  else
	    *(grub_uint32_t *) dstptr = bgcolor;
	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr += 3;
        }

      if (*srcptr & srcmask)
	{
	  *dstptr++ = fgcolor & 0xff;
	  *dstptr++ = (fgcolor & 0xff00) >> 8;
	  *dstptr++ = (fgcolor & 0xff0000) >> 16;
	}
      else
	{
	  *dstptr++ = bgcolor & 0xff;
	  *dstptr++ = (bgcolor & 0xff00) >> 8;
	  *dstptr++ = (bgcolor & 0xff0000) >> 16;
	}
      srcmask >>= 1;
      if (!srcmask)
	{
	  srcptr++;
	  srcmask = 0x80;
	}

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      dstptr += dstrowskip;
    }
}
#endif

/* Optimized replacing blitter for 1-bit to 16-bit.  */
static void
grub_video_fbblit_replace_16bit_1bit (struct grub_video_fbblit_info *dst,
				      struct grub_video_fbblit_info *src,
				      int x, int y,
				      int width, int height,
				      int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint16_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint16_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr & srcmask)
	    *dstptr = fgcolor;
	  else
	    *dstptr = bgcolor;
	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr++;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for 1-bit to 8-bit.  */
static void
grub_video_fbblit_replace_8bit_1bit (struct grub_video_fbblit_info *dst,
				      struct grub_video_fbblit_info *src,
				      int x, int y,
				      int width, int height,
				      int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint8_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr & srcmask)
	    *dstptr = fgcolor;
	  else
	    *dstptr = bgcolor;
	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr++;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      dstptr += dstrowskip;
    }
}

static void
grub_video_fbblit_replace_32bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint32_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint32_t palette[17];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);
  palette[16] = grub_video_fb_map_rgba (0, 0, 0, 0);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr == 0xf0)
	    *dstptr = palette[16];
	  else
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for 1-bit to 16bit.  */
static void
grub_video_fbblit_replace_24bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint32_t palette[17];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);
  palette[16] = grub_video_fb_map_rgba (0, 0, 0, 0);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  register grub_uint32_t col;
	  if (*srcptr == 0xf0)	      
	    col = palette[16];
	  else
	    col = palette[*srcptr & 0xf];
#ifdef GRUB_CPU_WORDS_BIGENDIAN
	  *dstptr++ = col >> 16;
	  *dstptr++ = col >> 8;
	  *dstptr++ = col >> 0;
#else
	  *dstptr++ = col >> 0;
	  *dstptr++ = col >> 8;
	  *dstptr++ = col >> 16;
#endif	  
	  srcptr++;
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for 1-bit to 16bit.  */
static void
grub_video_fbblit_replace_16bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint16_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint16_t palette[17];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);
  palette[16] = grub_video_fb_map_rgba (0, 0, 0, 0);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr == 0xf0)
	    *dstptr = palette[16];
	  else
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for 1-bit to 8bit.  */
static void
grub_video_fbblit_replace_8bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint8_t palette[17];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);
  palette[16] = grub_video_fb_map_rgba (0, 0, 0, 0);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr == 0xf0)
	    *dstptr = palette[16];
	  else
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}


static void
grub_video_fbblit_blend_32bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint32_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint32_t palette[16];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr != 0xf0)
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for 1-bit to 16bit.  */
static void
grub_video_fbblit_blend_24bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint32_t palette[16];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  register grub_uint32_t col;
	  if (*srcptr != 0xf0)	      
	    {
	      col = palette[*srcptr & 0xf];
#ifdef GRUB_CPU_WORDS_BIGENDIAN
	      *dstptr++ = col >> 16;
	      *dstptr++ = col >> 8;
	      *dstptr++ = col >> 0;
#else
	      *dstptr++ = col >> 0;
	      *dstptr++ = col >> 8;
	      *dstptr++ = col >> 16;
#endif	  
	    }
	  else
	    dstptr += 3;
	  srcptr++;
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for 1-bit to 16bit.  */
static void
grub_video_fbblit_blend_16bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint16_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint16_t palette[17];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr != 0xf0)
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for 1-bit to 8bit.  */
static void
grub_video_fbblit_blend_8bit_indexa (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int dstrowskip;
  unsigned int srcrowskip;
  grub_uint8_t palette[16];

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskip = src->mode_info->pitch - width;

  for (i = 0; i < 16; i++)
    palette[i] = grub_video_fb_map_color (i);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  if (*srcptr != 0xf0)
	    *dstptr = palette[*srcptr & 0xf];
	  srcptr++;
	  dstptr++;
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}


/* Optimized replacing blitter for RGBX8888 to BGRX8888.  */
static void
grub_video_fbblit_replace_BGRX8888_RGBX8888 (struct grub_video_fbblit_info *dst,
					     struct grub_video_fbblit_info *src,
					     int x, int y,
					     int width, int height,
					     int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
#ifdef GRUB_CPU_WORDS_BIGENDIAN
          grub_uint8_t a = *srcptr++;
#endif
          grub_uint8_t r = *srcptr++;
          grub_uint8_t g = *srcptr++;
          grub_uint8_t b = *srcptr++;
#ifndef GRUB_CPU_WORDS_BIGENDIAN
          grub_uint8_t a = *srcptr++;
#endif

#ifdef GRUB_CPU_WORDS_BIGENDIAN
          *dstptr++ = a;
#endif
          *dstptr++ = b;
          *dstptr++ = g;
          *dstptr++ = r;
#ifndef GRUB_CPU_WORDS_BIGENDIAN
          *dstptr++ = a;
#endif
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for RGB888 to BGRX8888.  */
static void
grub_video_fbblit_replace_BGRX8888_RGB888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint8_t r = *srcptr++;
          grub_uint8_t g = *srcptr++;
          grub_uint8_t b = *srcptr++;

#ifdef GRUB_CPU_WORDS_BIGENDIAN
          /* Set alpha component as opaque.  */
          *dstptr++ = 255;
#endif

          *dstptr++ = b;
          *dstptr++ = g;
          *dstptr++ = r;

#ifndef GRUB_CPU_WORDS_BIGENDIAN
          /* Set alpha component as opaque.  */
          *dstptr++ = 255;
#endif
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for RGBX8888 to BGR888.  */
static void
grub_video_fbblit_replace_BGR888_RGBX8888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;
  int i;
  int j;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint32_t color;
          grub_uint8_t sr;
          grub_uint8_t sg;
          grub_uint8_t sb;

          color = *srcptr++;

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

#ifdef GRUB_CPU_WORDS_BIGENDIAN
          *dstptr++ = sr;
          *dstptr++ = sg;
          *dstptr++ = sb;
#else
          *dstptr++ = sb;
          *dstptr++ = sg;
          *dstptr++ = sr;
#endif
        }

      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for RGB888 to BGR888.  */
static void
grub_video_fbblit_replace_BGR888_RGB888 (struct grub_video_fbblit_info *dst,
					 struct grub_video_fbblit_info *src,
					 int x, int y,
					 int width, int height,
					 int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint8_t r = *srcptr++;
          grub_uint8_t g = *srcptr++;
          grub_uint8_t b = *srcptr++;

          *dstptr++ = b;
          *dstptr++ = g;
          *dstptr++ = r;
        }

      srcptr += srcrowskip;
      dstptr += dstrowskip;
    }
}

/* Optimized replacing blitter for RGB888 to RGBX8888.  */
static void
grub_video_fbblit_replace_RGBX8888_RGB888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint32_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 3 * width;
  dstrowskip = dst->mode_info->pitch - 4 * width;
  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
#ifdef GRUB_CPU_WORDS_BIGENDIAN
          sb = *srcptr++;
          sg = *srcptr++;
          sr = *srcptr++;
#else
          sr = *srcptr++;
          sg = *srcptr++;
          sb = *srcptr++;
#endif
          /* Set alpha as opaque.  */
          color = 0xFF000000 | (sb << 16) | (sg << 8) | sr;

          *dstptr++ = color;
        }
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for RGBX8888 to RGB888.  */
static void
grub_video_fbblit_replace_RGB888_RGBX8888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);
  srcrowskip = src->mode_info->pitch - 4 * width;
  dstrowskip = dst->mode_info->pitch - 3 * width;

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
	{
	  color = *srcptr++;

	  sr = (color >> 0) & 0xFF;
	  sg = (color >> 8) & 0xFF;
	  sb = (color >> 16) & 0xFF;

#ifndef GRUB_CPU_WORDS_BIGENDIAN
	  *dstptr++ = sr;
	  *dstptr++ = sg;
	  *dstptr++ = sb;
#else
	  *dstptr++ = sb;
	  *dstptr++ = sg;
	  *dstptr++ = sr;
#endif
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for RGBX8888 to indexed color.  */
static void
grub_video_fbblit_replace_index_RGBX8888 (struct grub_video_fbblit_info *dst,
					  struct grub_video_fbblit_info *src,
					  int x, int y,
					  int width, int height,
					  int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 4 * width;
  dstrowskip = dst->mode_info->pitch - width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
	{
	  color = *srcptr++;

	  sr = (color >> 0) & 0xFF;
	  sg = (color >> 8) & 0xFF;
	  sb = (color >> 16) & 0xFF;

	  color = grub_video_fb_map_rgb(sr, sg, sb);
	  *dstptr++ = color & 0xFF;
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized replacing blitter for RGB888 to indexed color.  */
static void
grub_video_fbblit_replace_index_RGB888 (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 3 * width;
  dstrowskip = dst->mode_info->pitch - width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
#ifndef GRUB_CPU_WORDS_BIGENDIAN
          sr = *srcptr++;
          sg = *srcptr++;
          sb = *srcptr++;
#else
          sb = *srcptr++;
          sg = *srcptr++;
          sr = *srcptr++;
#endif

          color = grub_video_fb_map_rgb(sr, sg, sb);

          *dstptr++ = color & 0xFF;
        }
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

static inline grub_uint8_t
alpha_dilute (grub_uint8_t bg, grub_uint8_t fg, grub_uint8_t alpha)
{
  grub_uint16_t s;
  grub_uint16_t h, l;
  s = (fg * alpha) + (bg * (255 ^ alpha));
  /* Optimised division by 255.  */
  h = s >> 8;
  l = s & 0xff;
  if (h + l >= 255)
    h++;
  return h;
}

/* Generic blending blitter.  Works for every supported format.  */
static void
grub_video_fbblit_blend (struct grub_video_fbblit_info *dst,
			 struct grub_video_fbblit_info *src,
			 int x, int y, int width, int height,
			 int offset_x, int offset_y)
{
  int i;
  int j;

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint8_t src_red;
          grub_uint8_t src_green;
          grub_uint8_t src_blue;
          grub_uint8_t src_alpha;
          grub_uint8_t dst_red;
          grub_uint8_t dst_green;
          grub_uint8_t dst_blue;
          grub_uint8_t dst_alpha;
          grub_video_color_t src_color;
          grub_video_color_t dst_color;

          src_color = get_pixel (src, i + offset_x, j + offset_y);
          grub_video_fb_unmap_color_int (src, src_color, &src_red, &src_green,
					 &src_blue, &src_alpha);

          if (src_alpha == 0)
            continue;

          if (src_alpha == 255)
            {
              dst_color = grub_video_fb_map_rgba (src_red, src_green,
						  src_blue, src_alpha);
              set_pixel (dst, x + i, y + j, dst_color);
              continue;
            }

          dst_color = get_pixel (dst, x + i, y + j);

          grub_video_fb_unmap_color_int (dst, dst_color, &dst_red,
					 &dst_green, &dst_blue, &dst_alpha);

          dst_red = alpha_dilute (dst_red, src_red, src_alpha);
          dst_green = alpha_dilute (dst_green, src_green, src_alpha);
          dst_blue = alpha_dilute (dst_blue, src_blue, src_alpha);

          dst_alpha = src_alpha;
          dst_color = grub_video_fb_map_rgba (dst_red, dst_green, dst_blue,
					      dst_alpha);

          set_pixel (dst, x + i, y + j, dst_color);
        }
    }
}

/* Optimized blending blitter for RGBA8888 to BGRA8888.  */
static void
grub_video_fbblit_blend_BGRA8888_RGBA8888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  grub_uint32_t *srcptr;
  grub_uint32_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;
  int i;
  int j;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint32_t color;
          unsigned int sr;
          unsigned int sg;
          unsigned int sb;
          unsigned int a;
          unsigned int dr;
          unsigned int dg;
          unsigned int db;

          color = *srcptr++;

          a = color >> 24;

          if (a == 0)
            {
              /* Skip transparent source pixels.  */
              dstptr++;
              continue;
            }

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

          if (a == 255)
            {
              /* Opaque pixel shortcut.  */
              dr = sr;
              dg = sg;
              db = sb;
            }
          else
            {
              /* General pixel color blending.  */
              color = *dstptr;

              dr = (color >> 16) & 0xFF;
              dr = alpha_dilute (dr, sr, a);
              dg = (color >> 8) & 0xFF;
              dg = alpha_dilute (dg, sg, a);
              db = (color >> 0) & 0xFF;
              db = alpha_dilute (db, sb, a);
            }

          color = (a << 24) | (dr << 16) | (dg << 8) | db;

          *dstptr++ = color;
        }

      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized blending blitter for RGBA8888 to BGR888.  */
static void
grub_video_fbblit_blend_BGR888_RGBA8888 (struct grub_video_fbblit_info *dst,
					 struct grub_video_fbblit_info *src,
					 int x, int y,
					 int width, int height,
					 int offset_x, int offset_y)
{
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int srcrowskip;
  unsigned int dstrowskip;
  int i;
  int j;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  srcrowskip = src->mode_info->pitch - src->mode_info->bytes_per_pixel * width;
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          grub_uint32_t color;
          unsigned int sr;
          unsigned int sg;
          unsigned int sb;
          unsigned int a;
          unsigned int dr;
          unsigned int dg;
          unsigned int db;

          color = *srcptr++;

          a = color >> 24;

          if (a == 0)
            {
              /* Skip transparent source pixels.  */
              dstptr += 3;
              continue;
            }

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

          if (a == 255)
            {
              /* Opaque pixel shortcut.  */
              dr = sr;
              dg = sg;
              db = sb;
            }
          else
            {
              /* General pixel color blending.  */
              color = *dstptr;

#ifndef GRUB_CPU_WORDS_BIGENDIAN
              db = dstptr[0];
              dg = dstptr[1];
              dr = dstptr[2];
#else
              dr = dstptr[0];
              dg = dstptr[1];
              db = dstptr[2];
#endif

              db = alpha_dilute (db, sb, a);
              dg = alpha_dilute (dg, sg, a);
              dr = alpha_dilute (dr, sr, a);
            }

#ifndef GRUB_CPU_WORDS_BIGENDIAN
          *dstptr++ = db;
          *dstptr++ = dg;
          *dstptr++ = dr;
#else
          *dstptr++ = dr;
          *dstptr++ = dg;
          *dstptr++ = db;
#endif
        }

      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      dstptr += dstrowskip;
    }
}

/* Optimized blending blitter for RGBA888 to RGBA8888.  */
static void
grub_video_fbblit_blend_RGBA8888_RGBA8888 (struct grub_video_fbblit_info *dst,
					   struct grub_video_fbblit_info *src,
					   int x, int y,
					   int width, int height,
					   int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint32_t *srcptr;
  grub_uint32_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  unsigned int a;
  unsigned int dr;
  unsigned int dg;
  unsigned int db;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 4 * width;
  dstrowskip = dst->mode_info->pitch - 4 * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          color = *srcptr++;

          a = color >> 24;

          if (a == 0)
            {
              dstptr++;
              continue;
            }

          if (a == 255)
            {
              *dstptr++ = color;
              continue;
            }

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

          color = *dstptr;

          dr = (color >> 0) & 0xFF;
          dg = (color >> 8) & 0xFF;
          db = (color >> 16) & 0xFF;

          dr = alpha_dilute (dr, sr, a);
          dg = alpha_dilute (dg, sg, a);
          db = alpha_dilute (db, sb, a);

          color = (a << 24) | (db << 16) | (dg << 8) | dr;

          *dstptr++ = color;
        }
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized blending blitter for RGBA8888 to RGB888.  */
static void
grub_video_fbblit_blend_RGB888_RGBA8888 (struct grub_video_fbblit_info *dst,
					 struct grub_video_fbblit_info *src,
					 int x, int y,
					 int width, int height,
					 int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  unsigned int a;
  unsigned int dr;
  unsigned int dg;
  unsigned int db;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 4 * width;
  dstrowskip = dst->mode_info->pitch - 3 * width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          color = *srcptr++;

          a = color >> 24;

          if (a == 0)
            {
              dstptr += 3;
              continue;
            }

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

          if (a == 255)
            {
#ifndef GRUB_CPU_WORDS_BIGENDIAN
              *dstptr++ = sr;
              *dstptr++ = sg;
              *dstptr++ = sb;
#else
              *dstptr++ = sb;
              *dstptr++ = sg;
              *dstptr++ = sr;
#endif

              continue;
            }

#ifndef GRUB_CPU_WORDS_BIGENDIAN
          dr = dstptr[0];
          dg = dstptr[1];
          db = dstptr[2];
#else
          db = dstptr[0];
          dg = dstptr[1];
          dr = dstptr[2];
#endif

          dr = alpha_dilute (dr, sr, a);
          dg = alpha_dilute (dg, sg, a);
          db = alpha_dilute (db, sb, a);

#ifndef GRUB_CPU_WORDS_BIGENDIAN
          *dstptr++ = dr;
          *dstptr++ = dg;
          *dstptr++ = db;
#else
          *dstptr++ = db;
          *dstptr++ = dg;
          *dstptr++ = dr;
#endif
        }
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized blending blitter for RGBA8888 to indexed color.  */
static void
grub_video_fbblit_blend_index_RGBA8888 (struct grub_video_fbblit_info *dst,
					struct grub_video_fbblit_info *src,
					int x, int y,
					int width, int height,
					int offset_x, int offset_y)
{
  grub_uint32_t color;
  int i;
  int j;
  grub_uint32_t *srcptr;
  grub_uint8_t *dstptr;
  unsigned int sr;
  unsigned int sg;
  unsigned int sb;
  unsigned int a;
  unsigned char dr;
  unsigned char dg;
  unsigned char db;
  unsigned char da;
  grub_size_t srcrowskip;
  grub_size_t dstrowskip;

  srcrowskip = src->mode_info->pitch - 4 * width;
  dstrowskip = dst->mode_info->pitch - width;

  srcptr = grub_video_fb_get_video_ptr (src, offset_x, offset_y);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
          color = *srcptr++;

          a = color >> 24;

          if (a == 0)
            {
              dstptr++;
              continue;
            }

          sr = (color >> 0) & 0xFF;
          sg = (color >> 8) & 0xFF;
          sb = (color >> 16) & 0xFF;

          if (a == 255)
            {
              color = grub_video_fb_map_rgb(sr, sg, sb);
              *dstptr++ = color & 0xFF;
              continue;
            }

          grub_video_fb_unmap_color_int (dst, *dstptr, &dr, &dg, &db, &da);

          dr = alpha_dilute (dr, sr, a);
          dg = alpha_dilute (dg, sg, a);
          db = alpha_dilute (db, sb, a);

          color = grub_video_fb_map_rgb(dr, dg, db);

          *dstptr++ = color & 0xFF;
        }
      GRUB_VIDEO_FB_ADVANCE_POINTER (srcptr, srcrowskip);
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized blending blitter for 1-bit to XXXA8888.  */
static void
grub_video_fbblit_blend_XXXA8888_1bit (struct grub_video_fbblit_info *dst,
				       struct grub_video_fbblit_info *src,
				       int x, int y,
				       int width, int height,
				       int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint32_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint32_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  grub_uint32_t color;
	  grub_uint8_t a;

	  if (*srcptr & srcmask)
	    {
	      color = fgcolor;
	      a = src->mode_info->fg_alpha;
	    }
	  else
	    {
	      color = bgcolor;
	      a = src->mode_info->bg_alpha;
	    }

	  if (a == 255)
	    *dstptr = color;
	  else if (a != 0)
	    {
	      grub_uint8_t s1 = (color >> 0) & 0xFF;
	      grub_uint8_t s2 = (color >> 8) & 0xFF;
	      grub_uint8_t s3 = (color >> 16) & 0xFF;

	      grub_uint8_t d1 = (*dstptr >> 0) & 0xFF;
	      grub_uint8_t d2 = (*dstptr >> 8) & 0xFF;
	      grub_uint8_t d3 = (*dstptr >> 16) & 0xFF;

	      d1 = alpha_dilute (d1, s1, a);
	      d2 = alpha_dilute (d2, s2, a);
	      d3 = alpha_dilute (d3, s3, a);

	      *dstptr = (a << 24) | (d3 << 16) | (d2 << 8) | d1;
	    }

	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr++;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* Optimized blending blitter for 1-bit to XXX888.  */
#ifdef GRUB_HAVE_UNALIGNED_ACCESS
static void
grub_video_fbblit_blend_XXX888_1bit (struct grub_video_fbblit_info *dst,
				     struct grub_video_fbblit_info *src,
				     int x, int y,
				     int width, int height,
				     int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint8_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint32_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  grub_uint32_t color;
	  grub_uint8_t a;
	  if (*srcptr & srcmask)
	    {
	      color = fgcolor;
	      a = src->mode_info->fg_alpha;
	    }
	  else
	    {
	      color = bgcolor;
	      a = src->mode_info->bg_alpha;
	    }

	  if (a == 255)
	    {
#ifndef GRUB_CPU_WORDS_BIGENDIAN
	      ((grub_uint8_t *) dstptr)[0] = color & 0xff;
	      ((grub_uint8_t *) dstptr)[1] = (color & 0xff00) >> 8;
	      ((grub_uint8_t *) dstptr)[2] = (color & 0xff0000) >> 16;
#else
	      ((grub_uint8_t *) dstptr)[2] = color & 0xff;
	      ((grub_uint8_t *) dstptr)[1] = (color & 0xff00) >> 8;
	      ((grub_uint8_t *) dstptr)[0] = (color & 0xff0000) >> 16;
#endif
	    }
	  else if (a != 0)
	    {
	      grub_uint8_t s1 = (color >> 0) & 0xFF;
	      grub_uint8_t s2 = (color >> 8) & 0xFF;
	      grub_uint8_t s3 = (color >> 16) & 0xFF;

	      grub_uint8_t d1 = (*(grub_uint32_t *) dstptr >> 0) & 0xFF;
	      grub_uint8_t d2 = (*(grub_uint32_t *) dstptr >> 8) & 0xFF;
	      grub_uint8_t d3 = (*(grub_uint32_t *) dstptr >> 16) & 0xFF;

	      ((grub_uint8_t *) dstptr)[0] = alpha_dilute (d1, s1, a);
	      ((grub_uint8_t *) dstptr)[1] = alpha_dilute (d2, s2, a);
	      ((grub_uint8_t *) dstptr)[2] = alpha_dilute (d3, s3, a);
	    }

	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr += 3;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      dstptr += dstrowskip;
    }
}
#endif

/* Optimized blending blitter for 1-bit to XXX888.  */
static void
grub_video_fbblit_blend_XXX565_1bit (struct grub_video_fbblit_info *dst,
				     struct grub_video_fbblit_info *src,
				     int x, int y,
				     int width, int height,
				     int offset_x, int offset_y)
{
  int i;
  int j;
  grub_uint8_t *srcptr;
  grub_uint16_t *dstptr;
  grub_uint8_t srcmask;
  unsigned int dstrowskip;
  unsigned int srcrowskipbyte, srcrowskipbit;
  grub_uint16_t fgcolor, bgcolor;
  int bit_index;

  /* Calculate the number of bytes to advance from the end of one line
     to the beginning of the next line.  */
  dstrowskip = dst->mode_info->pitch - dst->mode_info->bytes_per_pixel * width;
  srcrowskipbyte = (src->mode_info->width - width) >> 3;
  srcrowskipbit = (src->mode_info->width - width) & 7;

  bit_index = offset_y * src->mode_info->width + offset_x;
  srcptr = (grub_uint8_t *) src->data + (bit_index >> 3);
  srcmask = 1 << (~bit_index & 7);
  dstptr = grub_video_fb_get_video_ptr (dst, x, y);

  fgcolor = grub_video_fb_map_rgba (src->mode_info->fg_red,
				    src->mode_info->fg_green,
				    src->mode_info->fg_blue,
				    src->mode_info->fg_alpha);

  bgcolor = grub_video_fb_map_rgba (src->mode_info->bg_red,
				    src->mode_info->bg_green,
				    src->mode_info->bg_blue,
				    src->mode_info->bg_alpha);

  for (j = 0; j < height; j++)
    {
      for (i = 0; i < width; i++)
        {
	  grub_uint32_t color;
	  grub_uint8_t a;
	  if (*srcptr & srcmask)
	    {
	      color = fgcolor;
	      a = src->mode_info->fg_alpha;
	    }
	  else
	    {
	      color = bgcolor;
	      a = src->mode_info->bg_alpha;
	    }

	  if (a == 255)
	    *dstptr = color;
	  else if (a != 0)
	    {
	      grub_uint8_t s1 = (color >> 0) & 0x1F;
	      grub_uint8_t s2 = (color >> 5) & 0x3F;
	      grub_uint8_t s3 = (color >> 11) & 0x1F;

	      grub_uint8_t d1 = (*dstptr >> 0) & 0x1F;
	      grub_uint8_t d2 = (*dstptr >> 5) & 0x3F;
	      grub_uint8_t d3 = (*dstptr >> 11) & 0x1F;

	      d1 = alpha_dilute (d1, s1, a);
	      d2 = alpha_dilute (d2, s2, a);
	      d3 = alpha_dilute (d3, s3, a);

	      *dstptr = (d1 & 0x1f) | ((d2 & 0x3f) << 5) | ((d3 & 0x1f) << 11);
	    }

	  srcmask >>= 1;
	  if (!srcmask)
	    {
	      srcptr++;
	      srcmask = 0x80;
	    }

	  dstptr++;
        }

      srcptr += srcrowskipbyte;
      if (srcmask >> srcrowskipbit)
	srcmask >>= srcrowskipbit;
      else
	{
	  srcptr++;
	  srcmask <<= 8 - srcrowskipbit;
	}
      GRUB_VIDEO_FB_ADVANCE_POINTER (dstptr, dstrowskip);
    }
}

/* NOTE: This function assumes that given coordinates are within bounds of
   handled data.  */
void
grub_video_fb_dispatch_blit (struct grub_video_fbblit_info *target,
			     struct grub_video_fbblit_info *source,
			     enum grub_video_blit_operators oper, int x, int y,
			     unsigned int width, unsigned int height,
			     int offset_x, int offset_y)
{
  if (oper == GRUB_VIDEO_BLIT_REPLACE)
    {
      /* Try to figure out more optimized version for replace operator.  */
      switch (source->mode_info->blit_format)
	{
	case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	      grub_video_fbblit_replace_directN (target, source,
						       x, y, width, height,
						       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	      grub_video_fbblit_replace_BGRX8888_RGBX8888 (target, source,
								 x, y, width, height,
								 offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_888:
	      grub_video_fbblit_replace_BGR888_RGBX8888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	      grub_video_fbblit_replace_RGB888_RGBX8888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	      grub_video_fbblit_replace_index_RGBX8888 (target, source,
							      x, y, width, height,
							      offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	      grub_video_fbblit_replace_BGRX8888_RGB888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	      grub_video_fbblit_replace_RGBX8888_RGB888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_888:
	      grub_video_fbblit_replace_BGR888_RGB888 (target, source,
							     x, y, width, height,
							     offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	      grub_video_fbblit_replace_directN (target, source,
						       x, y, width, height,
						       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	      grub_video_fbblit_replace_index_RGB888 (target, source,
							    x, y, width, height,
							    offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	      grub_video_fbblit_replace_directN (target, source,
						       x, y, width, height,
						       offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR_ALPHA:
	      grub_video_fbblit_replace_directN (target, source,
						       x, y, width, height,
						       offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR_ALPHA:
	  switch (target->mode_info->bytes_per_pixel)
	    {
	    case 4:
	      grub_video_fbblit_replace_32bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 3:
	      grub_video_fbblit_replace_24bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 2:
	      grub_video_fbblit_replace_16bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 1:
	      grub_video_fbblit_replace_8bit_indexa (target, source,
						     x, y, width, height,
						     offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_1BIT_PACKED:
	  switch (target->mode_info->bytes_per_pixel)
	    {
	    case 4:
	      grub_video_fbblit_replace_32bit_1bit (target, source,
						    x, y, width, height,
						    offset_x, offset_y);
	      return;
#ifdef GRUB_HAVE_UNALIGNED_ACCESS
	    case 3:
	      grub_video_fbblit_replace_24bit_1bit (target, source,
						    x, y, width, height,
						    offset_x, offset_y);
	      return;
#endif
	    case 2:
	      grub_video_fbblit_replace_16bit_1bit (target, source,
						    x, y, width, height,
						    offset_x, offset_y);
	      return;
	    case 1:
	      grub_video_fbblit_replace_8bit_1bit (target, source,
						   x, y, width, height,
						   offset_x, offset_y);
	      return;
	    }
	  break;
	default:
	  break;
	}

      /* No optimized replace operator found, use default (slow) blitter.  */
      grub_video_fbblit_replace (target, source, x, y, width, height,
				       offset_x, offset_y);
    }
  else
    {
      /* Try to figure out more optimized blend operator.  */
      switch (source->mode_info->blit_format)
	{
	case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	      grub_video_fbblit_blend_BGRA8888_RGBA8888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	      grub_video_fbblit_blend_RGBA8888_RGBA8888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_888:
	      grub_video_fbblit_blend_BGR888_RGBA8888 (target, source,
							     x, y, width, height,
							     offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	      grub_video_fbblit_blend_RGB888_RGBA8888 (target, source,
							     x, y, width, height,
							     offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	      grub_video_fbblit_blend_index_RGBA8888 (target, source,
							    x, y, width, height,
							    offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	  /* Note: There is really no alpha information here, so blend is
	     changed to replace.  */

	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	      grub_video_fbblit_replace_BGRX8888_RGB888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	      grub_video_fbblit_replace_RGBX8888_RGB888 (target, source,
							       x, y, width, height,
							       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_888:
	      grub_video_fbblit_replace_BGR888_RGB888 (target, source,
							     x, y, width, height,
							     offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	      grub_video_fbblit_replace_directN (target, source,
						       x, y, width, height,
						       offset_x, offset_y);
	      return;
	    case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR:
	      grub_video_fbblit_replace_index_RGB888 (target, source,
							    x, y, width, height,
							    offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_1BIT_PACKED:
	  switch (target->mode_info->blit_format)
	    {
	    case GRUB_VIDEO_BLIT_FORMAT_BGRA_8888:
	    case GRUB_VIDEO_BLIT_FORMAT_RGBA_8888:
	      grub_video_fbblit_blend_XXXA8888_1bit (target, source,
						     x, y, width, height,
						     offset_x, offset_y);
	      return;
#ifdef GRUB_HAVE_UNALIGNED_ACCESS
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_888:
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_888:
	      grub_video_fbblit_blend_XXX888_1bit (target, source,
						   x, y, width, height,
						   offset_x, offset_y);
	      return;
#endif
	    case GRUB_VIDEO_BLIT_FORMAT_BGR_565:
	    case GRUB_VIDEO_BLIT_FORMAT_RGB_565:
	      grub_video_fbblit_blend_XXX565_1bit (target, source,
						   x, y, width, height,
						   offset_x, offset_y);
	      return;
	    default:
	      break;
	    }
	  break;
	case GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR_ALPHA:
	  switch (target->mode_info->bytes_per_pixel)
	    {
	    case 4:
	      grub_video_fbblit_blend_32bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 3:
	      grub_video_fbblit_blend_24bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 2:
	      grub_video_fbblit_blend_16bit_indexa (target, source,
						      x, y, width, height,
						      offset_x, offset_y);
	      return;
	    case 1:
	      grub_video_fbblit_blend_8bit_indexa (target, source,
						     x, y, width, height,
						     offset_x, offset_y);
	      return;
	    }
	  break;
	default:
	  break;
	}

      /* No optimized blend operation found, use default (slow) blitter.  */
      grub_video_fbblit_blend (target, source, x, y, width, height,
				     offset_x, offset_y);
    }
}
