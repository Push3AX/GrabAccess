/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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
#include <grub/normal.h>
#include <grub/term.h>
#include <grub/menu.h>
#include <grub/command.h>
#include <grub/script_sh.h>
#include <grub/fs.h>
#include <grub/device.h>
#include <grub/file.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/gfxmenu_view.h>

#include <ini.h>

#include "fm.h"

struct grubfm_test_parse_ctx
{
  int exist;
  struct grub_dirhook_info info;
  char *name;
};

/* A hook for iterating directories. */
static int
grubfm_find_file (const char *name,
                  const struct grub_dirhook_info *info, void *data)
{
  struct grubfm_test_parse_ctx *ctx = data;

  if ((info->case_insensitive ?
         grub_strcasecmp (name, ctx->name)
       : grub_strcmp (name, ctx->name)) == 0)
  {
    ctx->info = *info;
    ctx->exist = 1;
    return 1;
  }
  return 0;
}

/* Check if file exists and fetch its information. */
static void
grubfm_get_fileinfo (const char *path, struct grubfm_test_parse_ctx *ctx)
{
  char *pathname;
  char *device_name;
  grub_fs_t fs;
  grub_device_t dev;
  char *tmp_path = grub_strdup (path);

  ctx->exist = 0;
  device_name = grub_file_get_device_name (tmp_path);
  dev = grub_device_open (device_name);
  if (! dev)
  {
    grub_free (device_name);
    return;
  }

  fs = grub_fs_probe (dev);
  if (! fs)
  {
    grub_free (device_name);
    grub_device_close (dev);
    return;
  }

  pathname = grub_strchr (tmp_path, ')');
  if (! pathname)
    pathname = tmp_path;
  else
    pathname++;

  /* Remove trailing '/'. */
  while (*pathname && pathname[grub_strlen (pathname) - 1] == '/')
    pathname[grub_strlen (pathname) - 1] = 0;

  /* Split into path and filename. */
  char *t;
  ctx->name = grub_strrchr (pathname, '/');
  if (! ctx->name)
  {
    t = grub_strdup ("/");
    ctx->name = pathname;
  }
  else
  {
    ctx->name++;
    t = grub_strdup (pathname);
    t[ctx->name - pathname] = 0;
  }

  /* It's the whole device. */
  if (! *pathname)
  {
    ctx->exist = 1;
    grub_memset (&ctx->info, 0, sizeof (ctx->info));
    /* Root is always a directory. */
    ctx->info.dir = 1;
  }
  else
    (fs->fs_dir) (dev, t, grubfm_find_file, ctx);

  if (t)
    grub_free (t);
  grub_device_close (dev);
  if (tmp_path)
    grub_free (tmp_path);
  grub_free (device_name);
}

int
grubfm_dir_exist (const char *fmt, ...)
{
  struct grubfm_test_parse_ctx ctx = {.exist = 0};
  va_list ap;
  char *path = NULL;
  va_start (ap, fmt);
  path = grub_xvasprintf(fmt, ap);
  va_end (ap);
  if (!path)
    return 0;
  grubfm_get_fileinfo (path, &ctx);
  grub_free (path);
  if (ctx.exist && ctx.info.dir)
    return 1;
  else
    return 0;
}

int
grubfm_file_exist (const char *fmt, ...)
{
  struct grubfm_test_parse_ctx ctx = {.exist = 0};
  va_list ap;
  char *path = NULL;
  va_start (ap, fmt);
  path = grub_xvasprintf(fmt, ap);
  va_end (ap);
  if (!path)
    return 0;
  grubfm_get_fileinfo (path, &ctx);
  grub_free (path);
  if (ctx.exist && !ctx.info.dir)
    return 1;
  else
    return 0;
}

void
grubfm_clear_menu (void)
{
  grub_normal_clear_menu ();
}

static void
add_menu (const char *title, const char *icon, const char *id,
                 const char *hotkey, const char *src, int hidden)
{
  char **args = NULL;
  char **class = NULL;
  grub_uint8_t flag = hidden ? 0x02 : 0x00;
  args = grub_malloc (sizeof (args[0]));
  if (!args)
    return;
  args[0] = grub_strdup (title);
  if (icon)
  {
    class = grub_malloc (2 * sizeof (class[0]));
    if (!class)
      return;
    class[0] = grub_strdup (icon);
    class[1] = NULL;
    grub_normal_add_menu_entry (1, (const char **) args, class, id, NULL, hotkey,
                                NULL, src, NULL, flag, NULL, NULL);
    if (class[0])
      grub_free (class[0]);
    if (class)
      grub_free (class);
  }
  else
    grub_normal_add_menu_entry (1, (const char **) args, NULL, id, NULL, hotkey,
                                NULL, src, NULL, flag, NULL, NULL);
  if (args[0])
    grub_free (args[0]);
  grub_free (args);
}

void
grubfm_add_menu (const char *title, const char *icon,
                 const char *hotkey, const char *src, int hidden)
{
  add_menu (title, icon, NULL, hotkey, src, hidden);
}

void
grubfm_add_file_menu (const char *title, const char *icon,
                      const char *file, const char *src)
{
  add_menu (title, icon, file, NULL, src, 0);
}

int
grubfm_command_exist (const char *str)
{
  return grub_command_find (str)? 1: 0;
}

grub_err_t
grubfm_run_cmd (const char *cmdline)
{
  int n;
  char ** args;
  grub_err_t errno = GRUB_ERR_NONE;
  if ((! grub_parser_split_cmdline (cmdline, 0, 0, &n, &args))
      && (n >= 0))
  {
    grub_command_t cmd;

    cmd = grub_command_find (args[0]);
    if (cmd)
      errno = (cmd->func) (cmd, n-1, &args[1]);
    else
      errno = GRUB_ERR_UNKNOWN_COMMAND;

    grub_free (args[0]);
    grub_free (args);
  }

  return errno;
}

grub_video_color_t
grubfm_get_color (grub_uint8_t red, grub_uint8_t green, grub_uint8_t blue)
{
  return grub_video_map_rgba (red, green, blue, 255);
}

void
grubfm_get_screen_info (unsigned int *width, unsigned int *height)
{
  struct grub_video_mode_info info;
  *width = *height = 0;
  if (grub_video_get_info (&info) == GRUB_ERR_NONE)
  {
    *width = info.width;
    *height = info.height;
  }
}

void
grubfm_gfx_printf (grub_video_color_t color, int x, int y, const char *fmt, ...)
{
  va_list ap;
  char *str = NULL;

  va_start (ap, fmt);
  str = grub_xvasprintf (fmt, ap);
  va_end (ap);

  if (str)
  {
    grub_font_draw_string (str, grub_font_get ("Unifont Regular 16"), color, x, y);
    grub_free (str);
  }
}

void
grubfm_gfx_clear (void)
{
  unsigned int w, h;
  grub_video_color_t black = grubfm_get_color (0, 0, 0);
  grubfm_get_screen_info (&w, &h);
  if (!w || !h)
    return;
  grubfm_draw_rect (black, 0, 0, w, h);
}

void grubfm_src_exe (const char *fmt, ...)
{
  va_list ap;
  char *src = NULL;
  va_start (ap, fmt);
  src = grub_xvasprintf(fmt, ap);
  va_end (ap);
  if (!src)
    return;
  grub_script_execute_sourcecode (src);
  grub_free (src);
}
