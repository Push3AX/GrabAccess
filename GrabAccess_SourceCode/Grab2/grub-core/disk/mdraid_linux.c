/* mdraid_linux.c - module to handle Linux Software RAID.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010  Free Software Foundation, Inc.
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

/* Linux RAID on disk structures and constants,
   copied from include/linux/raid/md_p.h.  */

GRUB_MOD_LICENSE ("GPLv3+");

#ifdef MODE_BIGENDIAN
#define grub_md_to_cpu64 grub_be_to_cpu64
#define grub_md_to_cpu32 grub_be_to_cpu32
#define grub_md_to_cpu16 grub_be_to_cpu16
#define grub_cpu_to_md64_compile_time grub_cpu_to_be64_compile_time
#define grub_cpu_to_md32_compile_time grub_cpu_to_be32_compile_time
#define grub_cpu_to_md16_compile_time grub_cpu_to_be16_compile_time
#else
#define grub_md_to_cpu64 grub_le_to_cpu64
#define grub_md_to_cpu32 grub_le_to_cpu32
#define grub_md_to_cpu16 grub_le_to_cpu16
#define grub_cpu_to_md64_compile_time grub_cpu_to_le64_compile_time
#define grub_cpu_to_md32_compile_time grub_cpu_to_le32_compile_time
#define grub_cpu_to_md16_compile_time grub_cpu_to_le16_compile_time
#endif

#define RESERVED_BYTES			(64 * 1024)
#define RESERVED_SECTORS		(RESERVED_BYTES / 512)

#define NEW_SIZE_SECTORS(x)		((x & ~(RESERVED_SECTORS - 1)) \
					- RESERVED_SECTORS)

#define SB_BYTES			4096
#define SB_WORDS			(SB_BYTES / 4)
#define SB_SECTORS			(SB_BYTES / 512)

/*
 * The following are counted in 32-bit words
 */
#define	SB_GENERIC_OFFSET		0

#define SB_PERSONALITY_OFFSET		64
#define SB_DISKS_OFFSET			128
#define SB_DESCRIPTOR_OFFSET		992

#define SB_GENERIC_CONSTANT_WORDS	32
#define SB_GENERIC_STATE_WORDS		32
#define SB_GENERIC_WORDS		(SB_GENERIC_CONSTANT_WORDS + \
                                         SB_GENERIC_STATE_WORDS)

#define SB_PERSONALITY_WORDS		64
#define SB_DESCRIPTOR_WORDS		32
#define SB_DISKS			27
#define SB_DISKS_WORDS			(SB_DISKS * SB_DESCRIPTOR_WORDS)

#define SB_RESERVED_WORDS		(1024 \
                                         - SB_GENERIC_WORDS \
                                         - SB_PERSONALITY_WORDS \
                                         - SB_DISKS_WORDS \
                                         - SB_DESCRIPTOR_WORDS)

#define SB_EQUAL_WORDS			(SB_GENERIC_WORDS \
                                         + SB_PERSONALITY_WORDS \
                                         + SB_DISKS_WORDS)

/*
 * Device "operational" state bits
 */
#define DISK_FAULTY			0
#define DISK_ACTIVE			1
#define DISK_SYNC			2
#define DISK_REMOVED			3

#define	DISK_WRITEMOSTLY		9

#define SB_MAGIC			0xa92b4efc

/*
 * Superblock state bits
 */
#define SB_CLEAN			0
#define SB_ERRORS			1

#define	SB_BITMAP_PRESENT		8

struct grub_raid_disk_09
{
  grub_uint32_t number;		/* Device number in the entire set.  */
  grub_uint32_t major;		/* Device major number.  */
  grub_uint32_t minor;		/* Device minor number.  */
  grub_uint32_t raid_disk;	/* The role of the device in the raid set.  */
  grub_uint32_t state;		/* Operational state.  */
  grub_uint32_t reserved[SB_DESCRIPTOR_WORDS - 5];
};

struct grub_raid_super_09
{
  /*
   * Constant generic information
   */
  grub_uint32_t md_magic;	/* MD identifier.  */
  grub_uint32_t major_version;	/* Major version.  */
  grub_uint32_t minor_version;	/* Minor version.  */
  grub_uint32_t patch_version;	/* Patchlevel version.  */
  grub_uint32_t gvalid_words;	/* Number of used words in this section.  */
  grub_uint32_t set_uuid0;	/* Raid set identifier.  */
  grub_uint32_t ctime;		/* Creation time.  */
  grub_uint32_t level;		/* Raid personality.  */
  grub_uint32_t size;		/* Apparent size of each individual disk.  */
  grub_uint32_t nr_disks;	/* Total disks in the raid set.  */
  grub_uint32_t raid_disks;	/* Disks in a fully functional raid set.  */
  grub_uint32_t md_minor;	/* Preferred MD minor device number.  */
  grub_uint32_t not_persistent;	/* Does it have a persistent superblock.  */
  grub_uint32_t set_uuid1;	/* Raid set identifier #2.  */
  grub_uint32_t set_uuid2;	/* Raid set identifier #3.  */
  grub_uint32_t set_uuid3;	/* Raid set identifier #4.  */
  grub_uint32_t gstate_creserved[SB_GENERIC_CONSTANT_WORDS - 16];

