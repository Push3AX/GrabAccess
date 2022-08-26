/* ofdisk.c - Open Firmware disk access.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2004,2006,2007,2008,2009,2011  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/arc/arc.h>
#include <grub/i18n.h>

static grub_arc_fileno_t last_handle = 0;
static char *last_path = NULL;
static int handle_writable = 0;

static int lnum = 0;

struct arcdisk_hash_ent
{
  char *devpath;
  int num;
  struct arcdisk_hash_ent *next;
};

#define ARCDISK_HASH_SZ	8
static struct arcdisk_hash_ent *arcdisk_hash[ARCDISK_HASH_SZ];

static int
arcdisk_hash_fn (const char *devpath)
{
  int hash = 0;
  while (*devpath)
    hash ^= *devpath++;
  return (hash & (ARCDISK_HASH_SZ - 1));
}

static struct arcdisk_hash_ent *
arcdisk_hash_find (const char *devpath)
{
  struct arcdisk_hash_ent *p = arcdisk_hash[arcdisk_hash_fn (devpath)];

  while (p)
    {
      if (!grub_strcmp (p->devpath, devpath))
	break;
      p = p->next;
    }
  return p;
}

static struct arcdisk_hash_ent *
arcdisk_hash_add (char *devpath)
{
  struct arcdisk_hash_ent *p;
  struct arcdisk_hash_ent **head = &arcdisk_hash[arcdisk_hash_fn(devpath)];

  p = grub_malloc (sizeof (*p));
  if (!p)
    return NULL;

  p->devpath = devpath;
  p->next = *head;
  p->num = lnum++;
  *head = p;
  return p;
}


/* Context for grub_arcdisk_iterate.  */
struct grub_arcdisk_iterate_ctx
{
  grub_disk_dev_iterate_hook_t hook;
  void *hook_data;
};

/* Helper for grub_arcdisk_iterate.  */
static int
grub_arcdisk_iterate_iter (const char *name,
			   const struct grub_arc_component *comp, void *data)
{
  struct grub_arcdisk_iterate_ctx *ctx = data;

  if (!(comp->type == GRUB_ARC_COMPONENT_TYPE_DISK
	|| comp->type == GRUB_ARC_COMPONENT_TYPE_FLOPPY
	|| comp->type == GRUB_ARC_COMPONENT_TYPE_TAPE))
    return 0;
  return ctx->hook (name, ctx->hook_data);
}

static int
grub_arcdisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		      grub_disk_pull_t pull)
{
  struct grub_arcdisk_iterate_ctx ctx = { hook, hook_data };

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  return grub_arc_iterate_devs (grub_arcdisk_iterate_iter, &ctx, 1);
}

#ifdef GRUB_CPU_MIPSEL
#define RAW_SUFFIX "partition(0)"
#else
#define RAW_SUFFIX "partition(10)"
#endif

