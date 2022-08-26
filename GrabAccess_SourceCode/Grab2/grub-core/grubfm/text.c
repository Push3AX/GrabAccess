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
#include <grub/conv.h>
#include <grub/gfxmenu_view.h>
#include <grub/lib/hexdump.h>

#include "fm.h"

#define SIZE_1MB 1048576

enum text_encoding
{
  ENCODING_UTF8,
  ENCODING_GBK,
};

static const char *text_encoding[] =
{
  "UTF-8",
  "GBK",
};

static enum text_encoding encoding = ENCODING_UTF8;

static int
grubfm_textcat_eof (grub_file_t file)
{
  if (!file || file->offset >= file->size)
    return 1;
  else
    return 0;
}

static void
grubfm_textcat_page (grub_file_t file,
                     grub_size_t from, grub_size_t count, unsigned int y)
{
  grub_size_t i;
  char *line = NULL;
  grub_video_color_t white = grubfm_get_color (255, 255, 255);;
  if (!file)
    return;
  /* goto */
  file->offset = 0;
  for (i = 0; i < from; i++)
  {
    if (grubfm_textcat_eof (file))
    {
      grubfm_gfx_printf (white, 0, y, "                    --- END ---");
      return;
    }
    line = grub_file_getline (file);
    if (line)
      grub_free (line);
  }
  for (i = 0; i < count; i++)
  {
    if (grubfm_textcat_eof (file))
    {
      grubfm_gfx_printf (white, 0, y + FONT_SPACE * i,
                         "                    --- END ---");
      return;
    }
    line = grub_file_getline (file);
    if (! line)
      grubfm_gfx_printf (white, 0, y + FONT_SPACE * i,
                         "%20lld (null)", (unsigned long long)(i + from + 1));
    else
    {
      if (encoding == ENCODING_GBK)
      {
        char *buffer = NULL;
        grub_uint32_t len = grub_strlen (line);
        grub_uint32_t buf_len;
        buf_len = len * 3 + 1;
        buffer = (char *) grub_zalloc (buf_len);
        gbk_to_utf8 (line, len, &buffer, &buf_len);
        grubfm_gfx_printf (white, 0, y + FONT_SPACE * i,
                         "%20lld %s", (unsigned long long)(i + from + 1), buffer);
        if (buffer)
          grub_free (buffer);
      }
      else
      {
        grubfm_gfx_printf (white, 0, y + FONT_SPACE * i,
                           "%20lld %s", (unsigned long long)(i + from + 1), line);
      }
      grub_free (line);
    }
  }
}

#define CAT_LINE_NUM 36

void
grubfm_textcat (const char *filename)
{
  grub_file_t file = 0;
  grub_size_t line_num = 0;
  unsigned int w, h;
  grub_video_color_t white = grubfm_get_color (255, 255, 255);
  grubfm_get_screen_info (&w, &h);
  if (w < 1024 || h < 768)
    return;
  file = grub_file_open (filename, GRUB_FILE_TYPE_CAT |
                         GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
    return;
  if (file->size > SIZE_1MB)
  {
    int key;
    grub_printf (_("Are you sure to open large text file %s?\nPress [Y] to continue.\n"),
                 file->name);
    key = grub_getkey ();
    if (key != 121)
      return;
  }
  while (1)
  {
    grubfm_gfx_clear ();
    grubfm_gfx_printf (white, 0, FONT_SPACE, _("FILE: %s (%s) ENCODING: %s"),
                       filename,
                       grub_get_human_size (file->size, GRUB_HUMAN_SIZE_SHORT),
                       text_encoding[encoding]);

    grubfm_textcat_page (file, line_num, CAT_LINE_NUM, 2 * FONT_SPACE);

    grubfm_gfx_printf (white, 0, h - 4,
                       _("↑ Page Up  ↓ Page Down  [e] Encoding  [ESC] Exit"));
    /* wait key */
    int key = 0;
    while (key != GRUB_TERM_ESC &&
           key != GRUB_TERM_KEY_UP &&
           key != GRUB_TERM_KEY_DOWN &&
           key != 'e')
    {
      key = grub_getkey ();
    }
    if (key == GRUB_TERM_ESC)
      break;
    if (key == GRUB_TERM_KEY_DOWN)
    {
      if (! grubfm_textcat_eof (file))
        line_num += CAT_LINE_NUM;
      continue;
    }
    if (key == GRUB_TERM_KEY_UP)
    {
      if (line_num < CAT_LINE_NUM)
        line_num = 0;
      else
        line_num -= CAT_LINE_NUM;
      continue;
    }
    if (key == 'e')
    {
      if (encoding == ENCODING_UTF8)
        encoding = ENCODING_GBK;
      else
        encoding = ENCODING_UTF8;
      continue;
    }
  }
  if (file)
    grub_file_close (file);
}
