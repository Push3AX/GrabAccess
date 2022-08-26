/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2004,2007  Free Software Foundation, Inc.
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

#ifndef GRUB_PC_PARTITION_HEADER
#define GRUB_PC_PARTITION_HEADER	1

#include <grub/symbol.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/disk.h>
#include <grub/partition.h>

/* The signature.  */
#define GRUB_PC_PARTITION_SIGNATURE		0xaa55

/* This is not a flag actually, but used as if it were a flag.  */
#define GRUB_PC_PARTITION_TYPE_HIDDEN_FLAG	0x10

/* DOS partition types.  */
#define GRUB_PC_PARTITION_TYPE_NONE		0
#define GRUB_PC_PARTITION_TYPE_FAT12		1
#define GRUB_PC_PARTITION_TYPE_FAT16_LT32M	4
#define GRUB_PC_PARTITION_TYPE_EXTENDED		5
#define GRUB_PC_PARTITION_TYPE_FAT16_GT32M	6
#define GRUB_PC_PARTITION_TYPE_NTFS	        7
#define GRUB_PC_PARTITION_TYPE_FAT32		0xb
#define GRUB_PC_PARTITION_TYPE_FAT32_LBA	0xc
#define GRUB_PC_PARTITION_TYPE_FAT16_LBA	0xe
#define GRUB_PC_PARTITION_TYPE_WIN95_EXTENDED	0xf
#define GRUB_PC_PARTITION_TYPE_PLAN9            0x39
#define GRUB_PC_PARTITION_TYPE_LDM		0x42
#define GRUB_PC_PARTITION_TYPE_EZD		0x55
#define GRUB_PC_PARTITION_TYPE_MINIX		0x80
#define GRUB_PC_PARTITION_TYPE_LINUX_MINIX	0x81
#define GRUB_PC_PARTITION_TYPE_LINUX_SWAP	0x82
#define GRUB_PC_PARTITION_TYPE_EXT2FS		0x83
#define GRUB_PC_PARTITION_TYPE_LINUX_EXTENDED	0x85
#define GRUB_PC_PARTITION_TYPE_VSTAFS		0x9e
#define GRUB_PC_PARTITION_TYPE_FREEBSD		0xa5
#define GRUB_PC_PARTITION_TYPE_OPENBSD		0xa6
#define GRUB_PC_PARTITION_TYPE_NETBSD		0xa9
#define GRUB_PC_PARTITION_TYPE_HFS		0xaf
#define GRUB_PC_PARTITION_TYPE_GPT_DISK		0xee
#define GRUB_PC_PARTITION_TYPE_LINUX_RAID	0xfd

#define GRUB_PC_MAX_PARTITIONS  4

/* The partition entry.  */
struct grub_msdos_partition_entry
{
  /* If active, 0x80, otherwise, 0x00.  */
  grub_uint8_t flag;

  /* The head of the start.  */
  grub_uint8_t start_head;

  /* (S | ((C >> 2) & 0xC0)) where S is the sector of the start and C
     is the cylinder of the start. Note that S is counted from one.  */
  grub_uint8_t start_sector;

  /* (C & 0xFF) where C is the cylinder of the start.  */
  grub_uint8_t start_cylinder;

  /* The partition type.  */
  grub_uint8_t type;

  /* The end versions of start_head, start_sector and start_cylinder,
     respectively.  */
  grub_uint8_t end_head;
  grub_uint8_t end_sector;
  grub_uint8_t end_cylinder;

  /* The start sector. Note that this is counted from zero.  */
  grub_uint32_t start;

  /* The length in sector units.  */
  grub_uint32_t length;
} GRUB_PACKED;

/* The structure of MBR.  */
struct grub_msdos_partition_mbr
{
  char dummy1[11];/* normally there is a short JMP instuction(opcode is 0xEB) */
  grub_uint16_t bytes_per_sector;/* seems always to be 512, so we just use 512 */
  grub_uint8_t sectors_per_cluster;/* non-zero, the power of 2, i.e., 2^n */
  grub_uint16_t reserved_sectors;/* FAT=non-zero, NTFS=0? */
  grub_uint8_t number_of_fats;/* NTFS=0; FAT=1 or 2  */
  grub_uint16_t root_dir_entries;/* FAT32=0, NTFS=0, FAT12/16=non-zero */
  grub_uint16_t total_sectors_short;/* FAT32=0, NTFS=0, FAT12/16=any */
  grub_uint8_t media_descriptor;/* range from 0xf0 to 0xff */
  grub_uint16_t sectors_per_fat;/* FAT32=0, NTFS=0, FAT12/16=non-zero */
  grub_uint16_t sectors_per_track;/* range from 1 to 63 */
  grub_uint16_t total_heads;/* range from 1 to 256 */
  grub_uint32_t hidden_sectors;/* any value */
  grub_uint32_t total_sectors_long;/* FAT32=non-zero, NTFS=0, FAT12/16=any */
  grub_uint32_t sectors_per_fat32;/* FAT32=non-zero, NTFS=any, FAT12/16=any */
  grub_uint64_t total_sectors_long_long;/* NTFS=non-zero, FAT12/16/32=any */
  char dummy2[392];
  grub_uint8_t unique_signature[4];
  grub_uint8_t unknown[2];

  /* Four partition entries.  */
  struct grub_msdos_partition_entry entries[4];

  /* The signature 0xaa55.  */
  grub_uint16_t signature;
} GRUB_PACKED;

static inline int
grub_msdos_partition_is_empty (int type)
{
  return (type == GRUB_PC_PARTITION_TYPE_NONE);
}

static inline int
grub_msdos_partition_is_extended (int type)
{
  return (type == GRUB_PC_PARTITION_TYPE_EXTENDED
	  || type == GRUB_PC_PARTITION_TYPE_WIN95_EXTENDED
	  || type == GRUB_PC_PARTITION_TYPE_LINUX_EXTENDED);
}

grub_err_t
grub_partition_msdos_iterate (grub_disk_t disk,
			      grub_partition_iterate_hook_t hook,
			      void *hook_data);

/* Convert a LBA address to a CHS address in the INT 13 format.  */
/* Taken from grub1. */
/* XXX: use hardcoded geometry of C = 1024, H = 255, S = 63.
   Is it a problem?
*/
static inline void
lba_to_chs (grub_uint32_t lba, grub_uint8_t *cl, grub_uint8_t *ch,
            grub_uint8_t *dh)
{
  grub_uint32_t cylinder, head, sector;
  grub_uint32_t sectors = 63, heads = 255, cylinders = 1024;

  sector = lba % sectors + 1;
  head = (lba / sectors) % heads;
  cylinder = lba / (sectors * heads);

  if (cylinder >= cylinders)
  {
    *cl = *ch = 0xff;
    *dh = 0xfe;
    return;
  }

  *cl = sector | ((cylinder & 0x300) >> 2);
  *ch = cylinder & 0xFF;
  *dh = head;
}

#endif /* ! GRUB_PC_PARTITION_HEADER */
