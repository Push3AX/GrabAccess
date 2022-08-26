/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/device.h>
#include <grub/mm.h>
#include <grub/fs.h>
#include <grub/env.h>
#include <grub/file.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const char *modnames_def[] = { 
  /* FIXME: autogenerate this.  */
#if defined (__i386__) || defined (__x86_64__) || defined (GRUB_MACHINE_MIPS_LOONGSON)
  "pata", "ahci", "usbms", "ohci", "uhci", "ehci"
#elif defined (GRUB_MACHINE_MIPS_QEMU_MIPS)
  "pata"
#else
#error "Fill this"
#endif
 };

static grub_err_t
get_uuid (const char *name, char **uuid, int getnative)
{
  grub_device_t dev;
  grub_fs_t fs = 0;

  *uuid = 0;

  dev = grub_device_open (name);
  if (!dev)
    return grub_errno;

  if (!dev->disk)
    {
      grub_dprintf ("nativedisk", "Skipping non-disk\n");
      grub_device_close (dev);
      return 0;
    }

  switch (dev->disk->dev->id)
    {
      /* Firmware disks.  */
    case GRUB_DISK_DEVICE_BIOSDISK_ID:
    case GRUB_DISK_DEVICE_OFDISK_ID:
    case GRUB_DISK_DEVICE_OBDISK_ID:
    case GRUB_DISK_DEVICE_EFIDISK_ID:
    case GRUB_DISK_DEVICE_NAND_ID:
    case GRUB_DISK_DEVICE_ARCDISK_ID:
    case GRUB_DISK_DEVICE_HOSTDISK_ID:
    case GRUB_DISK_DEVICE_UBOOTDISK_ID:
      break;

      /* Native disks.  */
    case GRUB_DISK_DEVICE_ATA_ID:
    case GRUB_DISK_DEVICE_SCSI_ID:
    case GRUB_DISK_DEVICE_XEN:
      if (getnative)
	break;
      /* FALLTHROUGH */

      /* Virtual disks.  */
      /* GRUB dynamically generated files.  */
    case GRUB_DISK_DEVICE_PROCFS_ID:
      /* To access through host OS routines (grub-emu only).  */
    case GRUB_DISK_DEVICE_HOST_ID:
      /* To access coreboot roms.  */
    case GRUB_DISK_DEVICE_CBFSDISK_ID:
      /* GRUB-only memdisk. Can't match any of firmware devices.  */
    case GRUB_DISK_DEVICE_MEMDISK_ID:
      grub_dprintf ("nativedisk", "Skipping native disk %s\n",
		    dev->disk->name);
      grub_device_close (dev);
      return 0;

      /* FIXME: those probably need special handling.  */
    case GRUB_DISK_DEVICE_LOOPBACK_ID:
    case GRUB_DISK_DEVICE_DISKFILTER_ID:
    case GRUB_DISK_DEVICE_CRYPTODISK_ID:
    case GRUB_DISK_DEVICE_VHD_ID:
    case GRUB_DISK_DEVICE_VFAT_ID:
    default:
      break;
    }
  if (dev)
    fs = grub_fs_probe (dev);
  if (!fs)
    {
      grub_device_close (dev);
      return grub_errno;
    }
  if (!fs->fs_uuid || fs->fs_uuid (dev, uuid) || !*uuid)
    {
      grub_device_close (dev);

      if (!grub_errno)
	grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		    N_("%s does not support UUIDs"), fs->name);

      return grub_errno;
    }
  grub_device_close (dev);
  return GRUB_ERR_NONE;
}

struct search_ctx
{
  char *root_uuid;
  char *prefix_uuid;
  const char *prefix_path;
  int prefix_found, root_found;
};

static int
iterate_device (const char *name, void *data)
{
  struct search_ctx *ctx = data;
  char *cur_uuid;

  if (get_uuid (name, &cur_uuid, 1))
    {
      if (grub_errno == GRUB_ERR_UNKNOWN_FS)
	grub_errno = 0;
      grub_print_error ();
      return 0;
    }

  grub_dprintf ("nativedisk", "checking %s: %s\n", name,
		cur_uuid);
  if (ctx->prefix_uuid && grub_strcasecmp (cur_uuid, ctx->prefix_uuid) == 0)
    {
      char *prefix;
      prefix = grub_xasprintf ("(%s)/%s", name, ctx->prefix_path);
      grub_env_set ("prefix", prefix);
      grub_free (prefix);
      ctx->prefix_found = 1;
    }
  if (ctx->root_uuid && grub_strcasecmp (cur_uuid, ctx->root_uuid) == 0)
    {
      grub_env_set ("root", name);
      ctx->root_found = 1;
    }
  return ctx->prefix_found && ctx->root_found;
}

