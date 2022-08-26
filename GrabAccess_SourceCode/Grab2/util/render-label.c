/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2012,2013 Free Software Foundation, Inc.
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


#include <config.h>

#include <grub/util/misc.h>
#include <grub/util/install.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/font.h>
#include <grub/gfxmenu_view.h>
#include <grub/color.h>
#include <grub/emu/hostdisk.h>

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct header
{
  grub_uint8_t magic;
  grub_uint16_t width;
  grub_uint16_t height;
} GRUB_PACKED;

static struct grub_video_palette_data ieee1275_palette[256];

void
grub_util_render_label (const char *label_font,
			const char *label_bgcolor,
			const char *label_color,
			const char *text,
			const char *output)
{
  struct header head;
  FILE *out;
  int i, j, k, cptr = 0;
  grub_font_t font;
  char *fontfull;
  const grub_uint8_t vals[] = { 0xff, 0xda, 0xb3, 0x87, 0x54, 0x00 };
  const grub_uint8_t vals2[] = { 0xf3, 0xe7, 0xcd, 0xc0, 0xa5, 0x96,
				 0x77, 0x66, 0x3f, 0x27 };
  int width, height;
  grub_uint8_t bg, fg;
  struct grub_video_mode_info mode_info;
  grub_err_t err;
  grub_video_rgba_color_t fgcolor;
  grub_video_rgba_color_t bgcolor;

  if (output)
    out = grub_util_fopen (output, "wb");
  else
    out = stdout;
  if (!out)
    {
      grub_util_error (_("cannot open `%s': %s"), output ? : "stdout",
		       strerror (errno));
    }

  if (label_color)
    {
      err = grub_video_parse_color (label_color, &fgcolor);
      if (err)
	grub_util_error (_("invalid color specification `%s'"), label_color);
    }
  else
    {
      fgcolor.red = 0x00;
      fgcolor.green = 0x00;
      fgcolor.blue = 0x00;
      fgcolor.alpha = 0xff;
    }

  if (label_bgcolor)
    {
      err = grub_video_parse_color (label_bgcolor, &bgcolor);
      if (err)
	grub_util_error (_("invalid color specification `%s'"), label_bgcolor);
    }
  else
    {
      bgcolor.red = 0xff;
      bgcolor.green = 0xff;
      bgcolor.blue = 0xff;
      bgcolor.alpha = 0xff;
    }

  for (i = 0; i < 256; i++)
    ieee1275_palette[i].a = 0xff;

  for (i = 0; i < 6; i++)
    for (j = 0; j < 6; j++)
      for (k = 0; k < 6; k++)
	{
	  ieee1275_palette[cptr].r = vals[i];
	  ieee1275_palette[cptr].g = vals[j];
	  ieee1275_palette[cptr].b = vals[k];
	  ieee1275_palette[cptr].a = 0xff;
	  cptr++;
	}
  cptr--;
  for (i = 0; i < 10; i++)
    {
      ieee1275_palette[cptr].r = vals2[i];
      ieee1275_palette[cptr].g = 0;
      ieee1275_palette[cptr].b = 0;
      ieee1275_palette[cptr].a = 0xff;
      cptr++;
    }
  for (i = 0; i < 10; i++)
    {
      ieee1275_palette[cptr].r = 0;
      ieee1275_palette[cptr].g = vals2[i];
      ieee1275_palette[cptr].b = 0;
      ieee1275_palette[cptr].a = 0xff;
      cptr++;
    }
  for (i = 0; i < 10; i++)
    {
      ieee1275_palette[cptr].r = 0;
      ieee1275_palette[cptr].g = 0;
      ieee1275_palette[cptr].b = vals2[i];
      ieee1275_palette[cptr].a = 0xff;
      cptr++;
    }
  for (i = 0; i < 10; i++)
    {
      ieee1275_palette[cptr].r = vals2[i];
      ieee1275_palette[cptr].g = vals2[i];
      ieee1275_palette[cptr].b = vals2[i];
      ieee1275_palette[cptr].a = 0xff;
      cptr++;
    }
  ieee1275_palette[cptr].r = 0;
  ieee1275_palette[cptr].g = 0;
  ieee1275_palette[cptr].b = 0;
  ieee1275_palette[cptr].a = 0xff;

  char * t;
  t = grub_canonicalize_file_name (label_font);
  if (!t)
    {
      grub_util_error (_("cannot open `%s': %s"), label_font,
		       strerror (errno));
    }  

  fontfull = xasprintf ("(host)/%s", t);
  free (t);

  grub_font_loader_init ();
  font = grub_font_load (fontfull);
  if (!font)
    {
      grub_util_error (_("cannot open `%s': %s"), label_font,
		       grub_errmsg);
    }  

  width = grub_font_get_string_width (font, text) + 10;
  height = grub_font_get_height (font);

  mode_info.width = width;
  mode_info.height = height;
  mode_info.pitch = width;

  mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
  mode_info.bpp = 8;
  mode_info.bytes_per_pixel = 1;
  mode_info.number_of_colors = 256;

  grub_video_capture_start (&mode_info, ieee1275_palette,
			    ARRAY_SIZE (ieee1275_palette));

  fg = grub_video_map_rgb (fgcolor.red,
			   fgcolor.green,
			   fgcolor.blue);
  bg = grub_video_map_rgb (bgcolor.red,
			   bgcolor.green,
			   bgcolor.blue);

  grub_memset (grub_video_capture_get_framebuffer (), bg, height * width);
  grub_font_draw_string (text, font, fg,
                         5, grub_font_get_ascent (font));

  head.magic = 1;
  head.width = grub_cpu_to_be16 (width);
  head.height = grub_cpu_to_be16 (height);
  fwrite (&head, 1, sizeof (head), out);
  fwrite (grub_video_capture_get_framebuffer (), 1, width * height, out);

  grub_video_capture_end ();
  if (out != stdout)
    fclose (out);

  free (fontfull);
}
