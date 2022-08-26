/* fs.c - filesystem manager */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2007  Free Software Foundation, Inc.
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

#include <grub/disk.h>
#include <grub/net.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/partition.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/i18n.h>

grub_fs_t grub_fs_list = 0;

grub_fs_autoload_hook_t grub_fs_autoload_hook = 0;

/* Helper for grub_fs_probe.  */
static int
probe_dummy_iter (const char *filename __attribute__ ((unused)),
		  const struct grub_dirhook_info *info __attribute__ ((unused)),
		  void *data __attribute__ ((unused)))
{
  return 1;
}

grub_fs_t
grub_fs_probe (grub_device_t device)
{
  grub_fs_t p;

  if (device->disk)
    {
      /* Make it sure not to have an infinite recursive calls.  */
      static int count = 0;

      if (grub_strcmp (device->disk->name, "vfat") == 0)
      {
        grub_dprintf ("fs", "VFAT DISK\n");
        for (p = grub_fs_list; p; p = p->next)
          if (grub_strcmp (p->name, "fat") == 0)
            return p;
      }

      for (p = grub_fs_list; p; p = p->next)
	{
	  grub_dprintf ("fs", "Detecting %s...\n", p->name);

	  /* This is evil: newly-created just mounted BtrFS after copying all
	     GRUB files has a very peculiar unrecoverable corruption which
	     will be fixed at sync but we'd rather not do a global sync and
	     syncing just files doesn't seem to help. Relax the check for
	     this time.  */
#ifdef GRUB_UTIL
	  if (grub_strcmp (p->name, "btrfs") == 0)
	    {
	      char *label = 0;
	      p->fs_uuid (device, &label);
	      if (label)
		grub_free (label);
	    }
	  else
#endif
	    (p->fs_dir) (device, "/", probe_dummy_iter, NULL);
	  if (grub_errno == GRUB_ERR_NONE)
	    return p;

	  grub_error_push ();
	  grub_dprintf ("fs", "%s detection failed.\n", p->name);
	  grub_error_pop ();

	  if (grub_errno != GRUB_ERR_BAD_FS
	      && grub_errno != GRUB_ERR_OUT_OF_RANGE)
	    return 0;

	  grub_errno = GRUB_ERR_NONE;
	}

      /* Let's load modules automatically.  */
      if (grub_fs_autoload_hook && count == 0)
	{
	  count++;

	  while (grub_fs_autoload_hook ())
	    {
	      p = grub_fs_list;

	      (p->fs_dir) (device, "/", probe_dummy_iter, NULL);
	      if (grub_errno == GRUB_ERR_NONE)
		{
		  count--;
		  return p;
		}

	      if (grub_errno != GRUB_ERR_BAD_FS
		  && grub_errno != GRUB_ERR_OUT_OF_RANGE)
		{
		  count--;
		  return 0;
		}

	      grub_errno = GRUB_ERR_NONE;
	    }

	  count--;
	}
    }
  else if (device->net && device->net->fs)
    return device->net->fs;

  grub_error (GRUB_ERR_UNKNOWN_FS, N_("unknown filesystem"));
  return 0;
}

/* Block list support routines.  */
static grub_uint64_t
strtosector (const char * str, const char ** const end)
{
  grub_uint64_t ret = 0;
  if (*str != '[')
  {
    ret = grub_strtoull (str, &str, 0);
    ret <<= GRUB_DISK_SECTOR_BITS;
  }
  if (*str == '[')
  {
    str++;
    ret += grub_strtoull (str, &str, 0);
  }
  if (*str == ']')
    str++;
  if (end)
    *end = (char *) str;
  return ret;
}

static grub_err_t
grub_fs_blocklist_open (grub_file_t file, const char *name)
{
  const char *p = name;
  unsigned num = 0;
  unsigned i;
  grub_disk_t disk = file->device->disk;
  struct grub_fs_block *blocks;
  grub_uint64_t max_sectors;
  grub_uint64_t part_sectors = 0;

  /* First, count the number of blocks.  */
  do
  {
    num++;
    p = grub_strchr (p, ',');
    if (p)
      p++;
  }
  while (p);

  /* Allocate a block list.  */
  blocks = grub_calloc (num + 1, sizeof (struct grub_fs_block));
  if (! blocks)
    return 0;

  file->size = 0;
  max_sectors = grub_disk_from_native_sector (disk, disk->total_sectors);
  p = (char *) name;
  if (! *p)
  {
    blocks[0].offset = 0;
    if (disk->partition)
    {
      part_sectors = grub_disk_from_native_sector (disk, disk->partition->len);
      blocks[0].length = part_sectors << GRUB_DISK_SECTOR_BITS;
    }
    else
      blocks[0].length = max_sectors << GRUB_DISK_SECTOR_BITS;
    file->size = blocks[0].length;
  }
  else for (i = 0; i < num; i++)
  {
    if (*p != '+')
    {
      blocks[i].offset = strtosector (p, &p);
      if (grub_errno != GRUB_ERR_NONE || *p != '+')
      {
        grub_error (GRUB_ERR_BAD_FILENAME, N_("invalid file name `%s'"), name);
        goto fail;
      }
    }

    p++;
    if (*p == '\0' || *p == ',')
      blocks[i].length = (max_sectors << GRUB_DISK_SECTOR_BITS)
                         - blocks[i].offset;
    else
      blocks[i].length = strtosector (p, &p);
    if (grub_errno != GRUB_ERR_NONE
        || blocks[i].length == 0
        || (*p && *p != ',' && ! grub_isspace (*p)))
    {
      grub_error (GRUB_ERR_BAD_FILENAME, N_("invalid file name `%s'"), name);
      goto fail;
    }

    if (max_sectors << GRUB_DISK_SECTOR_BITS <
        blocks[i].offset + blocks[i].length)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "beyond the total sectors");
      goto fail;
    }

    file->size += blocks[i].length;
    p++;
  }

  file->data = blocks;

  return GRUB_ERR_NONE;

