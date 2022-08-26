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
#include <grub/file.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/normal.h>
#include <grub/command.h>
#include <grub/script_sh.h>
#ifdef GRUB_MACHINE_MULTIBOOT
#include <grub/machine/kernel.h>
#endif

#include "fm.h"

ini_t *grubfm_ini_config = NULL;
ini_t *grubfm_usr_config = NULL;

static void
grubfm_add_menu_back (const char *filename)
{
  char *dir = NULL;
  char *src = NULL;
  dir = grub_strdup (filename);
  *grub_strrchr (dir, '/') = 0;

  src = grub_xasprintf ("grubfm \"%s/\"", dir);

  grubfm_add_menu (_("Back"), "go-previous", NULL, src, 0);
  grub_free (src);
  if (dir)
    grub_free (dir);
}

static int
grubfm_ini_menu_check (const char *condition, const char *dev)
{
  const char *value = NULL;
  grubfm_src_exe ("unset grubfm_test\n"
                  "%s (%s)%srules/%s\n",
                  grubfm_islua (condition)? "lua": "source",
                  dev, grubfm_data_path, condition);
  value = grub_env_get ("grubfm_test");
  if (!value)
    return 0;
  if (grub_strcmp(value, "0") == 0)
    return 0;
  return 1;
}

static void
grubfm_add_ini_menu (ini_t *ini, const char *dev)
{
  int i;
  char num[4] = "0";
  for (i = 0; i < 100; i++)
  {
    grub_sprintf (num, "%d", i);
    int hidden = 0;
    char *src = NULL;
    const char *script = NULL;
    const char *condition = NULL;
    const char *icon = NULL;
    const char *title = NULL;
    const char *hotkey = NULL;
#ifdef GRUB_MACHINE_EFI
    char platform = 'e';
#elif defined (GRUB_MACHINE_PCBIOS)
    char platform = 'b';
#elif defined (GRUB_MACHINE_MULTIBOOT)
    char platform = 'u';
    if (grub_mb_check_bios_int (0x13))
      platform = 'b';
#else
    char platform = 'u';
#endif
    const char *enable = NULL;
    /* menu */
    script = ini_get (ini, num, "menu");
    if (! script)
      break;
    /* enable = all|efi|bios */
    enable = ini_get (ini, num, "enable");
    if (enable && enable[0] != 'a' && enable[0] != platform)
      continue;
    /* condition (iftitle) */
    condition = ini_get (ini, num, "condition");
    if (condition && ! grubfm_ini_menu_check (condition, dev))
      continue;
    /* icon default=file */
    icon = ini_get (ini, num, "icon");
    if (! icon)
      icon = "file";
    /* menu title */
    title = ini_get (ini, num, "title");
    if (! title)
      title = "MENU";
    /* hotkey */
    hotkey = ini_get (ini, num, "hotkey");
    src = grub_xasprintf ("%s (%s)%srules/%s\n",
                          grubfm_islua (script)? "lua": "configfile",
                          dev, grubfm_data_path, script);
    /* hidden menu */
    if (ini_get (ini, num, "hidden"))
      hidden = 1;
    grubfm_add_menu (_(title), icon, hotkey, src, hidden);
    if (src)
      grub_free (src);
  }
}

static void
grubfm_check_boot (struct grubfm_enum_file_info *info,
                   struct grubfm_ini_enum_list *ctx, const char *dev)
{
  if (grubfm_boot && info->ext >= 0)
  {
    const char *boot_script = NULL;
    boot_script = ini_get (ctx->config[info->ext], "type", "boot");
    if (boot_script)
      grubfm_src_exe ("%s (%s)%srules/%s\n",
                      grubfm_islua (boot_script)? "lua": "source",
                      dev, grubfm_data_path, boot_script);
  }
}

void
grubfm_open_file (char *path)
{
  grubfm_add_menu_back (path);
  struct grubfm_enum_file_info info;
  struct grubfm_ini_enum_list *ctx = &grubfm_ext_table;
  struct grubfm_ini_enum_list *ctx_usr = &grubfm_usr_table;
  grub_file_t file = 0;
  file = grub_file_open (path, GRUB_FILE_TYPE_GET_SIZE |
                           GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
    return;
  info.name = file->name;
  info.size = (char *) grub_get_human_size (file->size,
                                            GRUB_HUMAN_SIZE_SHORT);

  grubfm_get_file_icon (&info, ctx_usr);
  grubfm_check_boot (&info, ctx_usr, grubfm_user);
  if (info.ext >= 0)
  {
    grubfm_add_ini_menu (ctx_usr->config[info.ext], grubfm_user);
    goto generic;
  }

  grubfm_get_file_icon (&info, ctx);
  grubfm_check_boot (&info, ctx, grubfm_root);
  if (info.ext >= 0)
    grubfm_add_ini_menu (ctx->config[info.ext], grubfm_root);

generic:
  if (grubfm_usr_config)
    grubfm_add_ini_menu (grubfm_usr_config, grubfm_user);
  if (grubfm_ini_config)
    grubfm_add_ini_menu (grubfm_ini_config, grubfm_root);

  grub_file_close (file);
}
