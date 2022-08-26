/* font.c - Font API and font file loader.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/bufio.h>
#include <grub/dl.h>
#include <grub/file.h>
#include <grub/font.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/charset.h>
#include <grub/unicode.h>
#include <grub/fontformat.h>
#include <grub/gfxmenu_view.h>

/* Draw a UTF-8 string of text on the current video render target.
   The x coordinate specifies the starting x position for the first character,
   while the y coordinate specifies the baseline position.
   If the string contains a character that FONT does not contain, then
   a glyph from another loaded font may be used instead.  */
grub_err_t
grub_font_draw_string (const char *str, grub_font_t font,
                       grub_video_color_t color,
                       int left_x, int baseline_y)
{
  int x;
  grub_uint32_t *logical;
  grub_ssize_t logical_len, visual_len;
  struct grub_unicode_glyph *visual, *ptr;
  grub_err_t err;

  logical_len = grub_utf8_to_ucs4_alloc (str, &logical, 0);
  if (logical_len < 0)
    return grub_errno;

  visual_len = grub_bidi_logical_to_visual (logical, logical_len, &visual,
					    0, 0, 0, 0, 0, 0, 0);
  grub_free (logical);
  if (visual_len < 0)
    return grub_errno;

  err = GRUB_ERR_NONE;
  for (ptr = visual, x = left_x; ptr < visual + visual_len; ptr++)
    {
      struct grub_font_glyph *glyph;
      glyph = grub_font_construct_glyph (font, ptr);
      if (!glyph)
	{
	  err = grub_errno;
	  goto out;
	}
      err = grub_font_draw_glyph (glyph, color, x, baseline_y);
      if (err)
	goto out;
      x += glyph->device_width;
    }

out:
  for (ptr = visual; ptr < visual + visual_len; ptr++)
    grub_unicode_destroy_glyph (ptr);
  grub_free (visual);

  return err;
}

/* Get the width in pixels of the specified UTF-8 string, when rendered in
   in the specified font (but falling back on other fonts for glyphs that
   are missing).  */
int
grub_font_get_string_width (grub_font_t font, const char *str)
{
  int width = 0;
  grub_uint32_t *ptr;
  grub_ssize_t logical_len;
  grub_uint32_t *logical;

  logical_len = grub_utf8_to_ucs4_alloc (str, &logical, 0);
  if (logical_len < 0)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  for (ptr = logical; ptr < logical + logical_len;)
    {
      struct grub_unicode_glyph glyph;

      ptr += grub_unicode_aglomerate_comb (ptr,
					   logical_len - (ptr - logical),
					   &glyph);
      width += grub_font_get_constructed_device_width (font, &glyph);

      grub_unicode_destroy_glyph (&glyph);
    }
  grub_free (logical);

  return width;
}
