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
#include <grub/device.h>
#include <grub/fs.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/term.h>

#include <ini.h>

#include "fm.h"

struct grubfm_ini_enum_list grubfm_ext_table = {0, 0, NULL, NULL, NULL, NULL, NULL};
struct grubfm_ini_enum_list grubfm_usr_table = {0, 0, NULL, NULL, NULL, NULL, NULL};

static int
grubfm_ini_enum_count (const char *filename __attribute__ ((unused)),
                       const struct grub_dirhook_info *info,
                       void *data)
{
  struct grubfm_ini_enum_list *ctx = data;

  if (!info->dir)
    ctx->n++;
  return 0;
}

static int
grubfm_ini_enum_iter (const char *filename,
                      const struct grub_dirhook_info *info,
                      void *data)
{
  struct grubfm_ini_enum_list *ctx = data;
  char *pathname;
  pathname = grub_xasprintf ("%stypes/%s", grubfm_data_path, filename);
  if (!pathname)
    return 1;
  if (! info->dir)
  {
    ctx->ext[ctx->i] = grub_strdup (filename);
    ctx->i++;
  }
  grub_free (pathname);
  return 0;
}

ini_t *
grubfm_ini_enum (const char *devname, struct grubfm_ini_enum_list *ctx)
{
  grub_fs_t fs;
  char *path = NULL;
  grub_device_t dev = 0;
  ini_t *cfg = NULL;

  path = grub_xasprintf ("%stypes/", grubfm_data_path);
  if (!path)
    goto fail;

  dev = grub_device_open (devname);
  if (!dev)
    goto fail;

  fs = grub_fs_probe (dev);

  if (fs)
  {
    (fs->fs_dir) (dev, path, grubfm_ini_enum_count, ctx);
    ctx->ext = grub_zalloc (ctx->n * sizeof (ctx->ext[0]));
    ctx->display = grub_zalloc (ctx->n * sizeof (ctx->display[0]));
    ctx->icon = grub_zalloc (ctx->n * sizeof (ctx->icon[0]));
    ctx->condition = grub_zalloc (ctx->n * sizeof (ctx->condition[0]));
    ctx->config = grub_zalloc (ctx->n * sizeof (ctx->config[0]));
    (fs->fs_dir) (dev, path, grubfm_ini_enum_iter, ctx);
    for (ctx->i = 0; ctx->i < ctx->n; ctx->i++)
    {
      char *ini_name = NULL;
      ini_name = grub_xasprintf ("(%s)%stypes/%s",
                                 devname, grubfm_data_path, ctx->ext[ctx->i]);
      if (!ini_name)
        break;
      ini_t *config = ini_load (ini_name);
      grub_free (ini_name);
      if (!config)
        continue;
      ctx->display[ctx->i] = ini_get(config, "type", "display")? 1: 0;
      ctx->icon[ctx->i] = grub_strdup (ini_get(config, "type", "icon"));
      const char *condition = NULL;
      condition = ini_get(config, "type", "condition");
      if (condition)
        ctx->condition[ctx->i] = grub_xasprintf ("unset grubfm_test\n"
                        "%s (%s)%srules/%s\n",
                        grubfm_islua (condition)? "lua": "source",
                        devname, grubfm_data_path, condition);
      ctx->config[ctx->i] = config;
    }
  }

  /* generic menu */
  char *ini_name = NULL;
  ini_name = grub_xasprintf ("(%s)%srules/generic.ini", devname, grubfm_data_path);
  if (grubfm_file_exist (ini_name))
    cfg = ini_load (ini_name);
  grub_free (ini_name);

fail:
  if (path)
    grub_free (path);
  if (dev)
    grub_device_close (dev);
  return cfg;
}

const char *
grubfm_get_file_icon (struct grubfm_enum_file_info *info,
                      struct grubfm_ini_enum_list *ctx)
{
  const char *icon = "file";
  if (!info || !info->name)
    goto ret;
  info->ext = -1;
  char *ext = grub_strrchr (info->name, '.');
  if (!ext || *ext == '\0' || *(ext++) == '\0')
    goto ret;

  for (ctx->i = 0; ctx->i < ctx->n; ctx->i++)
  {
    if (grub_strcasecmp (ext, ctx->ext[ctx->i]) == 0)
    {
      icon = ctx->icon[ctx->i];
      info->ext = ctx->i;
      info->condition = ctx->condition[ctx->i];
      info->display = ctx->display[ctx->i];
      break;
    }
  }
ret:
  return icon;
}
