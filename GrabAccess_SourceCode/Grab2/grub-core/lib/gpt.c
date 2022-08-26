/* gpt.c - Read/Verify/Write GUID Partition Tables (GPT).  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2006,2007,2008  Free Software Foundation, Inc.
 *  Copyright (C) 2014 CoreOS, Inc.
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

#include <grub/charset.h>
#include <grub/crypto.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>

GRUB_MOD_LICENSE ("GPLv3+");

#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

static grub_uint8_t grub_gpt_magic[] = GRUB_GPT_HEADER_MAGIC;

static grub_err_t
grub_gpt_read_entries (grub_disk_t disk, grub_gpt_t gpt,
		       struct grub_gpt_header *header,
		       void **ret_entries,
		       grub_size_t *ret_entries_size);

char *
grub_gpt_guid_to_str (grub_gpt_guid_t *guid)
{
  return grub_xasprintf ("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			 grub_le_to_cpu32 (guid->data1),
			 grub_le_to_cpu16 (guid->data2),
			 grub_le_to_cpu16 (guid->data3),
			 guid->data4[0], guid->data4[1],
			 guid->data4[2], guid->data4[3],
			 guid->data4[4], guid->data4[5],
			 guid->data4[6], guid->data4[7]);
}

static grub_err_t
grub_gpt_device_partentry (grub_device_t device,
			   struct grub_gpt_partentry *entry)
{
  grub_disk_t disk = device->disk;
  grub_partition_t p;
  grub_err_t err;

  if (!disk || !disk->partition)
    return grub_error (GRUB_ERR_BUG, "not a partition");

  if (grub_strcmp (disk->partition->partmap->name, "gpt"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "not a GPT partition");

  p = disk->partition;
  disk->partition = p->parent;
  err = grub_disk_read (disk, p->offset, p->index, sizeof (*entry), entry);
  disk->partition = p;

  return err;
}

grub_err_t
grub_gpt_part_label (grub_device_t device, char **label)
{
  struct grub_gpt_partentry entry;
  const grub_size_t name_len = ARRAY_SIZE (entry.name);
  const grub_size_t label_len = name_len * GRUB_MAX_UTF8_PER_UTF16 + 1;
  grub_size_t i;
  grub_uint8_t *end;

  if (grub_gpt_device_partentry (device, &entry))
    return grub_errno;

  *label = grub_malloc (label_len);
  if (!*label)
    return grub_errno;

  for (i = 0; i < name_len; i++)
    entry.name[i] = grub_le_to_cpu16 (entry.name[i]);

  end = grub_utf16_to_utf8 ((grub_uint8_t *) *label, entry.name, name_len);
  *end = '\0';

  return GRUB_ERR_NONE;
}

grub_err_t
grub_gpt_part_uuid (grub_device_t device, char **uuid)
{
  struct grub_gpt_partentry entry;

  if (grub_gpt_device_partentry (device, &entry))
    return grub_errno;

  *uuid = grub_gpt_guid_to_str (&entry.guid);
  if (!*uuid)
    return grub_errno;

  return GRUB_ERR_NONE;
}

static struct grub_gpt_header *
grub_gpt_get_header (grub_gpt_t gpt)
{
  if (gpt->status & GRUB_GPT_PRIMARY_HEADER_VALID)
    return &gpt->primary;
  else if (gpt->status & GRUB_GPT_BACKUP_HEADER_VALID)
    return &gpt->backup;

  grub_error (GRUB_ERR_BUG, "No valid GPT header");
  return NULL;
}

grub_err_t
grub_gpt_disk_uuid (grub_device_t device, char **uuid)
{
  struct grub_gpt_header *header;

  grub_gpt_t gpt = grub_gpt_read (device->disk);
  if (!gpt)
    goto done;

  header = grub_gpt_get_header (gpt);
  if (!header)
    goto done;

  *uuid = grub_gpt_guid_to_str (&header->guid);

done:
  grub_gpt_free (gpt);
  return grub_errno;
}

static grub_uint64_t
grub_gpt_size_to_sectors (grub_gpt_t gpt, grub_size_t size)
{
  unsigned int sector_size;
  grub_uint64_t sectors;

  sector_size = 1U << gpt->log_sector_size;
  sectors = size / sector_size;
  if (size % sector_size)
    sectors++;

  return sectors;
}

/* Copied from grub-core/kern/disk_common.c grub_disk_adjust_range so we can
 * avoid attempting to use disk->total_sectors when GRUB won't let us.
 * TODO: Why is disk->total_sectors not set to GRUB_DISK_SIZE_UNKNOWN?  */
