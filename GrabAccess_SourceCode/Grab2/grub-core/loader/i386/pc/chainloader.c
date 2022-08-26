/* chainloader.c - boot another boot loader */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2007,2009,2010  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/machine/kernel.h>
#include <grub/machine/chainloader.h>
#include <grub/machine/biosdisk.h>
#include <grub/machine/memory.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/partition.h>
#include <grub/memory.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/msdos_partition.h>
#include <grub/machine/biosnum.h>
#include <grub/cpu/floppy.h>
#include <grub/i18n.h>
#include <grub/video.h>
#include <grub/mm.h>
#include <grub/fat.h>
#include <grub/ntfs.h>
#include <grub/i386/relocator.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;
static int boot_drive;
static grub_addr_t boot_part_addr;
static struct grub_relocator *rel;
static grub_uint16_t mem_addr;

typedef enum
  {
    GRUB_CHAINLOADER_FORCE = 0x1,
    GRUB_CHAINLOADER_BPB = 0x2,
  } grub_chainloader_flags_t;

static grub_err_t
grub_chainloader_boot (void)
{
  struct grub_relocator16_state state = { 
    .edx = boot_drive,
    .esi = boot_part_addr,
    .ds = 0,
    .es = 0,
    .fs = 0,
    .gs = 0,
    .ss = 0,
    .cs = 0,
    .sp = mem_addr,
    .ip = mem_addr,
    .a20 = 0
  };
  grub_video_set_mode ("text", 0, 0);

  return grub_relocator16_boot (rel, state);
}

static grub_err_t
grub_chainloader_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

void
grub_chainloader_patch_bpb (void *bs, grub_device_t dev, grub_uint8_t dl)
{
  grub_uint32_t part_start = 0, heads = 0, sectors = 0;
  if (dev && dev->disk)
    {
      part_start = grub_partition_get_start (dev->disk->partition);
      if (dev->disk->data)
        {
          heads = ((struct grub_biosdisk_data *)(dev->disk->data))->heads;
          sectors = ((struct grub_biosdisk_data *)(dev->disk->data))->sectors;
        }
    }
  if (grub_memcmp ((char *) &((struct grub_ntfs_bpb *) bs)->oem_name,
		   "NTFS", 4) == 0)
    {
      struct grub_ntfs_bpb *bpb = (struct grub_ntfs_bpb *) bs;
      bpb->num_hidden_sectors = grub_cpu_to_le32 (part_start);
      bpb->bios_drive = dl;
      return;
    }

  do
    {
      struct grub_fat_bpb *bpb = (struct grub_fat_bpb *) bs;
      if (grub_strncmp((const char *) bpb->version_specific.fat12_or_fat16.fstype, "FAT12", 5)
	  && grub_strncmp((const char *) bpb->version_specific.fat12_or_fat16.fstype, "FAT16", 5)
	  && grub_strncmp((const char *) bpb->version_specific.fat32.fstype, "FAT32", 5))
	break;

      if (grub_le_to_cpu16 (bpb->bytes_per_sector) < 512
	  || (grub_le_to_cpu16 (bpb->bytes_per_sector)
	      & (grub_le_to_cpu16 (bpb->bytes_per_sector) - 1)))
	break;
	  
      if (bpb->sectors_per_cluster == 0
	  || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)))
	break;

      if (bpb->num_reserved_sectors == 0)
	break;
      if (bpb->num_total_sectors_16 == 0 && bpb->num_total_sectors_32 == 0)
	break;

      if (bpb->num_fats == 0)
	break;

      if (bpb->sectors_per_fat_16)
	{
	  bpb->num_hidden_sectors = grub_cpu_to_le32 (part_start);
	  bpb->version_specific.fat12_or_fat16.num_ph_drive = dl;
          if (sectors)
            bpb->sectors_per_track = grub_cpu_to_le16 (sectors);
          if (heads)
            bpb->num_heads = grub_cpu_to_le16 (heads);
	  return;
	}
      if (bpb->version_specific.fat32.sectors_per_fat_32)
	{
	  bpb->num_hidden_sectors = grub_cpu_to_le32 (part_start);
	  bpb->version_specific.fat32.num_ph_drive = dl;
          if (sectors)
            bpb->sectors_per_track = grub_cpu_to_le16 (sectors);
          if (heads)
            bpb->num_heads = grub_cpu_to_le16 (heads);
	  return;
	}
      break;
    }
  while (0);
}

