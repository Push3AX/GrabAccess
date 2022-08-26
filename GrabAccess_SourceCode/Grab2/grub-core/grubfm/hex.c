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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/normal.h>
#include <grub/term.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/gfxmenu_view.h>
#include <grub/lib/hexdump.h>

#include "fm.h"

#define HEXDUMP_LEN 0x10

static void
grubfm_hexdump_print (grub_file_t file, grub_size_t skip, unsigned int y)
{
  grub_size_t i;
  grub_size_t len;
  unsigned char buf[HEXDUMP_LEN];
  char data_str[HEXDUMP_LEN + 1];
  char data_hex[3 * HEXDUMP_LEN +1];
  char str[85];
  grub_font_t font = 0;
  grub_video_color_t white = grubfm_get_color (255, 255, 255);

  if (!file)
    return;

  font = grub_font_get ("unifont");

  grub_memset (buf, 0, sizeof (buf));
  grub_memset (data_str, 0, sizeof (data_str));
  grub_memset (data_hex, 0, sizeof (data_hex));

  if (skip >= file->size)
    return;
  if (skip + HEXDUMP_LEN > file->size)
    len = file->size - skip;
  else
    len = HEXDUMP_LEN;
  file->offset = skip;
  grub_file_read (file, buf, len);

  for (i = 0; i < HEXDUMP_LEN; i++)
  {
    data_str[i] = (buf[i] >= 32) && (buf[i] < 127) ? buf[i] : '.';
    grub_sprintf (data_hex, "%s %02x", data_hex, buf[i]);
  }
  grub_snprintf (str, sizeof (str), "0x%08llx%s |%s|",
                 (unsigned long long)skip, data_hex, data_str);
  grub_font_draw_string (str, font, white, 0, y);
}

#define HEXDUMP_HEADER \
    "  offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"

#define HEXDUMP_LINE 0x20
#define HEXDUMP_PAGE_OFFSET (HEXDUMP_LINE*HEXDUMP_LEN)

void
grubfm_hexdump (const char *filename)
{
  grub_size_t i;
  grub_file_t file = 0;
  grub_font_t font = 0;
  grub_size_t offset = 0;
  unsigned int w, h;
  grub_video_color_t white = grubfm_get_color (255, 255, 255);
  grubfm_get_screen_info (&w, &h);
  if (w < 1024 || h < 768)
    return;
  file = grub_file_open (filename, GRUB_FILE_TYPE_HEXCAT |
                         GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
    return;
  font = grub_font_get ("unifont");
  while (1)
  {
    grubfm_gfx_clear ();
    grubfm_gfx_printf (white, 0, FONT_SPACE, _("FILE: %s (%s)"), filename,
                       grub_get_human_size (file->size, GRUB_HUMAN_SIZE_SHORT));
    grub_font_draw_string (HEXDUMP_HEADER, font, white, 0, 2 * FONT_SPACE);
    for (i = 0; i < HEXDUMP_LINE; i++)
    {
      grub_size_t len = offset + i * HEXDUMP_LEN;
      grubfm_hexdump_print (file, len, FONT_SPACE * (i + 3));
      if (len > file->size)
      {
        grub_font_draw_string (_("--- END ---"), font,
                               white, 0, FONT_SPACE * (i + 2));
        break;
      }
    }

    grubfm_gfx_printf (white, 0, h - FONT_SPACE,
                       _("↑ Page Up  ↓ Page Down  [ESC] Exit"));
    /* wait key */
    int key = 0;
    while (key != GRUB_TERM_ESC &&
           key != GRUB_TERM_KEY_UP &&
           key != GRUB_TERM_KEY_DOWN)
    {
      key = grub_getkey ();
    }
    if (key == GRUB_TERM_ESC)
      break;
    if (key == GRUB_TERM_KEY_UP)
    {
      if (offset > HEXDUMP_PAGE_OFFSET)
        offset -= HEXDUMP_PAGE_OFFSET;
      else
        offset = 0;
      continue;
    }
    if (key == GRUB_TERM_KEY_DOWN)
    {
      if (offset + HEXDUMP_PAGE_OFFSET < file->size)
        offset += HEXDUMP_PAGE_OFFSET;
      continue;
    }
  }
  if (file)
    grub_file_close (file);
}