static int
grub_gpt_disk_size_valid (grub_disk_t disk)
{
  grub_disk_addr_t total_sectors;

  /* Transform total_sectors to number of 512B blocks.  */
  total_sectors = disk->total_sectors << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS);

  /* Some drivers have problems with disks above reasonable.
     Treat unknown as 1EiB disk. While on it, clamp the size to 1EiB.
     Just one condition is enough since GRUB_DISK_UNKNOWN_SIZE << ls is always
     above 9EiB.
  */
  if (total_sectors > (1ULL << 51))
    return 0;

  return 1;
}

static void
grub_gpt_lecrc32 (grub_uint32_t *crc, const void *data, grub_size_t len)
{
  grub_uint32_t crc32_val;

  grub_crypto_hash (GRUB_MD_CRC32, &crc32_val, data, len);

  /* GRUB_MD_CRC32 always uses big endian, gpt is always little.  */
  *crc = grub_swap_bytes32 (crc32_val);
}

static void
grub_gpt_header_lecrc32 (grub_uint32_t *crc, struct grub_gpt_header *header)
{
  grub_uint32_t old, new;

  /* crc32 must be computed with the field cleared.  */
  old = header->crc32;
  header->crc32 = 0;
  grub_gpt_lecrc32 (&new, header, sizeof (*header));
  header->crc32 = old;

  *crc = new;
}

/* Make sure the MBR is a protective MBR and not a normal MBR.  */
grub_err_t
grub_gpt_pmbr_check (struct grub_msdos_partition_mbr *mbr)
{
  unsigned int i;

  if (mbr->signature !=
      grub_cpu_to_le16_compile_time (GRUB_PC_PARTITION_SIGNATURE))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid MBR signature");

  for (i = 0; i < sizeof (mbr->entries); i++)
    if (mbr->entries[i].type == GRUB_PC_PARTITION_TYPE_GPT_DISK)
      return GRUB_ERR_NONE;

  return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid protective MBR");
}

static grub_uint64_t
grub_gpt_entries_size (struct grub_gpt_header *gpt)
{
  return (grub_uint64_t) grub_le_to_cpu32 (gpt->maxpart) *
         (grub_uint64_t) grub_le_to_cpu32 (gpt->partentry_size);
}

static grub_uint64_t
grub_gpt_entries_sectors (struct grub_gpt_header *gpt,
			  unsigned int log_sector_size)
{
  grub_uint64_t sector_bytes, entries_bytes;

  sector_bytes = 1ULL << log_sector_size;
  entries_bytes = grub_gpt_entries_size (gpt);
  return grub_divmod64(entries_bytes + sector_bytes - 1, sector_bytes, NULL);
}

static int
is_pow2 (grub_uint32_t n)
{
  return (n & (n - 1)) == 0;
}

grub_err_t
grub_gpt_header_check (struct grub_gpt_header *gpt,
		       unsigned int log_sector_size)
{
  grub_uint32_t crc = 0, size;
  grub_uint64_t start, end;

  if (grub_memcmp (gpt->magic, grub_gpt_magic, sizeof (grub_gpt_magic)) != 0)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT signature");

  if (gpt->version != GRUB_GPT_HEADER_VERSION)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "unknown GPT version");

  grub_gpt_header_lecrc32 (&crc, gpt);
  if (gpt->crc32 != crc)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT header crc32");

  /* The header size "must be greater than or equal to 92 and must be less
   * than or equal to the logical block size."  */
  size = grub_le_to_cpu32 (gpt->headersize);
  if (size < 92U || size > (1U << log_sector_size))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT header size");

  /* The partition entry size must be "a value of 128*(2^n) where n is an
   * integer greater than or equal to zero (e.g., 128, 256, 512, etc.)."  */
  size = grub_le_to_cpu32 (gpt->partentry_size);
  if (size < 128U || size % 128U || !is_pow2 (size / 128U))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT entry size");

  /* The minimum entries table size is specified in terms of bytes,
   * regardless of how large the individual entry size is.  */
  if (grub_gpt_entries_size (gpt) < GRUB_GPT_DEFAULT_ENTRIES_SIZE)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT entry table size");

  /* And of course there better be some space for partitions!  */
  start = grub_le_to_cpu64 (gpt->start);
  end = grub_le_to_cpu64 (gpt->end);
  if (start > end)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid usable sectors");

  return GRUB_ERR_NONE;
}