static void
grub_chainloader_cmd (const char *filename, grub_chainloader_flags_t flags, grub_uint16_t addr)
{
  grub_file_t file = 0;
  grub_uint16_t signature;
  grub_device_t dev;
  int drive = -1;
  grub_addr_t part_addr = 0;
  grub_uint8_t *bs, *ptable;

  rel = grub_relocator_new ();
  if (!rel)
    goto fail;

  grub_dl_ref (my_mod);
  
  mem_addr = addr;

  file = grub_file_open (filename, GRUB_FILE_TYPE_PCCHAINLOADER
			 | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (! file)
    goto fail;

  {
    grub_relocator_chunk_t ch;
    grub_err_t err;

    err = grub_relocator_alloc_chunk_addr (rel, &ch, 0x7C00,
					   GRUB_DISK_SECTOR_SIZE);
    if (err)
      goto fail;
    bs = get_virtual_current_address (ch);
    err = grub_relocator_alloc_chunk_addr (rel, &ch,
					   GRUB_MEMORY_MACHINE_PART_TABLE_ADDR,
					   64);
    if (err)
      goto fail;
    ptable = get_virtual_current_address (ch);
  }

  /* Read the first block.  */
  if (grub_file_read (file, bs, GRUB_DISK_SECTOR_SIZE)
      != GRUB_DISK_SECTOR_SIZE)
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);

      goto fail;
    }

  /* Check the signature.  */
  signature = *((grub_uint16_t *) (bs + GRUB_DISK_SECTOR_SIZE - 2));
  if (signature != grub_le_to_cpu16 (0xaa55)
      && ! (flags & GRUB_CHAINLOADER_FORCE))
    {
      grub_error (GRUB_ERR_BAD_OS, "invalid signature");
      goto fail;
    }

  grub_file_close (file);

  /* Obtain the partition table from the root device.  */
  drive = grub_get_root_biosnumber ();
  dev = grub_device_open (0);
  if (dev && dev->disk && dev->disk->partition)
    {
      grub_disk_t disk = dev->disk;

      if (disk)
	{
	  grub_partition_t p = disk->partition;

	  if (p && grub_strcmp (p->partmap->name, "msdos") == 0)
	    {
	      disk->partition = p->parent;
	      grub_disk_read (disk, p->offset, 446, 64, ptable);
	      part_addr = (GRUB_MEMORY_MACHINE_PART_TABLE_ADDR
			   + (p->index << 4));
	      disk->partition = p;
	    }
	}
    }

  if (flags & GRUB_CHAINLOADER_BPB)
    grub_chainloader_patch_bpb ((void *) 0x7C00, dev, drive);

  if (dev)
    grub_device_close (dev);
 
  /* Ignore errors. Perhaps it's not fatal.  */
  grub_errno = GRUB_ERR_NONE;

  boot_drive = drive;
  boot_part_addr = part_addr;

  grub_loader_set (grub_chainloader_boot, grub_chainloader_unload, 1);
  return;

 fail:

  if (file)
    grub_file_close (file);

  grub_dl_unref (my_mod);
}

static grub_err_t
grub_cmd_chainloader (grub_command_t cmd __attribute__ ((unused)),
		      int argc, char *argv[])
{
  grub_chainloader_flags_t flags = 0;
  grub_uint16_t addr;
  const char *endptr;

  while (argc > 0)
    {
      if (grub_strcmp (argv[0], "--force") == 0)
	{
	  flags |= GRUB_CHAINLOADER_FORCE;
	  argc--;
	  argv++;
	  continue;
	}
      if (grub_strcmp (argv[0], "--bpb") == 0)
	{
	  flags |= GRUB_CHAINLOADER_BPB;
	  argc--;
	  argv++;
	  continue;
	}
      break;
    }

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (argc == 2)
    addr = grub_strtoul(argv[1], &endptr, 0);
  else
    addr = GRUB_MEMORY_MACHINE_BOOT_LOADER_ADDR;

  grub_chainloader_cmd (argv[0], flags, addr);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(chainloader)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  cmd = grub_register_command ("chainloader", grub_cmd_chainloader,
			       N_("[--force|--bpb] FILE [ADDR]"),
			       N_("Load another boot loader."));
  my_mod = mod;
}

GRUB_MOD_FINI(chainloader)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_unregister_command (cmd);
}