fail:
  grub_free (blocks);
  return grub_errno;
}

static grub_ssize_t
grub_fs_blocklist_rw (int write, grub_file_t file, char *buf, grub_size_t len)
{
  struct grub_fs_block *p;
  grub_off_t offset;
  grub_ssize_t ret = 0;

  if (len > file->size - file->offset)
    len = file->size - file->offset;

  offset = file->offset;
  for (p = file->data; p->length && len > 0; p++)
  {
    if (offset < p->length)
    {
      grub_size_t size;

      size = len;
      if (offset + size > p->length)
        size = p->length - offset;

      if ((write ?
          grub_disk_write_weak (file->device->disk, 0, p->offset + offset,
                                size, buf) :
           grub_disk_read_ex (file->device->disk, 0, p->offset + offset,
              size, buf, file->blocklist)) != GRUB_ERR_NONE)
        return -1;

      ret += size;
      len -= size;
      if (buf)
        buf += size;
      offset += size;
    }
    else
      offset -= p->length;
  }

  return ret;
}

static grub_ssize_t
grub_fs_blocklist_read (grub_file_t file, char *buf, grub_size_t len)
{
  grub_ssize_t ret;
  file->device->disk->read_hook = file->read_hook;
  file->device->disk->read_hook_data = file->read_hook_data;
  ret = grub_fs_blocklist_rw (0, file, buf, len);
  file->device->disk->read_hook = 0;
  return ret;
}

grub_ssize_t
grub_blocklist_write (grub_file_t file, const char *buf, grub_size_t len)
{
  return (file->fs != &grub_fs_blocklist) ? -1 :
    grub_fs_blocklist_rw (1, file, (char *) buf, len);
}

static grub_err_t
grub_fs_blocklist_close (grub_file_t file)
{
  grub_free (file->data);
  return grub_errno;
}

#define BLOCKLIST_INC_STEP	8

struct read_blocklist_ctx
{
  int num;
  struct grub_fs_block *blocks;
  grub_off_t total_size;
  grub_disk_addr_t part_start;
};

static void
read_blocklist (grub_disk_addr_t sector, unsigned offset,
                unsigned length, void *ctx)
{
  struct read_blocklist_ctx *c = ctx;
  grub_off_t block_offset;

  block_offset = ((sector - c->part_start) << GRUB_DISK_SECTOR_BITS) + offset;

  if (c->num && (c->blocks[c->num - 1].offset
                 + c->blocks[c->num - 1].length == block_offset))
  {
    c->blocks[c->num - 1].length += length;
    goto quit;
  }

  if ((c->num & (BLOCKLIST_INC_STEP - 1)) == 0)
  {
    c->blocks = grub_realloc (c->blocks, (c->num + BLOCKLIST_INC_STEP) *
                  sizeof (struct grub_fs_block));
    if (! c->blocks)
      return;
  }

  c->blocks[c->num].offset = block_offset;
  c->blocks[c->num].length = length;
  c->num++;

quit:
  c->total_size += length;
}

int
grub_blocklist_offset_convert (grub_file_t file, grub_off_t ofs, grub_off_t len)
{
  struct read_blocklist_ctx c;

  if (!file->device->disk || !file->size || ofs >= file->size)
    return 0;

  file->offset = ofs;
  if (len && len < file->size - ofs)
    file->size = len + ofs;
  c.num = 0;
  c.blocks = 0;
  c.total_size = 0;
  c.part_start = grub_partition_get_start (file->device->disk->partition);
  file->read_hook = read_blocklist;
  file->read_hook_data = &c;
  grub_file_dummy_read (file);
  file->read_hook = 0;
  file->read_hook_data = 0;
  if (grub_errno || (c.total_size != file->size - ofs))
  {
    grub_errno = 0;
    c.num = 0;
    grub_free (c.blocks);
  }
  else
  {
    if (file->fs->fs_close)
      (file->fs->fs_close) (file);
    file->fs = &grub_fs_blocklist;
    file->data = c.blocks;
  }
  return c.num;
}

int
grub_blocklist_convert (grub_file_t file)
{
  int num = grub_blocklist_offset_convert (file, 0, 0);
  file->offset = 0;
  return num;
}

struct grub_fs grub_fs_blocklist =
{
  .name = "blocklist",
  .fs_dir = 0,
  .fs_open = grub_fs_blocklist_open,
  .fs_read = grub_fs_blocklist_read,
  .fs_close = grub_fs_blocklist_close,
  .fast_blocklist = 1,
  .next = 0
};
