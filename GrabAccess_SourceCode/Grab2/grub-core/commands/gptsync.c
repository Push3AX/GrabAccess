/* gptsync.c - fill the mbr based on gpt entries  */
/* XXX: I don't know what to do if sector size isn't 512 bytes */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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
#include <grub/dl.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/msdos_partition.h>
#include <grub/partition.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/fs.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_gptsync (grub_command_t cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  grub_device_t dev;
  struct grub_msdos_partition_mbr mbr;
  struct grub_partition *partition;
  grub_disk_addr_t first_sector;
  int numactive = 0;
  int i;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "device name required");
  if (argc > 4)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "only 3 partitions can be "
		       "in hybrid MBR");

  if (args[0][0] == '(' && args[0][grub_strlen (args[0]) - 1] == ')')
    {
      args[0][grub_strlen (args[0]) - 1] = 0;
      dev = grub_device_open (args[0] + 1);
      args[0][grub_strlen (args[0])] = ')';
    }
  else
    dev = grub_device_open (args[0]);

  if (! dev)
    return grub_errno;

  if (! dev->disk)
    {
      grub_device_close (dev);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "not a disk");
    }

  /* Read the protective MBR.  */
  if (grub_disk_read (dev->disk, 0, 0, sizeof (mbr), &mbr))
    {
      grub_device_close (dev);
      return grub_errno;
    }

  /* Check if it is valid.  */
  if (mbr.signature != grub_cpu_to_le16_compile_time (GRUB_PC_PARTITION_SIGNATURE))
    {
      grub_device_close (dev);
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "no signature");
    }

  /* Make sure the MBR is a protective MBR and not a normal MBR.  */
  for (i = 0; i < 4; i++)
    if (mbr.entries[i].type == GRUB_PC_PARTITION_TYPE_GPT_DISK)
      break;
  if (i == 4)
    {
      grub_device_close (dev);
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "no GPT partition map found");
    }

  first_sector = dev->disk->total_sectors;
  for (i = 1; i < argc; i++)
    {
      char *separator, csep = 0;
      grub_uint8_t type;
      separator = grub_strchr (args[i], '+');
      if (! separator)
	separator = grub_strchr (args[i], '-');
      if (separator)
	{
	  csep = *separator;
	  *separator = 0;
	}
      partition = grub_partition_probe (dev->disk, args[i]);
      if (separator)
	*separator = csep;
      if (! partition)
	{
	  grub_device_close (dev);
	  return grub_error (GRUB_ERR_UNKNOWN_DEVICE,
			     N_("no such partition"));
	}

      if (partition->start + partition->len > 0xffffffff)
	{
	  grub_device_close (dev);
	  return grub_error (GRUB_ERR_OUT_OF_RANGE,
			     "only partitions residing in the first 2TB "
			     "can be present in hybrid MBR");
	}


      if (first_sector > partition->start)
	first_sector = partition->start;

      if (separator && *(separator + 1))
	type = grub_strtoul (separator + 1, 0, 0);
      else
	{
	  grub_fs_t fs = 0;
	  dev->disk->partition = partition;
	  fs = grub_fs_probe (dev);

	  /* Unknown filesystem isn't fatal. */
	  if (grub_errno == GRUB_ERR_UNKNOWN_FS)
	    {
	      fs = 0;
	      grub_errno = GRUB_ERR_NONE;
	    }

	  if (fs && grub_strcmp (fs->name, "ntfs") == 0)
	    type = GRUB_PC_PARTITION_TYPE_NTFS;
	  else if (fs && grub_strcmp (fs->name, "fat") == 0)
	    /* FIXME: detect FAT16. */
	    type = GRUB_PC_PARTITION_TYPE_FAT32_LBA;
	  else if (fs && (grub_strcmp (fs->name, "hfsplus") == 0
			  || grub_strcmp (fs->name, "hfs") == 0))
	    type = GRUB_PC_PARTITION_TYPE_HFS;
	  else
	    /* FIXME: detect more types. */
	    type = GRUB_PC_PARTITION_TYPE_EXT2FS;

	  dev->disk->partition = 0;
	}

      mbr.entries[i].flag = (csep == '+') ? 0x80 : 0;
      if (csep == '+')
	{
	  numactive++;
	  if (numactive == 2)
	    {
	      grub_device_close (dev);
	      return grub_error (GRUB_ERR_BAD_ARGUMENT,
				 "only one partition can be active");
	    }
	}
      mbr.entries[i].type = type;
      mbr.entries[i].start = grub_cpu_to_le32 (partition->start);
      lba_to_chs (partition->start,
		  &(mbr.entries[i].start_sector),
		  &(mbr.entries[i].start_cylinder),
		  &(mbr.entries[i].start_head));
      lba_to_chs (partition->start + partition->len - 1,
		  &(mbr.entries[i].end_sector),
		  &(mbr.entries[i].end_cylinder),
		  &(mbr.entries[i].end_head));
      mbr.entries[i].length = grub_cpu_to_le32 (partition->len);
      grub_free (partition);
    }
  for (; i < 4; i++)
    grub_memset (&(mbr.entries[i]), 0, sizeof (mbr.entries[i]));

  /* The protective partition. */
  if (first_sector > 0xffffffff)
    first_sector = 0xffffffff;
  else
    first_sector--;
  mbr.entries[0].flag = 0;
  mbr.entries[0].type = GRUB_PC_PARTITION_TYPE_GPT_DISK;
  mbr.entries[0].start = grub_cpu_to_le32_compile_time (1);
  lba_to_chs (1,
	      &(mbr.entries[0].start_sector),
	      &(mbr.entries[0].start_cylinder),
	      &(mbr.entries[0].start_head));
  lba_to_chs (first_sector,
	      &(mbr.entries[0].end_sector),
	      &(mbr.entries[0].end_cylinder),
	      &(mbr.entries[0].end_head));
  mbr.entries[0].length = grub_cpu_to_le32 (first_sector);

  mbr.signature = grub_cpu_to_le16_compile_time (GRUB_PC_PARTITION_SIGNATURE);

  if (grub_disk_write (dev->disk, 0, 0, sizeof (mbr), &mbr))
    {
      grub_device_close (dev);
      return grub_errno;
    }

  grub_device_close (dev);

  grub_printf_ (N_("New MBR is written to `%s'\n"), args[0]);

  return GRUB_ERR_NONE;
}


static grub_command_t cmd;

GRUB_MOD_INIT(gptsync)
{
  (void) mod;			/* To stop warning. */
  cmd = grub_register_command ("gptsync", grub_cmd_gptsync,
			       N_("DEVICE [PARTITION[+/-[TYPE]]] ..."),
			       /* TRANSLATORS: MBR type is one-byte partition
				  type id.  */
			       N_("Fill hybrid MBR of GPT drive DEVICE. "
			       "Specified partitions will be a part "
			       "of hybrid MBR. Up to 3 partitions are "
			       "allowed. TYPE is an MBR type. "
			       "+ means that partition is active. "
			       "Only one partition can be active."));
}

GRUB_MOD_FINI(gptsync)
{
  grub_unregister_command (cmd);
}