static int
grub_gpt_headers_equal (grub_gpt_t gpt)
{
  /* Assume headers passed grub_gpt_header_check so skip magic and version.
   * Individual fields must be checked instead of just using memcmp because
   * crc32, header, alternate, and partitions will all normally differ.  */

  if (gpt->primary.headersize != gpt->backup.headersize ||
      gpt->primary.header_lba != gpt->backup.alternate_lba ||
      gpt->primary.alternate_lba != gpt->backup.header_lba ||
      gpt->primary.start != gpt->backup.start ||
      gpt->primary.end != gpt->backup.end ||
      gpt->primary.maxpart != gpt->backup.maxpart ||
      gpt->primary.partentry_size != gpt->backup.partentry_size ||
      gpt->primary.partentry_crc32 != gpt->backup.partentry_crc32)
    return 0;

  return grub_memcmp(&gpt->primary.guid, &gpt->backup.guid,
                     sizeof(grub_gpt_guid_t)) == 0;
}

static grub_err_t
grub_gpt_check_primary (grub_gpt_t gpt)
{
  grub_uint64_t backup, primary, entries, entries_len, start, end;

  primary = grub_le_to_cpu64 (gpt->primary.header_lba);
  backup = grub_le_to_cpu64 (gpt->primary.alternate_lba);
  entries = grub_le_to_cpu64 (gpt->primary.partitions);
  entries_len = grub_gpt_entries_sectors(&gpt->primary, gpt->log_sector_size);
  start = grub_le_to_cpu64 (gpt->primary.start);
  end = grub_le_to_cpu64 (gpt->primary.end);

  grub_dprintf ("gpt", "Primary GPT layout:\n"
		"primary header = 0x%llx backup header = 0x%llx\n"
		"entries location = 0x%llx length = 0x%llx\n"
		"first usable = 0x%llx last usable = 0x%llx\n",
		(unsigned long long) primary,
		(unsigned long long) backup,
		(unsigned long long) entries,
		(unsigned long long) entries_len,
		(unsigned long long) start,
		(unsigned long long) end);

  if (grub_gpt_header_check (&gpt->primary, gpt->log_sector_size))
    return grub_errno;
  if (primary != 1)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid primary GPT LBA");
  if (entries <= 1 || entries+entries_len > start)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid entries location");
  if (backup <= end)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid backup GPT LBA");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_gpt_check_backup (grub_gpt_t gpt)
{
  grub_uint64_t backup, primary, entries, entries_len, start, end;

  backup = grub_le_to_cpu64 (gpt->backup.header_lba);
  primary = grub_le_to_cpu64 (gpt->backup.alternate_lba);
  entries = grub_le_to_cpu64 (gpt->backup.partitions);
  entries_len = grub_gpt_entries_sectors(&gpt->backup, gpt->log_sector_size);
  start = grub_le_to_cpu64 (gpt->backup.start);
  end = grub_le_to_cpu64 (gpt->backup.end);

  grub_dprintf ("gpt", "Backup GPT layout:\n"
		"primary header = 0x%llx backup header = 0x%llx\n"
		"entries location = 0x%llx length = 0x%llx\n"
		"first usable = 0x%llx last usable = 0x%llx\n",
		(unsigned long long) primary,
		(unsigned long long) backup,
		(unsigned long long) entries,
		(unsigned long long) entries_len,
		(unsigned long long) start,
		(unsigned long long) end);

  if (grub_gpt_header_check (&gpt->backup, gpt->log_sector_size))
    return grub_errno;
  if (primary != 1)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid primary GPT LBA");
  if (entries <= end || entries+entries_len > backup)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid entries location");
  if (backup <= end)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid backup GPT LBA");

  /* If both primary and backup are valid but differ prefer the primary.  */
  if ((gpt->status & GRUB_GPT_PRIMARY_HEADER_VALID) &&
      !grub_gpt_headers_equal (gpt))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "backup GPT out of sync");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_gpt_read_primary (grub_disk_t disk, grub_gpt_t gpt)
{
  grub_disk_addr_t addr;

  /* TODO: The gpt partmap module searches for the primary header instead
   * of relying on the disk's sector size. For now trust the disk driver
   * but eventually this code should match the existing behavior.  */
  gpt->log_sector_size = disk->log_sector_size;

  grub_dprintf ("gpt", "reading primary GPT from sector 0x1\n");

  addr = grub_gpt_sector_to_addr (gpt, 1);
  if (grub_disk_read (disk, addr, 0, sizeof (gpt->primary), &gpt->primary))
    return grub_errno;

  if (grub_gpt_check_primary (gpt))
    return grub_errno;

  gpt->status |= GRUB_GPT_PRIMARY_HEADER_VALID;

  if (grub_gpt_read_entries (disk, gpt, &gpt->primary,
			     &gpt->entries, &gpt->entries_size))
    return grub_errno;

  gpt->status |= GRUB_GPT_PRIMARY_ENTRIES_VALID;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_gpt_read_backup (grub_disk_t disk, grub_gpt_t gpt)
{
  void *entries = NULL;
  grub_size_t entries_size;
  grub_uint64_t sector;
  grub_disk_addr_t addr;

  /* Assumes gpt->log_sector_size == disk->log_sector_size  */
  if (gpt->status & GRUB_GPT_PRIMARY_HEADER_VALID)
    {
      sector = grub_le_to_cpu64 (gpt->primary.alternate_lba);
      if (grub_gpt_disk_size_valid (disk) && sector >= disk->total_sectors)
	return grub_error (GRUB_ERR_OUT_OF_RANGE,
			   "backup GPT located at 0x%llx, "
			   "beyond last disk sector at 0x%llx",
			   (unsigned long long) sector,
			   (unsigned long long) disk->total_sectors - 1);
    }
  else if (grub_gpt_disk_size_valid (disk))
    sector = disk->total_sectors - 1;
  else
    return grub_error (GRUB_ERR_OUT_OF_RANGE,
		       "size of disk unknown, cannot locate backup GPT");

  grub_dprintf ("gpt", "reading backup GPT from sector 0x%llx\n",
		(unsigned long long) sector);

  addr = grub_gpt_sector_to_addr (gpt, sector);
  if (grub_disk_read (disk, addr, 0, sizeof (gpt->backup), &gpt->backup))
    return grub_errno;

  if (grub_gpt_check_backup (gpt))
    return grub_errno;

  /* Ensure the backup header thinks it is located where we found it.  */
  if (grub_le_to_cpu64 (gpt->backup.header_lba) != sector)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid backup GPT LBA");

  gpt->status |= GRUB_GPT_BACKUP_HEADER_VALID;

  if (grub_gpt_read_entries (disk, gpt, &gpt->backup,
			     &entries, &entries_size))
    return grub_errno;

  if (gpt->status & GRUB_GPT_PRIMARY_ENTRIES_VALID)
    {
      if (entries_size != gpt->entries_size ||
	  grub_memcmp (entries, gpt->entries, entries_size) != 0)
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "backup GPT out of sync");

      grub_free (entries);
    }
  else
    {
      gpt->entries = entries;
      gpt->entries_size = entries_size;
    }

  gpt->status |= GRUB_GPT_BACKUP_ENTRIES_VALID;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_gpt_read_entries (grub_disk_t disk, grub_gpt_t gpt,
		       struct grub_gpt_header *header,
		       void **ret_entries,
		       grub_size_t *ret_entries_size)
{
  void *entries = NULL;
  grub_uint32_t count, size, crc;
  grub_uint64_t sector;
  grub_disk_addr_t addr;
  grub_size_t entries_size;

  /* Grub doesn't include calloc, hence the manual overflow check.  */
  count = grub_le_to_cpu32 (header->maxpart);
  size = grub_le_to_cpu32 (header->partentry_size);
  entries_size = count *size;
  if (size && entries_size / size != count)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }

  /* Double check that the header was validated properly.  */
  if (entries_size < GRUB_GPT_DEFAULT_ENTRIES_SIZE)
    return grub_error (GRUB_ERR_BUG, "invalid GPT entries table size");

  entries = grub_malloc (entries_size);
  if (!entries)
    goto fail;

  sector = grub_le_to_cpu64 (header->partitions);
  grub_dprintf ("gpt", "reading GPT %lu entries from sector 0x%llx\n",
		(unsigned long) count,
		(unsigned long long) sector);

  addr = grub_gpt_sector_to_addr (gpt, sector);
  if (grub_disk_read (disk, addr, 0, entries_size, entries))
    goto fail;

  grub_gpt_lecrc32 (&crc, entries, entries_size);
  if (crc != header->partentry_crc32)
    {
      grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid GPT entry crc32");
      goto fail;
    }

  *ret_entries = entries;
  *ret_entries_size = entries_size;
  return GRUB_ERR_NONE;

fail:
  grub_free (entries);
  return grub_errno;
}

grub_gpt_t
grub_gpt_read (grub_disk_t disk)
{
  grub_gpt_t gpt;

  grub_dprintf ("gpt", "reading GPT from %s\n", disk->name);

  gpt = grub_zalloc (sizeof (*gpt));
  if (!gpt)
    goto fail;

  if (grub_disk_read (disk, 0, 0, sizeof (gpt->mbr), &gpt->mbr))
    goto fail;

  /* Check the MBR but errors aren't reported beyond the status bit.  */
  if (grub_gpt_pmbr_check (&gpt->mbr))
    grub_errno = GRUB_ERR_NONE;
  else
    gpt->status |= GRUB_GPT_PROTECTIVE_MBR;

  /* If both the primary and backup fail report the primary's error.  */
  if (grub_gpt_read_primary (disk, gpt))
    {
      grub_error_push ();
      grub_gpt_read_backup (disk, gpt);
      grub_error_pop ();
    }
  else
    grub_gpt_read_backup (disk, gpt);

  /* If either succeeded clear any possible error from the other.  */
  if (grub_gpt_primary_valid (gpt) || grub_gpt_backup_valid (gpt))
    grub_errno = GRUB_ERR_NONE;
  else
    goto fail;

  return gpt;

fail:
  grub_gpt_free (gpt);
  return NULL;
}

struct grub_gpt_partentry *
grub_gpt_get_partentry (grub_gpt_t gpt, grub_uint32_t n)
{
  struct grub_gpt_header *header;
  grub_size_t offset;

  header = grub_gpt_get_header (gpt);
  if (!header)
    return NULL;

  if (n >= grub_le_to_cpu32 (header->maxpart))
    return NULL;

  offset = (grub_size_t) grub_le_to_cpu32 (header->partentry_size) * n;
  return (struct grub_gpt_partentry *) ((char *) gpt->entries + offset);
}

grub_err_t
grub_gpt_repair (grub_disk_t disk, grub_gpt_t gpt)
{
  /* Skip if there is nothing to do.  */
  if (grub_gpt_both_valid (gpt))
    return GRUB_ERR_NONE;

  grub_dprintf ("gpt", "repairing GPT for %s\n", disk->name);

  if (disk->log_sector_size != gpt->log_sector_size)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "GPT sector size must match disk sector size");

  if (grub_gpt_primary_valid (gpt))
    {
      grub_uint64_t backup_header;

      grub_dprintf ("gpt", "primary GPT is valid\n");

      /* Relocate backup to end if disk if the disk has grown.  */
      backup_header = grub_le_to_cpu64 (gpt->primary.alternate_lba);
      if (grub_gpt_disk_size_valid (disk) &&
	  disk->total_sectors - 1 > backup_header)
	{
	  backup_header = disk->total_sectors - 1;
	  grub_dprintf ("gpt", "backup GPT header relocated to 0x%llx\n",
			(unsigned long long) backup_header);

	  gpt->primary.alternate_lba = grub_cpu_to_le64 (backup_header);
	}

      grub_memcpy (&gpt->backup, &gpt->primary, sizeof (gpt->backup));
      gpt->backup.header_lba = gpt->primary.alternate_lba;
      gpt->backup.alternate_lba = gpt->primary.header_lba;
      gpt->backup.partitions = grub_cpu_to_le64 (backup_header -
	  grub_gpt_size_to_sectors (gpt, gpt->entries_size));
    }
  else if (grub_gpt_backup_valid (gpt))
    {
      grub_dprintf ("gpt", "backup GPT is valid\n");

      grub_memcpy (&gpt->primary, &gpt->backup, sizeof (gpt->primary));
      gpt->primary.header_lba = gpt->backup.alternate_lba;
      gpt->primary.alternate_lba = gpt->backup.header_lba;
      gpt->primary.partitions = grub_cpu_to_le64_compile_time (2);
    }
  else
    return grub_error (GRUB_ERR_BUG, "No valid GPT");

  if (grub_gpt_update (gpt))
    return grub_errno;

  grub_dprintf ("gpt", "repairing GPT for %s successful\n", disk->name);

  return GRUB_ERR_NONE;
}

