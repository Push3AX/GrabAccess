/* dmraid_nvidia.c - module to handle Nvidia fakeraid.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/diskfilter.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define NV_SIGNATURES		4

#define NV_IDLE			0
#define NV_SCDB_INIT_RAID	2
#define NV_SCDB_REBUILD_RAID	3
#define NV_SCDB_UPGRADE_RAID	4
#define NV_SCDB_SYNC_RAID	5

#define NV_LEVEL_UNKNOWN	0x00
#define NV_LEVEL_JBOD		0xFF
#define NV_LEVEL_0		0x80
#define NV_LEVEL_1		0x81
#define NV_LEVEL_3		0x83
#define NV_LEVEL_5		0x85
#define NV_LEVEL_10		0x8a
#define NV_LEVEL_1_0		0x8180

#define NV_ARRAY_FLAG_BOOT		1 /* BIOS use only.  */
#define NV_ARRAY_FLAG_ERROR		2 /* Degraded or offline.  */
#define NV_ARRAY_FLAG_PARITY_VALID	4 /* RAID-3/5 parity valid.  */

struct grub_nv_array
{
  grub_uint32_t version;
  grub_uint32_t signature[NV_SIGNATURES];
  grub_uint8_t raid_job_code;
  grub_uint8_t stripe_width;
  grub_uint8_t total_volumes;
  grub_uint8_t original_width;
  grub_uint32_t raid_level;
  grub_uint32_t stripe_block_size;
  grub_uint32_t stripe_block_size_bytes;
  grub_uint32_t stripe_block_size_log2;
  grub_uint32_t stripe_mask;
  grub_uint32_t stripe_size;
  grub_uint32_t stripe_size_bytes;
  grub_uint32_t raid_job_mask;
  grub_uint32_t original_capacity;
  grub_uint32_t flags;
};

#define NV_ID_LEN		8
#define NV_ID_STRING		"NVIDIA"
#define NV_VERSION		100

#define	NV_PRODID_LEN		16
#define	NV_PRODREV_LEN		4

struct grub_nv_super
{
  char vendor[NV_ID_LEN];	/* 0x00 - 0x07 ID string.  */
  grub_uint32_t size;		/* 0x08 - 0x0B Size of metadata in dwords.  */
  grub_uint32_t chksum;		/* 0x0C - 0x0F Checksum of this struct.  */
  grub_uint16_t version;	/* 0x10 - 0x11 NV version.  */
  grub_uint8_t unit_number;	/* 0x12 Disk index in array.  */
  grub_uint8_t reserved;	/* 0x13.  */
  grub_uint32_t capacity;	/* 0x14 - 0x17 Array capacity in sectors.  */
  grub_uint32_t sector_size;	/* 0x18 - 0x1B Sector size.  */
  char prodid[NV_PRODID_LEN];	/* 0x1C - 0x2B Array product ID.  */
  char prodrev[NV_PRODREV_LEN];	/* 0x2C - 0x2F Array product revision */
  grub_uint32_t unit_flags;	/* 0x30 - 0x33 Flags for this disk */
  struct grub_nv_array array;	/* Array information */
} GRUB_PACKED;

static struct grub_diskfilter_vg *
grub_dmraid_nv_detect (grub_disk_t disk,
			struct grub_diskfilter_pv_id *id,
                        grub_disk_addr_t *start_sector)
{
  grub_disk_addr_t sector;
  struct grub_nv_super sb;
  int level;
  grub_uint64_t disk_size;
  grub_uint32_t capacity;
  grub_uint8_t total_volumes;
  char *uuid;

  if (disk->partition)
    /* Skip partition.  */
    return NULL;

  sector = grub_disk_native_sectors (disk);
  if (sector == GRUB_DISK_SIZE_UNKNOWN)
    /* Not raid.  */
    return NULL;
  sector -= 2;
  if (grub_disk_read (disk, sector, 0, sizeof (sb), &sb))
    return NULL;

  if (grub_memcmp (sb.vendor, NV_ID_STRING, 6))
    /* Not raid.  */
    return NULL;

  if (sb.version != NV_VERSION)
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  "unknown version: %d.%d", sb.version >> 8, sb.version & 0xFF);
      return NULL;
    }

  capacity = grub_le_to_cpu32 (sb.capacity);
  total_volumes = sb.array.total_volumes;

  switch (sb.array.raid_level)
    {
    case NV_LEVEL_0:
      level = 0;
      if (total_volumes == 0)
	/* Not RAID.  */
	return NULL;
      disk_size = capacity / total_volumes;
      break;

    case NV_LEVEL_1:
      level = 1;
      disk_size = sb.capacity;
      break;

    case NV_LEVEL_5:
      level = 5;
      if (total_volumes == 0 || total_volumes == 1)
	/* Not RAID.  */
	return NULL;
      disk_size = capacity / (total_volumes - 1);
      break;

    default:
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  "unsupported RAID level: %d", sb.array.raid_level);
      return NULL;
    }

  uuid = grub_malloc (sizeof (sb.array.signature));
  if (! uuid)
    return NULL;

  grub_memcpy (uuid, (char *) &sb.array.signature,
               sizeof (sb.array.signature));

  id->uuidlen = 0;
  id->id = sb.unit_number;

  *start_sector = 0;

  return grub_diskfilter_make_raid (sizeof (sb.array.signature),
				    uuid, sb.array.total_volumes,
				    "nv", disk_size,
				    sb.array.stripe_block_size,
				    GRUB_RAID_LAYOUT_LEFT_ASYMMETRIC,
				    level);
}

static struct grub_diskfilter grub_dmraid_nv_dev =
{
  .name = "dmraid_nv",
  .detect = grub_dmraid_nv_detect,
  .next = 0
};

GRUB_MOD_INIT(dm_nv)
{
  grub_diskfilter_register_front (&grub_dmraid_nv_dev);
}

GRUB_MOD_FINI(dm_nv)
{
  grub_diskfilter_unregister (&grub_dmraid_nv_dev);
}