static grub_err_t
grub_cmd_nativedisk (grub_command_t cmd __attribute__ ((unused)),
		     int argc, char **args_in)
{
  char *uuid_root = 0, *uuid_prefix, *prefdev = 0;
  const char *prefix = 0;
  const char *path_prefix = 0;
  int mods_loaded = 0;
  grub_dl_t *mods;
  const char **args;
  int i;

  if (argc == 0)
    {
      argc = ARRAY_SIZE (modnames_def);
      args = modnames_def;
    }
  else
    args = (const char **) args_in;

  prefix = grub_env_get ("prefix");

  if (! prefix)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("variable `%s' isn't set"), "prefix");

  if (prefix)
    path_prefix = (prefix[0] == '(') ? grub_strchr (prefix, ')') : NULL;
  if (path_prefix)
    path_prefix++;
  else
    path_prefix = prefix;

  mods = grub_calloc (argc, sizeof (mods[0]));
  if (!mods)
    return grub_errno;

  if (get_uuid (NULL, &uuid_root, 0))
    {
      grub_free (mods);
      return grub_errno;
    }

  prefdev = grub_file_get_device_name (prefix);
  if (grub_errno)
    {
      grub_print_error ();
      prefdev = 0;
    }

  if (get_uuid (prefdev, &uuid_prefix, 0))
    {
      grub_free (uuid_root);
      grub_free (prefdev);
      grub_free (mods);
      return grub_errno;
    }

  grub_dprintf ("nativedisk", "uuid_prefix = %s, uuid_root = %s\n",
		uuid_prefix, uuid_root);

  for (mods_loaded = 0; mods_loaded < argc; mods_loaded++)
    {
      char *filename;
      grub_dl_t mod;
      grub_file_t file = NULL;
      grub_ssize_t size;
      void *core = 0;

      mod = grub_dl_get (args[mods_loaded]);
      if (mod)
	{
	  mods[mods_loaded] = 0;
	  continue;
	}

      filename = grub_xasprintf ("%s/" GRUB_TARGET_CPU "-" GRUB_PLATFORM "/%s.mod",
				 prefix, args[mods_loaded]);
      if (! filename)
	goto fail;

      file = grub_file_open (filename,
			     GRUB_FILE_TYPE_GRUB_MODULE);
      grub_free (filename);
      if (! file)
	goto fail;

      size = grub_file_size (file);
      core = grub_malloc (size);
      if (! core)
	{
	  grub_file_close (file);
	  goto fail;
	}

      if (grub_file_read (file, core, size) != (grub_ssize_t) size)
	{
	  grub_file_close (file);
	  grub_free (core);
	  goto fail;
	}

      grub_file_close (file);

      mods[mods_loaded] = grub_dl_load_core_noinit (core, size);
      if (! mods[mods_loaded])
	goto fail;
    }

  for (i = 0; i < argc; i++)
    if (mods[i])
      grub_dl_init (mods[i]);

  if (uuid_prefix || uuid_root)
    {
      struct search_ctx ctx;
      grub_fs_autoload_hook_t saved_autoload;

      /* No need to autoload FS since obviously we already have the necessary fs modules.  */
      saved_autoload = grub_fs_autoload_hook;
      grub_fs_autoload_hook = 0;

      ctx.root_uuid = uuid_root;
      ctx.prefix_uuid = uuid_prefix;
      ctx.prefix_path = path_prefix;
      ctx.prefix_found = !uuid_prefix;
      ctx.root_found = !uuid_root;

      /* FIXME: try to guess the correct values.  */
      grub_device_iterate (iterate_device, &ctx);

      grub_fs_autoload_hook = saved_autoload;
    }
  grub_free (uuid_root);
  grub_free (uuid_prefix);
  grub_free (prefdev);
  grub_free (mods);

  return GRUB_ERR_NONE;

 fail:
  grub_free (uuid_root);
  grub_free (uuid_prefix);
  grub_free (prefdev);

  for (i = 0; i < mods_loaded; i++)
    if (mods[i])
      {
	mods[i]->fini = 0;
	grub_dl_unload (mods[i]);
      }
  grub_free (mods);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(nativedisk)
{
  cmd = grub_register_command ("nativedisk", grub_cmd_nativedisk, N_("[MODULE1 MODULE2 ...]"),
			       N_("Switch to native disk drivers. If no modules are specified default set (pata,ahci,usbms,ohci,uhci,ehci) is used"));
}

GRUB_MOD_FINI(nativedisk)
{
  grub_unregister_command (cmd);
}
