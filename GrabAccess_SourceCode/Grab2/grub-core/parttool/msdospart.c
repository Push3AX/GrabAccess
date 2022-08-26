/* pcpart.c - manipulate fdisk partitions */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/msdos_partition.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/parttool.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv2+");

static int activate_table_handle = -1;
static int type_table_handle = -1;

static struct grub_parttool_argdesc grub_pcpart_bootargs[] =
{
  {"boot", N_("Make partition active"), GRUB_PARTTOOL_ARG_BOOL},
  {0, 0, 0}
};

static grub_err_t grub_pcpart_boot (const grub_device_t dev,
				    const struct grub_parttool_args *args)
{
  int i, index;
  grub_partition_t part;
  struct grub_msdos_partition_mbr mbr;

  if (dev->disk->partition->offset)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("not a primary partition"));

  index = dev->disk->partition->index;
  part = dev->disk->partition;
  dev->disk->partition = part->parent;

  /* Read the MBR.  */
  if (grub_disk_read (dev->disk, 0, 0, sizeof (mbr), &mbr))
    {
      dev->disk->partition = part;
      return grub_errno;
    }

  if (args[0].set && args[0].bool)
    {
      for (i = 0; i < 4; i++)
	mbr.entries[i].flag = 0x0;
      mbr.entries[index].flag = 0x80;
      grub_printf_ (N_("Partition %d is active now. \n"), index);
    }
  else
    {
      mbr.entries[index].flag = 0x0;
      grub_printf (N_("Cleared active flag on %d. \n"), index);
    }

   /* Write the MBR.  */
  grub_disk_write (dev->disk, 0, 0, sizeof (mbr), &mbr);

  dev->disk->partition = part;

  return GRUB_ERR_NONE;
}

static struct grub_parttool_argdesc grub_pcpart_typeargs[] =
{
  {"type", N_("Change partition type"), GRUB_PARTTOOL_ARG_VAL},
  {"hidden", N_("Set `hidden' flag in partition type"), GRUB_PARTTOOL_ARG_BOOL},
  {0, 0, 0}
};

static grub_err_t grub_pcpart_type (const grub_device_t dev,
				    const struct grub_parttool_args *args)
{
  int index;
  grub_uint8_t type;
  grub_partition_t part;
  struct grub_msdos_partition_mbr mbr;

  index = dev->disk->partition->index;
  part = dev->disk->partition;
  dev->disk->partition = part->parent;

  /* Read the parttable.  */
  if (grub_disk_read (dev->disk, part->offset, 0,
		      sizeof (mbr), &mbr))
    {
      dev->disk->partition = part;
      return grub_errno;
    }

  if (args[0].set)
    type = grub_strtoul (args[0].str, 0, 0);
  else
    type = mbr.entries[index].type;

  if (args[1].set)
    {
      if (args[1].bool)
	type |= GRUB_PC_PARTITION_TYPE_HIDDEN_FLAG;
      else
	type &= ~GRUB_PC_PARTITION_TYPE_HIDDEN_FLAG;
    }

  if (grub_msdos_partition_is_extended (type))
    {
      dev->disk->partition = part;
      return grub_error (GRUB_ERR_BAD_ARGUMENT,
			 N_("the partition type 0x%x isn't "
			    "valid"), type);
    }

  mbr.entries[index].type = type;
  /* TRANSLATORS: In this case we're actually writing to the disk and actively
     modifying partition type rather than just defining it.  */
  grub_printf_ (N_("Setting partition type to 0x%x\n"), type);

   /* Write the parttable.  */
  grub_disk_write (dev->disk, part->offset, 0,
		   sizeof (mbr), &mbr);

  dev->disk->partition = part;

  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT (msdospart)
{
  activate_table_handle = grub_parttool_register ("msdos",
						  grub_pcpart_boot,
						  grub_pcpart_bootargs);
  type_table_handle = grub_parttool_register ("msdos",
					      grub_pcpart_type,
					      grub_pcpart_typeargs);

}
GRUB_MOD_FINI(msdospart)
{
  grub_parttool_unregister (activate_table_handle);
  grub_parttool_unregister (type_table_handle);
}
