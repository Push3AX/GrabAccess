/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2006,2007,2008  Free Software Foundation, Inc.
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

#ifndef GRUB_GPT_PARTITION_HEADER
#define GRUB_GPT_PARTITION_HEADER	1

#include <grub/types.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>

typedef grub_packed_guid_t grub_gpt_part_guid_t;
typedef grub_packed_guid_t grub_gpt_guid_t;

/* Format the raw little-endian GUID as a newly allocated string.  */
char * grub_gpt_guid_to_str (grub_gpt_guid_t *guid);


#define GRUB_GPT_GUID_INIT(a, b, c, d1, d2, d3, d4, d5, d6, d7, d8)  \
  {					\
    grub_cpu_to_le32_compile_time (a),	\
    grub_cpu_to_le16_compile_time (b),	\
    grub_cpu_to_le16_compile_time (c),	\
    { d1, d2, d3, d4, d5, d6, d7, d8 }	\
  }

#define GRUB_GPT_PARTITION_TYPE_EMPTY \
  GRUB_GPT_GUID_INIT (0x0, 0x0, 0x0,  \
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0)

#define GRUB_GPT_PARTITION_TYPE_EFI_SYSTEM \
  GRUB_GPT_GUID_INIT (0xc12a7328, 0xf81f, 0x11d2, \
      0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b)

#define GRUB_GPT_PARTITION_TYPE_BIOS_BOOT \
  GRUB_GPT_GUID_INIT (0x21686148, 0x6449, 0x6e6f, \
      0x74, 0x4e, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49)

#define GRUB_GPT_PARTITION_TYPE_LDM \
  GRUB_GPT_GUID_INIT (0x5808c8aa, 0x7e8f, 0x42e0, \
      0x85, 0xd2, 0xe1, 0xe9, 0x04, 0x34, 0xcf, 0xb3)

#define GRUB_GPT_PARTITION_TYPE_USR_X86_64 \
  GRUB_GPT_GUID_INIT (0x5dfbf5f4, 0x2848, 0x4bac, \
      0xaa, 0x5e, 0x0d, 0x9a, 0x20, 0xb7, 0x45, 0xa6)

#define GRUB_GPT_HEADER_MAGIC \
  { 0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54 }

#define GRUB_GPT_HEADER_VERSION	\
  grub_cpu_to_le32_compile_time (0x00010000U)

struct grub_gpt_header
{
  grub_uint8_t magic[8];
  grub_uint32_t version;
  grub_uint32_t headersize;
  grub_uint32_t crc32;
  grub_uint32_t unused1;
  grub_uint64_t header_lba;
  grub_uint64_t alternate_lba;
  grub_uint64_t start;
  grub_uint64_t end;
  grub_gpt_guid_t guid;
  grub_uint64_t partitions;
  grub_uint32_t maxpart;
  grub_uint32_t partentry_size;
  grub_uint32_t partentry_crc32;
} GRUB_PACKED;

struct grub_gpt_partentry
{
  grub_gpt_part_guid_t type;
  grub_gpt_part_guid_t guid;
  grub_uint64_t start;
  grub_uint64_t end;
  grub_uint64_t attrib;
  grub_uint16_t name[36];
} GRUB_PACKED;

enum grub_gpt_part_attr_offset
{
  /* Standard partition attribute bits defined by UEFI.  */
  GRUB_GPT_PART_ATTR_OFFSET_REQUIRED			= 0,
  GRUB_GPT_PART_ATTR_OFFSET_NO_BLOCK_IO_PROTOCOL	= 1,
  GRUB_GPT_PART_ATTR_OFFSET_LEGACY_BIOS_BOOTABLE	= 2,

  /* De facto standard attribute bits defined by Microsoft and reused by
   * http://www.freedesktop.org/wiki/Specifications/DiscoverablePartitionsSpec */
  GRUB_GPT_PART_ATTR_OFFSET_READ_ONLY			= 60,
  GRUB_GPT_PART_ATTR_OFFSET_NO_AUTO			= 63,

  /* Partition attributes for priority based selection,
   * Currently only valid for PARTITION_TYPE_USR_X86_64.
   * TRIES_LEFT and PRIORITY are 4 bit wide fields.  */
  GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_PRIORITY		= 48,
  GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_TRIES_LEFT		= 52,
  GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_SUCCESSFUL		= 56,
};

/* Helpers for reading/writing partition attributes.  */
static inline grub_uint64_t
grub_gpt_entry_attribute (struct grub_gpt_partentry *entry,
			  enum grub_gpt_part_attr_offset offset,
			  unsigned int bits)
{
  grub_uint64_t attrib = grub_le_to_cpu64 (entry->attrib);

  return (attrib >> offset) & ((1ULL << bits) - 1);
}

static inline void
grub_gpt_entry_set_attribute (struct grub_gpt_partentry *entry,
			      grub_uint64_t value,
			      enum grub_gpt_part_attr_offset offset,
			      unsigned int bits)
{
  grub_uint64_t attrib, mask;

  mask = (((1ULL << bits) - 1) << offset);
  attrib = grub_le_to_cpu64 (entry->attrib) & ~mask;
  attrib |= ((value << offset) & mask);
  entry->attrib = grub_cpu_to_le64 (attrib);
}

