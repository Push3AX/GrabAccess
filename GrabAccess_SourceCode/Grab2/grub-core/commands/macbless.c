/* hfspbless.c - set the hfs+ boot directory.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2005,2007,2008,2009,2012,2013  Free Software Foundation, Inc.
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

#include <grub/command.h>
#include <grub/fs.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/hfsplus.h>
#include <grub/hfs.h>
#include <grub/partition.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/err.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct find_node_context
{
  grub_uint64_t inode_found;
  char *dirname;
  enum
  { FOUND_NONE, FOUND_FILE, FOUND_DIR } found;
};

static int
find_inode (const char *filename,
	    const struct grub_dirhook_info *info, void *data)
{
  struct find_node_context *ctx = data;
  if (!info->inodeset)
    return 0;

  if ((grub_strcmp (ctx->dirname, filename) == 0
       || (info->case_insensitive
	   && grub_strcasecmp (ctx->dirname, filename) == 0)))
    {
      ctx->inode_found = info->inode;
      ctx->found = info->dir ? FOUND_DIR : FOUND_FILE;
    }
  return 0;
}

grub_err_t
grub_mac_bless_inode (grub_device_t dev, grub_uint32_t inode, int is_dir,
		      int intel)
{
  grub_err_t err;
  union
  {
    struct grub_hfs_sblock hfs;
    struct grub_hfsplus_volheader hfsplus;
  } volheader;
  grub_disk_addr_t embedded_offset;

  if (intel && is_dir)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "can't bless a directory for mactel");
  if (!intel && !is_dir)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "can't bless a file for mac PPC");

  /* Read the bootblock.  */
  err = grub_disk_read (dev->disk, GRUB_HFSPLUS_SBLOCK, 0, sizeof (volheader),
			(char *) &volheader);
  if (err)
    return err;

  embedded_offset = 0;
  if (grub_be_to_cpu16 (volheader.hfs.magic) == GRUB_HFS_MAGIC)
    {
      int extent_start;
      int ablk_size;
      int ablk_start;

      /* See if there's an embedded HFS+ filesystem.  */
      if (grub_be_to_cpu16 (volheader.hfs.embed_sig) != GRUB_HFSPLUS_MAGIC)
	{
	  if (intel)
	    volheader.hfs.intel_bootfile = grub_be_to_cpu32 (inode);
	  else
	    volheader.hfs.ppc_bootdir = grub_be_to_cpu32 (inode);
	  return GRUB_ERR_NONE;
	}

      /* Calculate the offset needed to translate HFS+ sector numbers.  */
      extent_start =
	grub_be_to_cpu16 (volheader.hfs.embed_extent.first_block);
      ablk_size = grub_be_to_cpu32 (volheader.hfs.blksz);
      ablk_start = grub_be_to_cpu16 (volheader.hfs.first_block);
      embedded_offset = (ablk_start
			 + ((grub_uint64_t) extent_start)
			 * (ablk_size >> GRUB_DISK_SECTOR_BITS));

      err =
	grub_disk_read (dev->disk, embedded_offset + GRUB_HFSPLUS_SBLOCK, 0,
			sizeof (volheader), (char *) &volheader);
      if (err)
	return err;
    }

  if ((grub_be_to_cpu16 (volheader.hfsplus.magic) != GRUB_HFSPLUS_MAGIC)
      && (grub_be_to_cpu16 (volheader.hfsplus.magic) != GRUB_HFSPLUSX_MAGIC))
    return grub_error (GRUB_ERR_BAD_FS, "not a HFS+ filesystem");
  if (intel)
    volheader.hfsplus.intel_bootfile = grub_be_to_cpu32 (inode);
  else
    volheader.hfsplus.ppc_bootdir = grub_be_to_cpu32 (inode);

  return grub_disk_write (dev->disk, embedded_offset + GRUB_HFSPLUS_SBLOCK, 0,
			  sizeof (volheader), (char *) &volheader);
}

grub_err_t
grub_mac_bless_file (grub_device_t dev, const char *path_in, int intel)
{
  grub_fs_t fs;

  char *path, *tail;
  struct find_node_context ctx;

  fs = grub_fs_probe (dev);
  if (!fs || (grub_strcmp (fs->name, "hfsplus") != 0
	      && grub_strcmp (fs->name, "hfs") != 0))
    return grub_error (GRUB_ERR_BAD_FS, "no suitable FS found");

  path = grub_strdup (path_in);
  if (!path)
    return grub_errno;

  tail = path + grub_strlen (path) - 1;

  /* Remove trailing '/'. */
  while (tail != path && *tail == '/')
    *(tail--) = 0;

  tail = grub_strrchr (path, '/');
  ctx.found = 0;

  if (tail)
    {
      *tail = 0;
      ctx.dirname = tail + 1;

      (fs->fs_dir) (dev, *path == 0 ? "/" : path, find_inode, &ctx);
    }
  else
    {
      ctx.dirname = path + 1;
      (fs->fs_dir) (dev, "/", find_inode, &ctx);
    }
  if (!ctx.found)
    {
      grub_free (path);
      return grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("file `%s' not found"),
			 path_in);
    }
  grub_free (path);

  return grub_mac_bless_inode (dev, (grub_uint32_t) ctx.inode_found,
			       (ctx.found == FOUND_DIR), intel);
}

static grub_err_t
grub_cmd_macbless (grub_command_t cmd, int argc, char **args)
{
  char *device_name;
  char *path = 0;
  grub_device_t dev = 0;
  grub_err_t err;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
  device_name = grub_file_get_device_name (args[0]);
  dev = grub_device_open (device_name);

  path = grub_strchr (args[0], ')');
  if (!path)
    path = args[0];
  else
    path = path + 1;

  if (!path || *path == 0 || !dev)
    {
      if (dev)
	grub_device_close (dev);

      grub_free (device_name);

      return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid argument");
    }

  err = grub_mac_bless_file (dev, path, cmd->name[3] == 't');

  grub_device_close (dev);
  grub_free (device_name);
  return err;
}

static grub_command_t cmd, cmd_ppc;

GRUB_MOD_INIT(macbless)
{
  cmd = grub_register_command ("mactelbless", grub_cmd_macbless,
			       N_("FILE"),
			       N_
			       ("Bless FILE of HFS or HFS+ partition for intel macs."));
  cmd_ppc =
    grub_register_command ("macppcbless", grub_cmd_macbless, N_("DIR"),
			   N_
			   ("Bless DIR of HFS or HFS+ partition for PPC macs."));
}

GRUB_MOD_FINI(macbless)
{
  grub_unregister_command (cmd);
  grub_unregister_command (cmd_ppc);
}