  /*
   * Generic state information
   */
  grub_uint32_t utime;		/* Superblock update time.  */
  grub_uint32_t state;		/* State bits (clean, ...).  */
  grub_uint32_t active_disks;	/* Number of currently active disks.  */
  grub_uint32_t working_disks;	/* Number of working disks.  */
  grub_uint32_t failed_disks;	/* Number of failed disks.  */
  grub_uint32_t spare_disks;	/* Number of spare disks.  */
  grub_uint32_t sb_csum;	/* Checksum of the whole superblock.  */
  grub_uint64_t events;		/* Superblock update count.  */
  grub_uint64_t cp_events;	/* Checkpoint update count.  */
  grub_uint32_t recovery_cp;	/* Recovery checkpoint sector count.  */
  grub_uint32_t gstate_sreserved[SB_GENERIC_STATE_WORDS - 12];

  /*
   * Personality information
   */
  grub_uint32_t layout;		/* The array's physical layout.  */
  grub_uint32_t chunk_size;	/* Chunk size in bytes.  */
  grub_uint32_t root_pv;	/* LV root PV.  */
  grub_uint32_t root_block;	/* LV root block.  */
  grub_uint32_t pstate_reserved[SB_PERSONALITY_WORDS - 4];

  /*
   * Disks information
   */
  struct grub_raid_disk_09 disks[SB_DISKS];

  /*
   * Reserved
   */
  grub_uint32_t reserved[SB_RESERVED_WORDS];

  /*
   * Active descriptor
   */
  struct grub_raid_disk_09 this_disk;
} GRUB_PACKED;

static struct grub_diskfilter_vg *
grub_mdraid_detect (grub_disk_t disk,
		    struct grub_diskfilter_pv_id *id,
		    grub_disk_addr_t *start_sector)
{
  grub_disk_addr_t sector;
  grub_uint64_t size;
  struct grub_raid_super_09 *sb = NULL;
  grub_uint32_t *uuid;
  grub_uint32_t level;
  struct grub_diskfilter_vg *ret;

  /* The sector where the mdraid 0.90 superblock is stored, if available.  */
  size = grub_disk_native_sectors (disk);
  if (size == GRUB_DISK_SIZE_UNKNOWN)
    /* not 0.9x raid.  */
    return NULL;
  sector = NEW_SIZE_SECTORS (size);

  sb = grub_malloc (sizeof (*sb));
  if (!sb)
    return NULL;

  if (grub_disk_read (disk, sector, 0, SB_BYTES, sb))
    goto fail;

  /* Look whether there is a mdraid 0.90 superblock.  */
  if (sb->md_magic != grub_cpu_to_md32_compile_time (SB_MAGIC))
    /* not 0.9x raid.  */
    goto fail;

  if (sb->major_version != grub_cpu_to_md32_compile_time (0)
      || sb->minor_version != grub_cpu_to_md32_compile_time (90))
    /* Unsupported version.  */
    goto fail;

  /* No need for explicit check that sb->size is 0 (unspecified) since
     0 >= non-0 is false.  */
  if (((grub_disk_addr_t) grub_md_to_cpu32 (sb->size)) * 2 >= size)
    goto fail;

  /* FIXME: Check the checksum.  */

  level = grub_md_to_cpu32 (sb->level);
  /* Multipath.  */
  if ((int) level == -4)
    level = 1;

  if (level != 0 && level != 1 && level != 4 &&
      level != 5 && level != 6 && level != 10)
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  "unsupported RAID level: %d", level);
      goto fail;
    }
  if (grub_md_to_cpu32 (sb->this_disk.number) == 0xffff
      || grub_md_to_cpu32 (sb->this_disk.number) == 0xfffe)
    /* Spares aren't implemented.  */
    goto fail;

  uuid = grub_malloc (16);
  if (!uuid)
    goto fail;

  uuid[0] = grub_swap_bytes32 (sb->set_uuid0);
  uuid[1] = grub_swap_bytes32 (sb->set_uuid1);
  uuid[2] = grub_swap_bytes32 (sb->set_uuid2);
  uuid[3] = grub_swap_bytes32 (sb->set_uuid3);

  *start_sector = 0;

  id->uuidlen = 0;
  id->id = grub_md_to_cpu32 (sb->this_disk.number);

  char buf[32];
  grub_snprintf (buf, sizeof (buf), "md%d", grub_md_to_cpu32 (sb->md_minor));
  ret = grub_diskfilter_make_raid (16, (char *) uuid,
				   grub_md_to_cpu32 (sb->raid_disks), buf,
				   (sb->size) ? ((grub_disk_addr_t)
						 grub_md_to_cpu32 (sb->size)) * 2
				   : sector,
				   grub_md_to_cpu32 (sb->chunk_size) >> 9,
				   grub_md_to_cpu32 (sb->layout),
				   level);
  grub_free (sb);
  return ret;

 fail:
  grub_free (sb);
  return NULL;
}

static struct grub_diskfilter grub_mdraid_dev = {
#ifdef MODE_BIGENDIAN
  .name = "mdraid09_be",
#else
  .name = "mdraid09",
#endif
  .detect = grub_mdraid_detect,
  .next = 0
};

#ifdef MODE_BIGENDIAN
GRUB_MOD_INIT (mdraid09_be)
#else
GRUB_MOD_INIT (mdraid09)
#endif
{
  grub_diskfilter_register_front (&grub_mdraid_dev);
}

#ifdef MODE_BIGENDIAN
GRUB_MOD_FINI (mdraid09_be)
#else
GRUB_MOD_FINI (mdraid09)
#endif
{
  grub_diskfilter_unregister (&grub_mdraid_dev);
}