grub_err_t
grub_gpt_update (grub_gpt_t gpt)
{
  grub_uint32_t crc;

  /* Clear status bits, require revalidation of everything.  */
  gpt->status &= ~(GRUB_GPT_PRIMARY_HEADER_VALID |
		   GRUB_GPT_PRIMARY_ENTRIES_VALID |
		   GRUB_GPT_BACKUP_HEADER_VALID |
		   GRUB_GPT_BACKUP_ENTRIES_VALID);

  /* Writing headers larger than our header structure are unsupported.  */
  gpt->primary.headersize =
    grub_cpu_to_le32_compile_time (sizeof (gpt->primary));
  gpt->backup.headersize =
    grub_cpu_to_le32_compile_time (sizeof (gpt->backup));

  grub_gpt_lecrc32 (&crc, gpt->entries, gpt->entries_size);
  gpt->primary.partentry_crc32 = crc;
  gpt->backup.partentry_crc32 = crc;

  grub_gpt_header_lecrc32 (&gpt->primary.crc32, &gpt->primary);
  grub_gpt_header_lecrc32 (&gpt->backup.crc32, &gpt->backup);

  if (grub_gpt_check_primary (gpt))
    {
      grub_error_push ();
      return grub_error (GRUB_ERR_BUG, "Generated invalid GPT primary header");
    }

  gpt->status |= (GRUB_GPT_PRIMARY_HEADER_VALID |
		  GRUB_GPT_PRIMARY_ENTRIES_VALID);

  if (grub_gpt_check_backup (gpt))
    {
      grub_error_push ();
      return grub_error (GRUB_ERR_BUG, "Generated invalid GPT backup header");
    }

  gpt->status |= (GRUB_GPT_BACKUP_HEADER_VALID |
		  GRUB_GPT_BACKUP_ENTRIES_VALID);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_gpt_write_table (grub_disk_t disk, grub_gpt_t gpt,
		      struct grub_gpt_header *header)
{
  grub_disk_addr_t addr;

  if (grub_le_to_cpu32 (header->headersize) != sizeof (*header))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "Header size is %u, must be %" PRIuGRUB_SIZE,
		       grub_le_to_cpu32 (header->headersize),
		       sizeof (*header));

  addr = grub_gpt_sector_to_addr (gpt, grub_le_to_cpu64 (header->header_lba));
  if (addr == 0)
    return grub_error (GRUB_ERR_BUG,
		       "Refusing to write GPT header to address 0x0");
  if (grub_disk_write (disk, addr, 0, sizeof (*header), header))
    return grub_errno;

  addr = grub_gpt_sector_to_addr (gpt, grub_le_to_cpu64 (header->partitions));
  if (addr < 2)
    return grub_error (GRUB_ERR_BUG,
		       "Refusing to write GPT entries to address 0x%llx",
		       (unsigned long long) addr);
  if (grub_disk_write (disk, addr, 0, gpt->entries_size, gpt->entries))
    return grub_errno;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_gpt_write (grub_disk_t disk, grub_gpt_t gpt)
{
  grub_uint64_t backup_header;

  /* TODO: update/repair protective MBRs too.  */

  if (!grub_gpt_both_valid (gpt))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "Invalid GPT data");

  /* Write the backup GPT first so if writing fails the update is aborted
   * and the primary is left intact.  However if the backup location is
   * inaccessible we have to just skip and hope for the best, the backup
   * will need to be repaired in the OS.  */
  backup_header = grub_le_to_cpu64 (gpt->backup.header_lba);
  if (grub_gpt_disk_size_valid (disk) &&
      backup_header >= disk->total_sectors)
    {
      grub_printf ("warning: backup GPT located at 0x%llx, "
		   "beyond last disk sector at 0x%llx\n",
		   (unsigned long long) backup_header,
		   (unsigned long long) disk->total_sectors - 1);
      grub_printf ("warning: only writing primary GPT, "
	           "the backup GPT must be repaired from the OS\n");
    }
  else
    {
      grub_dprintf ("gpt", "writing backup GPT to %s\n", disk->name);
      if (grub_gpt_write_table (disk, gpt, &gpt->backup))
	return grub_errno;
    }

  grub_dprintf ("gpt", "writing primary GPT to %s\n", disk->name);
  if (grub_gpt_write_table (disk, gpt, &gpt->primary))
    return grub_errno;

  return GRUB_ERR_NONE;
}

void
grub_gpt_free (grub_gpt_t gpt)
{
  if (!gpt)
    return;

  grub_free (gpt->entries);
  grub_free (gpt);
}

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

