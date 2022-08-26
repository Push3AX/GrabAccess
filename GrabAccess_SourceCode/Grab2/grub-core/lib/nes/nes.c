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
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/video.h>

#include <assert.h>
#include <string.h>

#define AGNES_IMPLEMENTATION
#include "agnes.h"

GRUB_MOD_LICENSE ("GPLv3+");
GRUB_MOD_DUAL_LICENSE("MIT");

static int pixel_size = 2;
static int wait_time = 15;
static int tick = 0;

static int
scan (void)
{
  static int key = 0;
  int scan = grub_getkey_noblock ();
  tick++;
  if (scan != key)
    return key = scan;
  else
    return 0;
}

static void *
read_file(const char *filename, size_t *out_len)
{
  grub_file_t fp = grub_file_open(filename, GRUB_FILE_TYPE_CAT);
  if (!fp)
  {
    return NULL;
  }
  size_t file_size = fp->size;

  unsigned char *file_contents = (unsigned char *) malloc (file_size);
  if (!file_contents)
  {
    grub_file_close (fp);
    return NULL;
  }
  grub_file_read (fp, file_contents, file_size);
  grub_file_close (fp);
  *out_len = file_size;
  return file_contents;
}

static void
get_screen_info (unsigned int *width, unsigned int *height)
{
  struct grub_video_mode_info info;
  *width = *height = 0;
  if (grub_video_get_info (&info) == GRUB_ERR_NONE)
  {
    *width = info.width;
    *height = info.height;
  }
}

static void
draw_rect (grub_video_color_t color, int x, int y,
           unsigned int w, unsigned int h)
{
  grub_video_fill_rect (color, x, y, w, h);
}

static grub_video_color_t
get_color (grub_uint8_t r, grub_uint8_t g, grub_uint8_t b, grub_uint8_t a)
{
  return grub_video_map_rgba (r, g, b, a);
}

static void
gfx_clear (void)
{
  unsigned int w, h;
  grub_video_color_t black = get_color (0, 0, 0, 0);
  get_screen_info (&w, &h);
  if (!w || !h)
    return;
  draw_rect (black, 0, 0, w, h);
}

static void
display_pixel (int x, int y, agnes_color_t c)
{
  grub_video_color_t color = get_color (c.r, c.g, c.b, c.a);
  draw_rect (color, x * pixel_size, y * pixel_size, pixel_size, pixel_size);
}

#define KEY_Z     'z'
#define KEY_X     'x'
#define KEY_J     'j'
#define KEY_K     'k'
#define KEY_W     'w'
#define KEY_S     's'
#define KEY_A     'a'
#define KEY_D     'd'
#define KEY_UP    GRUB_TERM_KEY_UP
#define KEY_DOWN  GRUB_TERM_KEY_DOWN
#define KEY_LEFT  GRUB_TERM_KEY_LEFT
#define KEY_RIGHT GRUB_TERM_KEY_RIGHT
#define KEY_ENTER 0x0d
#define KEY_SPACE ' '
#define KEY_SHIFT GRUB_TERM_SHIFT
#define KEY_ESC   GRUB_TERM_ESC

static grub_err_t
grub_cmd_nes (grub_extcmd_context_t ctxt __attribute__ ((unused)),
              int argc, char **argv)
{
  unsigned int w, h;
  pixel_size = 1;
  get_screen_info (&w, &h);
  if (w < 640 || h < 480)
    return grub_error (GRUB_ERR_BAD_OS,
                       N_("gfxmode (minimum resolution 640x480) required"));
  if (!argc)
  {
    printf("Usage: nes game.nes\n");
    return 1;
  }

  if (argc >= 2)
  {
    if (argv[1][0] == '1')
      pixel_size = 1;
    if (argv[1][0] == '2')
      pixel_size = 2;
    else if (argv[1][0] == '3')
      pixel_size = 3;
    else if (argv[1][0] == '4')
      pixel_size = 4;
    else
      pixel_size = 2;
  }
  if (argc >= 3)
    wait_time = strtoul (argv[2], NULL, 0);

  const char *ines_name = argv[0];

  size_t ines_data_size = 0;
  void* ines_data = read_file (ines_name, &ines_data_size);
  if (ines_data == NULL)
  {
    printf ("Reading %s failed.\n", ines_name);
    return 1;
  }

  agnes_t *agnes = agnes_make ();
  if (agnes == NULL)
  {
    printf ("Making agnes failed.\n");
    return 1;
  }

  bool ok = agnes_load_ines_data (agnes, ines_data, ines_data_size);
  if (!ok)
  {
    printf("Loading %s failed.\n", ines_name);
    return 1;
  }

  gfx_clear ();
  agnes_input_t input;

  while (true)
  {
    grub_refresh ();
    int key;

    if ((key = scan ()) || tick > wait_time)
    {
      tick = 0;
      memset (&input, 0, sizeof (agnes_input_t));
      if (key == KEY_ESC)
        goto exit;
      if (key == KEY_J)
        input.a = true;
      if (key == KEY_K)
        input.b = true;
      if (key == KEY_LEFT || key == KEY_A)
        input.left = true;
      if (key == KEY_RIGHT || key == KEY_D)
        input.right = true;
      if (key == KEY_UP || key == KEY_W)
        input.up = true;
      if (key == KEY_DOWN || key == KEY_S)
        input.down = true;
      if (key == KEY_SPACE || key == KEY_Z)
        input.select = true;
      if (key == KEY_ENTER || key == KEY_X)
        input.start = true;
    }
    agnes_set_input(agnes, &input, NULL);

    ok = agnes_next_frame(agnes);
    if (!ok)
    {
      printf("Getting next frame failed.\n");
      return 1;
    }

    for (int y = 0; y < AGNES_SCREEN_HEIGHT; y++)
    {
      for (int x = 0; x < AGNES_SCREEN_WIDTH; x++)
      {
        agnes_color_t c = agnes_get_screen_pixel(agnes, x, y);
        display_pixel (x, y, c);
      }
    }
  }

exit:
  agnes_destroy(agnes);
  if (ines_data)
    grub_free (ines_data);
  return 0;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(nes)
{
  cmd = grub_register_extcmd ("nes", grub_cmd_nes, 0,
                              N_("FILE [PIXEL_SIZE] [WAIT_TIME]"),
                              N_("NES emulator."), 0);
}

GRUB_MOD_FINI(nes)
{
  grub_unregister_extcmd (cmd);
}