static grub_err_t
reopen (const char *name, int writable)
{
  grub_arc_fileno_t handle;

  if (last_path && grub_strcmp (last_path, name) == 0
      && (!writable || handle_writable))
    {
      grub_dprintf ("arcdisk", "using already opened %s\n", name);
      return GRUB_ERR_NONE;
    }
  if (last_path)
    {
      GRUB_ARC_FIRMWARE_VECTOR->close (last_handle);
      grub_free (last_path);
      last_path = NULL;
      last_handle = 0;
      handle_writable = 0;
    }
  if (GRUB_ARC_FIRMWARE_VECTOR->open (name,
				      writable ? GRUB_ARC_FILE_ACCESS_OPEN_RW
				      : GRUB_ARC_FILE_ACCESS_OPEN_RO, &handle))
    {
      grub_dprintf ("arcdisk", "couldn't open %s\n", name);
      return grub_error (GRUB_ERR_IO, "couldn't open %s", name);
    }
  handle_writable = writable;
  last_path = grub_strdup (name);
  if (!last_path)
    return grub_errno;
  last_handle = handle;
  grub_dprintf ("arcdisk", "opened %s\n", name);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_arcdisk_open (const char *name, grub_disk_t disk)
{
  char *fullname;
  grub_err_t err;
  grub_arc_err_t r;
  struct grub_arc_fileinfo info;
  struct arcdisk_hash_ent *hash;

  if (grub_memcmp (name, "arc/", 4) != 0)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not arc device");
  fullname = grub_arc_alt_name_to_norm (name, RAW_SUFFIX);
  disk->data = fullname;
  grub_dprintf ("arcdisk", "opening %s\n", fullname);

  hash = arcdisk_hash_find (fullname);
  if (!hash)
    hash = arcdisk_hash_add (fullname);
  if (!hash)
    return grub_errno;

  err = reopen (fullname, 0);
  if (err)
    return err;

  r = GRUB_ARC_FIRMWARE_VECTOR->getfileinformation (last_handle, &info);
  if (r)
    {
      grub_uint64_t res = 0;
      int i;

      grub_dprintf ("arcdisk", "couldn't retrieve size: %ld\n", r);
      for (i = 40; i >= 9; i--)
	{
	  grub_uint64_t pos = res | (1ULL << i);
	  char buf[512];
	  long unsigned count = 0;
	  grub_dprintf ("arcdisk",
			"seek to 0x%" PRIxGRUB_UINT64_T "\n", pos);
	  if (GRUB_ARC_FIRMWARE_VECTOR->seek (last_handle, &pos, 0))
	    continue;
	  if (GRUB_ARC_FIRMWARE_VECTOR->read (last_handle, buf,
					      0x200, &count))
	    continue;
	  if (count == 0)
	    continue;
	  res |= (1ULL << i);
	}
      grub_dprintf ("arcdisk",
		    "determined disk size 0x%" PRIxGRUB_UINT64_T "\n", res);
      disk->total_sectors = (res + 0x200) >> 9;
    }
  else
    disk->total_sectors = (info.end >> 9);

  disk->id = hash->num;
  return GRUB_ERR_NONE;
}

static void
grub_arcdisk_close (grub_disk_t disk)
{
  grub_free (disk->data);
}

static grub_err_t
grub_arcdisk_read (grub_disk_t disk, grub_disk_addr_t sector,
		   grub_size_t size, char *buf)
{
  grub_err_t err;
  grub_uint64_t pos = sector << 9;
  unsigned long count;
  grub_uint64_t totl = size << 9;
  grub_arc_err_t r;

  err = reopen (disk->data, 0);
  if (err)
    return err;
  r = GRUB_ARC_FIRMWARE_VECTOR->seek (last_handle, &pos, 0);
  if (r)
    {
      grub_dprintf ("arcdisk", "seek to 0x%" PRIxGRUB_UINT64_T " failed: %ld\n",
		    pos, r);
      return grub_error (GRUB_ERR_IO, "couldn't seek");
    }

  while (totl)
    {
      if (GRUB_ARC_FIRMWARE_VECTOR->read (last_handle, buf,
					  totl, &count))
	return grub_error (GRUB_ERR_READ_ERROR,
			   N_("failure reading sector 0x%llx "
			      "from `%s'"),
			   (unsigned long long) sector,
			   disk->name);
      totl -= count;
      buf += count;
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_arcdisk_write (grub_disk_t disk, grub_disk_addr_t sector,
		    grub_size_t size, const char *buf)
{
  grub_err_t err;
  grub_uint64_t pos = sector << 9;
  unsigned long count;
  grub_uint64_t totl = size << 9;
  grub_arc_err_t r;

  err = reopen (disk->data, 1);
  if (err)
    return err;
  r = GRUB_ARC_FIRMWARE_VECTOR->seek (last_handle, &pos, 0);
  if (r)
    {
      grub_dprintf ("arcdisk", "seek to 0x%" PRIxGRUB_UINT64_T " failed: %ld\n",
		    pos, r);
      return grub_error (GRUB_ERR_IO, "couldn't seek");
    }

  while (totl)
    {
      if (GRUB_ARC_FIRMWARE_VECTOR->write (last_handle, buf,
					   totl, &count))
	return grub_error (GRUB_ERR_WRITE_ERROR, N_("failure writing sector 0x%llx "
						    "to `%s'"),
			   (unsigned long long) sector,
			   disk->name);
      totl -= count;
      buf += count;
    }

  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_arcdisk_dev =
  {
    .name = "arcdisk",
    .id = GRUB_DISK_DEVICE_ARCDISK_ID,
    .disk_iterate = grub_arcdisk_iterate,
    .disk_open = grub_arcdisk_open,
    .disk_close = grub_arcdisk_close,
    .disk_read = grub_arcdisk_read,
    .disk_write = grub_arcdisk_write,
    .next = 0
  };

void
grub_arcdisk_init (void)
{
  grub_disk_dev_register (&grub_arcdisk_dev);
}

void
grub_arcdisk_fini (void)
{
  if (last_path)
    {
      GRUB_ARC_FIRMWARE_VECTOR->close (last_handle);
      grub_free (last_path);
      last_path = NULL;
      last_handle = 0;
    }

  grub_disk_dev_unregister (&grub_arcdisk_dev);
}