/* Basic GPT partmap module.  */
grub_err_t
grub_gpt_partition_map_iterate (grub_disk_t disk,
				grub_partition_iterate_hook_t hook,
				void *hook_data);

/* Advanced GPT library.  */

/* Status bits for the grub_gpt.status field.  */
#define GRUB_GPT_PROTECTIVE_MBR		0x01
#define GRUB_GPT_HYBRID_MBR		0x02
#define GRUB_GPT_PRIMARY_HEADER_VALID	0x04
#define GRUB_GPT_PRIMARY_ENTRIES_VALID	0x08
#define GRUB_GPT_BACKUP_HEADER_VALID	0x10
#define GRUB_GPT_BACKUP_ENTRIES_VALID	0x20

/* UEFI requires the entries table to be at least 16384 bytes for a
 * total of 128 entries given the standard 128 byte entry size.  */
#define GRUB_GPT_DEFAULT_ENTRIES_SIZE	16384
#define GRUB_GPT_DEFAULT_ENTRIES_LENGTH	\
  (GRUB_GPT_DEFAULT_ENTRIES_SIZE / sizeof (struct grub_gpt_partentry))

struct grub_gpt
{
  /* Bit field indicating which structures on disk are valid.  */
  unsigned status;

  /* Protective or hybrid MBR.  */
  struct grub_msdos_partition_mbr mbr;

  /* Each of the two GPT headers.  */
  struct grub_gpt_header primary;
  struct grub_gpt_header backup;

  /* Only need one entries table, on disk both copies are identical.
   * The on disk entry size may be larger than our partentry struct so
   * the table cannot be indexed directly.  */
  void *entries;
  grub_size_t entries_size;

  /* Logarithm of sector size, in case GPT and disk driver disagree.  */
  unsigned int log_sector_size;
};
typedef struct grub_gpt *grub_gpt_t;

/* Helpers for checking the gpt status field.  */
static inline int
grub_gpt_mbr_valid (grub_gpt_t gpt)
{
  return ((gpt->status & GRUB_GPT_PROTECTIVE_MBR) ||
	  (gpt->status & GRUB_GPT_HYBRID_MBR));
}

static inline int
grub_gpt_primary_valid (grub_gpt_t gpt)
{
  return ((gpt->status & GRUB_GPT_PRIMARY_HEADER_VALID) &&
	  (gpt->status & GRUB_GPT_PRIMARY_ENTRIES_VALID));
}

static inline int
grub_gpt_backup_valid (grub_gpt_t gpt)
{
  return ((gpt->status & GRUB_GPT_BACKUP_HEADER_VALID) &&
	  (gpt->status & GRUB_GPT_BACKUP_ENTRIES_VALID));
}

static inline int
grub_gpt_both_valid (grub_gpt_t gpt)
{
  return grub_gpt_primary_valid (gpt) && grub_gpt_backup_valid (gpt);
}

/* Translate GPT sectors to GRUB's 512 byte block addresses.  */
static inline grub_disk_addr_t
grub_gpt_sector_to_addr (grub_gpt_t gpt, grub_uint64_t sector)
{
  return (sector << (gpt->log_sector_size - GRUB_DISK_SECTOR_BITS));
}

/* Allocates and fills new grub_gpt structure, free with grub_gpt_free.  */
grub_gpt_t grub_gpt_read (grub_disk_t disk);

/* Helper for indexing into the entries table.
 * Returns NULL when the end of the table has been reached.  */
struct grub_gpt_partentry * grub_gpt_get_partentry (grub_gpt_t gpt,
						    grub_uint32_t n);

/* Sync and update primary and backup headers if either are invalid.  */
grub_err_t grub_gpt_repair (grub_disk_t disk, grub_gpt_t gpt);

/* Recompute checksums and revalidate everything, must be called after
 * modifying any GPT data.  */
grub_err_t grub_gpt_update (grub_gpt_t gpt);

/* Write headers and entry tables back to disk.  */
grub_err_t grub_gpt_write (grub_disk_t disk, grub_gpt_t gpt);

void grub_gpt_free (grub_gpt_t gpt);

grub_err_t grub_gpt_pmbr_check (struct grub_msdos_partition_mbr *mbr);
grub_err_t grub_gpt_header_check (struct grub_gpt_header *gpt,
				  unsigned int log_sector_size);


/* Utilities for simple partition data lookups, usage is intended to
 * be similar to fs->label and fs->uuid functions.  */

/* Return the partition label of the device DEVICE in LABEL.
 * The label is in a new buffer and should be freed by the caller.  */
grub_err_t grub_gpt_part_label (grub_device_t device, char **label);

/* Return the partition uuid of the device DEVICE in UUID.
 * The uuid is in a new buffer and should be freed by the caller.  */
grub_err_t grub_gpt_part_uuid (grub_device_t device, char **uuid);

/* Return the disk uuid of the device DEVICE in UUID.
 * The uuid is in a new buffer and should be freed by the caller.  */
grub_err_t grub_gpt_disk_uuid (grub_device_t device, char **uuid);

#endif /* ! GRUB_GPT_PARTITION_HEADER */
