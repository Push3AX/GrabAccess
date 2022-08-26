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

#include <grub/procfs.h>
#include <grub/disk.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/archelp.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_procfs_entry *grub_procfs_entries;

static int
grub_procdev_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
			 grub_disk_pull_t pull)
{
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  return hook ("proc", hook_data);
}

static grub_err_t
grub_procdev_open (const char *name, grub_disk_t disk)
{
  if (grub_strcmp (name, "proc"))
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a procfs disk");

  disk->total_sectors = GRUB_UINT_MAX;
  disk->max_agglomerate = GRUB_DISK_MAX_MAX_AGGLOMERATE;
  disk->id = 0;

  disk->data = 0;

  return GRUB_ERR_NONE;
}

static void
grub_procdev_close (grub_disk_t disk __attribute((unused)))
{
}

static grub_err_t
grub_procdev_read (grub_disk_t disk __attribute((unused)),
		grub_disk_addr_t sector __attribute ((unused)),
		grub_size_t size,
		char *buf)
{
  grub_memset (buf, 0, size << GRUB_DISK_SECTOR_BITS);
  return 0;
}

static grub_err_t
grub_procdev_write (grub_disk_t disk __attribute ((unused)),
		       grub_disk_addr_t sector __attribute ((unused)),
		       grub_size_t size __attribute ((unused)),
		       const char *buf __attribute ((unused)))
{
  return GRUB_ERR_OUT_OF_RANGE;
}

struct grub_archelp_data
{
  struct grub_procfs_entry *entry, *next_entry;
};

static void
grub_procfs_rewind (struct grub_archelp_data *data)
{
  data->entry = NULL;
  data->next_entry = grub_procfs_entries;
}

static grub_err_t
grub_procfs_find_file (struct grub_archelp_data *data, char **name,
		     grub_int32_t *mtime,
		     grub_uint32_t *mode)
{
  data->entry = data->next_entry;
  if (!data->entry)
    {
      *mode = GRUB_ARCHELP_ATTR_END;
      return GRUB_ERR_NONE;
    }
  data->next_entry = data->entry->next;
  *mode = GRUB_ARCHELP_ATTR_FILE | GRUB_ARCHELP_ATTR_NOTIME;
  *name = grub_strdup (data->entry->name);
  *mtime = 0;
  if (!*name)
    return grub_errno;
  return GRUB_ERR_NONE;
}

static struct grub_archelp_ops arcops =
  {
    .find_file = grub_procfs_find_file,
    .rewind = grub_procfs_rewind
  };

static grub_ssize_t
grub_procfs_read (grub_file_t file, char *buf, grub_size_t len)
{
  char *data = file->data;

  grub_memcpy (buf, data + file->offset, len);

  return len;
}

static grub_err_t
grub_procfs_close (grub_file_t file)
{
  char *data;

  data = file->data;
  grub_free (data);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_procfs_dir (grub_device_t device, const char *path,
		 grub_fs_dir_hook_t hook, void *hook_data)
{
  struct grub_archelp_data data;

  /* Check if the disk is our dummy disk.  */
  if (grub_strcmp (device->disk->name, "proc"))
    return grub_error (GRUB_ERR_BAD_FS, "not a procfs");

  grub_procfs_rewind (&data);

  return grub_archelp_dir (&data, &arcops,
			   path, hook, hook_data);
}

static grub_err_t
grub_procfs_open (struct grub_file *file, const char *path)
{
  grub_err_t err;
  struct grub_archelp_data data;
  grub_size_t sz;

  grub_procfs_rewind (&data);

  err = grub_archelp_open (&data, &arcops, path);
  if (err)
    return err;
  file->data = data.entry->get_contents (&sz);
  if (!file->data)
    return grub_errno;
  file->size = sz;
  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_procfs_dev = {
  .name = "proc",
  .id = GRUB_DISK_DEVICE_PROCFS_ID,
  .disk_iterate = grub_procdev_iterate,
  .disk_open = grub_procdev_open,
  .disk_close = grub_procdev_close,
  .disk_read = grub_procdev_read,
  .disk_write = grub_procdev_write,
  .next = 0
};

static struct grub_fs grub_procfs_fs =
  {
    .name = "procfs",
    .fs_dir = grub_procfs_dir,
    .fs_open = grub_procfs_open,
    .fs_read = grub_procfs_read,
    .fs_close = grub_procfs_close,
    .next = 0
  };

GRUB_MOD_INIT (procfs)
{
  grub_disk_dev_register (&grub_procfs_dev);
  grub_fs_register (&grub_procfs_fs);
}

GRUB_MOD_FINI (procfs)
{
  grub_disk_dev_unregister (&grub_procfs_dev);
  grub_fs_unregister (&grub_procfs_fs);
}
