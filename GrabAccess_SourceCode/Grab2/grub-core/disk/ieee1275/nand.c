/* nand.c - NAND flash disk access.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009 Free Software Foundation, Inc.
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
#include <grub/dl.h>
#include <grub/ieee1275/ieee1275.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_nand_data
{
  grub_ieee1275_ihandle_t handle;
  grub_uint32_t block_size;
};

static int
grub_nand_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		   grub_disk_pull_t pull)
{
  static int have_nand = -1;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  if (have_nand == -1)
    {
      struct grub_ieee1275_devalias alias;

      have_nand = 0;
      FOR_IEEE1275_DEVALIASES(alias)
	if (grub_strcmp (alias.name, "nand") == 0)
	  {
	    have_nand = 1;
	    break;
	  }
      grub_ieee1275_devalias_free (&alias);
    }

  if (have_nand)
    return hook ("nand", hook_data);

  return 0;
}

static grub_err_t
grub_nand_read (grub_disk_t disk, grub_disk_addr_t sector,
                grub_size_t size, char *buf);

static grub_err_t
grub_nand_open (const char *name, grub_disk_t disk)
{
  grub_ieee1275_ihandle_t dev_ihandle = 0;
  struct grub_nand_data *data = 0;
  const char *devname;
  struct size_args
    {
      struct grub_ieee1275_common_hdr common;
      grub_ieee1275_cell_t method;
      grub_ieee1275_cell_t ihandle;
      grub_ieee1275_cell_t result;
      grub_ieee1275_cell_t size1;
      grub_ieee1275_cell_t size2;
    } args;

  if (grub_memcmp (name, "nand/", sizeof ("nand/") - 1) == 0)
    devname = name + sizeof ("nand/") - 1;
  else if (grub_strcmp (name, "nand") == 0)
    devname = name;
  else
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a NAND device");

  data = grub_malloc (sizeof (*data));
  if (! data)
    goto fail;

  grub_ieee1275_open (devname, &dev_ihandle);
  if (! dev_ihandle)
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");
      goto fail;
    }

  data->handle = dev_ihandle;

  INIT_IEEE1275_COMMON (&args.common, "call-method", 2, 2);
  args.method = (grub_ieee1275_cell_t) "block-size";
  args.ihandle = dev_ihandle;
  args.result = 1;

  if ((IEEE1275_CALL_ENTRY_FN (&args) == -1) || (args.result))
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't get block size");
      goto fail;
    }

  data->block_size = (args.size1 >> GRUB_DISK_SECTOR_BITS);
  if (!data->block_size)
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "invalid block size");
      goto fail;
    }

  INIT_IEEE1275_COMMON (&args.common, "call-method", 2, 3);
  args.method = (grub_ieee1275_cell_t) "size";
  args.ihandle = dev_ihandle;
  args.result = 1;

  if ((IEEE1275_CALL_ENTRY_FN (&args) == -1) || (args.result))
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't get disk size");
      goto fail;
    }

  disk->total_sectors = args.size1;
  disk->total_sectors <<= 32;
  disk->total_sectors += args.size2;
  disk->total_sectors >>= GRUB_DISK_SECTOR_BITS;

  disk->id = dev_ihandle;

  disk->data = data;

  return 0;

fail:
  if (dev_ihandle)
    grub_ieee1275_close (dev_ihandle);
  grub_free (data);
  return grub_errno;
}

static void
grub_nand_close (grub_disk_t disk)
{
  grub_ieee1275_close (((struct grub_nand_data *) disk->data)->handle);
  grub_free (disk->data);
}

static grub_err_t
grub_nand_read (grub_disk_t disk, grub_disk_addr_t sector,
                grub_size_t size, char *buf)
{
  struct grub_nand_data *data = disk->data;
  grub_size_t bsize, ofs;

  struct read_args
    {
      struct grub_ieee1275_common_hdr common;
      grub_ieee1275_cell_t method;
      grub_ieee1275_cell_t ihandle;
      grub_ieee1275_cell_t ofs;
      grub_ieee1275_cell_t page;
      grub_ieee1275_cell_t len;
      grub_ieee1275_cell_t buf;
      grub_ieee1275_cell_t result;
    } args;

  INIT_IEEE1275_COMMON (&args.common, "call-method", 6, 1);
  args.method = (grub_ieee1275_cell_t) "pio-read";
  args.ihandle = data->handle;
  args.buf = (grub_ieee1275_cell_t) buf;
  args.page = (grub_ieee1275_cell_t) ((grub_size_t) sector / data->block_size);

  ofs = ((grub_size_t) sector % data->block_size) << GRUB_DISK_SECTOR_BITS;
  size <<= GRUB_DISK_SECTOR_BITS;
  bsize = (data->block_size << GRUB_DISK_SECTOR_BITS);

  do
    {
      grub_size_t len;

      len = (ofs + size > bsize) ? (bsize - ofs) : size;

      args.len = (grub_ieee1275_cell_t) len;
      args.ofs = (grub_ieee1275_cell_t) ofs;
      args.result = 1;

      if ((IEEE1275_CALL_ENTRY_FN (&args) == -1) || (args.result))
        return grub_error (GRUB_ERR_READ_ERROR, N_("failure reading sector 0x%llx "
						   "from `%s'"),
			   (unsigned long long) sector,
			   disk->name);

      ofs = 0;
      size -= len;
      args.buf += len;
      args.page++;
    } while (size);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_nand_write (grub_disk_t disk __attribute ((unused)),
                 grub_disk_addr_t sector __attribute ((unused)),
                 grub_size_t size __attribute ((unused)),
                 const char *buf __attribute ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		     "nand write is not supported");
}

static struct grub_disk_dev grub_nand_dev =
  {
    .name = "nand",
    .id = GRUB_DISK_DEVICE_NAND_ID,
    .disk_iterate = grub_nand_iterate,
    .disk_open = grub_nand_open,
    .disk_close = grub_nand_close,
    .disk_read = grub_nand_read,
    .disk_write = grub_nand_write,
    .next = 0
  };

GRUB_MOD_INIT(nand)
{
  grub_disk_dev_register (&grub_nand_dev);
}

GRUB_MOD_FINI(nand)
{
  grub_disk_dev_unregister (&grub_nand_dev);
}
