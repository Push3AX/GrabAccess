/* ubootdisk.c - disk subsystem support for U-Boot platforms */
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

#include <grub/disk.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/partition.h>
#include <grub/term.h>
#include <grub/types.h>
#include <grub/uboot/disk.h>
#include <grub/uboot/uboot.h>
#include <grub/uboot/api_public.h>

static struct ubootdisk_data *hd_devices;
static int hd_num;
static int hd_max;

/*
 * grub_ubootdisk_register():
 *   Called for each disk device enumerated as part of U-Boot initialization
 *   code.
 */
grub_err_t
grub_ubootdisk_register (struct device_info *newdev)
{
  struct ubootdisk_data *d;

#define STOR_TYPE(x) ((x) & 0x0ff0)
  switch (STOR_TYPE (newdev->type))
    {
    case DT_STOR_IDE:
    case DT_STOR_SATA:
    case DT_STOR_SCSI:
    case DT_STOR_MMC:
    case DT_STOR_USB:
      /* hd */
      if (hd_num == hd_max)
	{
	  int new_num;
	  new_num = (hd_max ? hd_max * 2 : 1);
	  d = grub_realloc(hd_devices,
			   sizeof (struct ubootdisk_data) * new_num);
	  if (!d)
	    return grub_errno;
	  hd_devices = d;
	  hd_max = new_num;
	}

      d = &hd_devices[hd_num];
      hd_num++;
      break;
    default:
      return GRUB_ERR_BAD_DEVICE;
      break;
    }

  d->dev = newdev;
  d->cookie = newdev->cookie;
  d->opencount = 0;

  return 0;
}

/*
 * uboot_disk_iterate():
 *   Iterator over enumerated disk devices.
 */
static int
uboot_disk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		    grub_disk_pull_t pull)
{
  char buf[16];
  int count;

  switch (pull)
    {
    case GRUB_DISK_PULL_NONE:
      /* "hd" - built-in mass-storage */
      for (count = 0 ; count < hd_num; count++)
	{
	  grub_snprintf (buf, sizeof (buf) - 1, "hd%d", count);
	  grub_dprintf ("ubootdisk", "iterating %s\n", buf);
	  if (hook (buf, hook_data))
	    return 1;
	}
      break;
    default:
      return 0;
    }

  return 0;
}

/* Helper function for uboot_disk_open. */
static struct ubootdisk_data *
get_hd_device (int num)
{
  if (num < hd_num)
    return &hd_devices[num];

  return NULL;
}

/*
 * uboot_disk_open():
 *   Opens a disk device already enumerated.
 */
static grub_err_t
uboot_disk_open (const char *name, struct grub_disk *disk)
{
  struct ubootdisk_data *d;
  struct device_info *devinfo;
  int num;
  int retval;

  grub_dprintf ("ubootdisk", "Opening '%s'\n", name);

  num = grub_strtoul (name + 2, 0, 10);
  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_dprintf ("ubootdisk", "Opening '%s' failed, invalid number\n",
		    name);
      goto fail;
    }

  if (name[1] != 'd')
    {
      grub_dprintf ("ubootdisk", "Opening '%s' failed, invalid name\n", name);
      goto fail;
    }

  switch (name[0])
    {
    case 'h':
      d = get_hd_device (num);
      break;
    default:
      goto fail;
    }

  if (!d)
    goto fail;

  /*
   * Subsystems may call open on the same device recursively - but U-Boot
   * does not deal with this. So simply keep track of number of calls and
   * return success if already open.
   */
  if (d->opencount > 0)
    {
      grub_dprintf ("ubootdisk", "(%s) already open\n", disk->name);
      d->opencount++;
      retval = 0;
    }
  else
    {
      retval = grub_uboot_dev_open (d->dev);
      if (retval != 0)
	goto fail;
      d->opencount = 1;
    }

  grub_dprintf ("ubootdisk", "cookie: 0x%08x\n", (grub_addr_t) d->cookie);
  disk->id = (grub_addr_t) d->cookie;

  devinfo = d->dev;

  d->block_size = devinfo->di_stor.block_size;
  if (d->block_size == 0)
    return grub_error (GRUB_ERR_IO, "no block size");

  for (disk->log_sector_size = 0;
       (1U << disk->log_sector_size) < d->block_size;
       disk->log_sector_size++);

  grub_dprintf ("ubootdisk", "(%s) blocksize=%d, log_sector_size=%d\n",
		disk->name, d->block_size, disk->log_sector_size);

  if (devinfo->di_stor.block_count)
    disk->total_sectors = devinfo->di_stor.block_count;
  else
    disk->total_sectors = GRUB_DISK_SIZE_UNKNOWN;

  disk->data = d;

  return GRUB_ERR_NONE;

fail:
  return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such device");
}

static void
uboot_disk_close (struct grub_disk *disk)
{
  struct ubootdisk_data *d;
  int retval;

  d = disk->data;

  /*
   * In mirror of open function, keep track of number of calls to close and
   * send on to U-Boot only when opencount would decrease to 0.
   */
  if (d->opencount > 1)
    {
      grub_dprintf ("ubootdisk", "Closed (%s)\n", disk->name);

      d->opencount--;
    }
  else if (d->opencount == 1)
    {
      retval = grub_uboot_dev_close (d->dev);
      d->opencount--;
      grub_dprintf ("ubootdisk", "closed %s (%d)\n", disk->name, retval);
    }
  else
    {
      grub_dprintf ("ubootdisk", "device %s not open!\n", disk->name);
    }
}

/*
 * uboot_disk_read():
 *   Called from within disk subsystem to read a sequence of blocks into the
 *   disk cache. Maps directly on top of U-Boot API, only wrap in some error
 *   handling.
 */
static grub_err_t
uboot_disk_read (struct grub_disk *disk,
		 grub_disk_addr_t offset, grub_size_t numblocks, char *buf)
{
  struct ubootdisk_data *d;
  grub_size_t real_size;
  int retval;

  d = disk->data;

  retval = grub_uboot_dev_read (d->dev, buf, numblocks, offset, &real_size);
  grub_dprintf ("ubootdisk",
		"retval=%d, numblocks=%d, real_size=%llu, sector=%llu\n",
		retval, numblocks, (grub_uint64_t) real_size,
		(grub_uint64_t) offset);
  if (retval != 0)
    return grub_error (GRUB_ERR_IO, "U-Boot disk read error");

  return GRUB_ERR_NONE;
}

static grub_err_t
uboot_disk_write (struct grub_disk *disk,
		  grub_disk_addr_t offset, grub_size_t numblocks, const char *buf)
{
  struct ubootdisk_data *d;
  int retval;

  d = disk->data;

  retval = grub_uboot_dev_write (d->dev, buf, numblocks, offset);
  grub_dprintf ("ubootdisk",
		"retval=%d, numblocks=%d, sector=%llu\n",
		retval, numblocks, (grub_uint64_t) offset);

  if (retval != 0)
    return grub_error (GRUB_ERR_IO, "U-Boot disk write error");

  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_ubootdisk_dev = {
  .name = "ubootdisk",
  .id = GRUB_DISK_DEVICE_UBOOTDISK_ID,
  .disk_iterate = uboot_disk_iterate,
  .disk_open = uboot_disk_open,
  .disk_close = uboot_disk_close,
  .disk_read = uboot_disk_read,
  .disk_write = uboot_disk_write,
  .next = 0
};

void
grub_ubootdisk_init (void)
{
  grub_disk_dev_register (&grub_ubootdisk_dev);
}

void
grub_ubootdisk_fini (void)
{
  grub_disk_dev_unregister (&grub_ubootdisk_dev);
}
