/* dfly.c - Read DragonFly BSD disklabel64.  */
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
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/partition.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct grub_partition_map grub_dfly_partition_map;

#define GRUB_PARTITION_DISKLABEL64_HEADER_SIZE 200

/* Full entry is 64 bytes however we really care only
   about offset and size which are in first 16 bytes.
   Avoid using too much stack.  */
#define GRUB_PARTITION_DISKLABEL64_ENTRY_SIZE 64
struct grub_partition_disklabel64_entry
{
  grub_uint64_t boffset;
  grub_uint64_t bsize;
};

/* Full entry is 200 bytes however we really care only
   about magic and number of partitions which are in first 16 bytes.
   Avoid using too much stack.  */
struct grub_partition_disklabel64
{
  grub_uint32_t   magic;
#define GRUB_DISKLABEL64_MAGIC		  ((grub_uint32_t)0xc4464c59)
  grub_uint32_t   crc;
  grub_uint32_t   unused;
  grub_uint32_t   npartitions;
};

static grub_err_t
dfly_partition_map_iterate (grub_disk_t disk,
			    grub_partition_iterate_hook_t hook,
			    void *hook_data)
{
  struct grub_partition part;
  unsigned partno, pos;
  struct grub_partition_disklabel64 label;

  part.partmap = &grub_dfly_partition_map;

  if (grub_disk_read (disk, 1, 0, sizeof (label), &label))
    return grub_errno;

  if (label.magic != grub_cpu_to_le32_compile_time (GRUB_DISKLABEL64_MAGIC))
    {
      grub_dprintf ("partition",
		    "bad magic (found 0x%x; wanted 0x%x)\n",
		    (unsigned int) grub_le_to_cpu32 (label.magic),
		    (unsigned int) GRUB_DISKLABEL64_MAGIC);
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "disklabel64 not found");
    }

  pos = GRUB_PARTITION_DISKLABEL64_HEADER_SIZE + GRUB_DISK_SECTOR_SIZE;

  for (partno = 0;
       partno < grub_le_to_cpu32 (label.npartitions); ++partno)
    {
      grub_disk_addr_t sector = pos >> GRUB_DISK_SECTOR_BITS;
      grub_off_t       offset = pos & (GRUB_DISK_SECTOR_SIZE - 1);
      struct grub_partition_disklabel64_entry dpart;

      pos += GRUB_PARTITION_DISKLABEL64_ENTRY_SIZE;
      if (grub_disk_read (disk, sector, offset, sizeof (dpart), &dpart))
	return grub_errno;

      grub_dprintf ("partition",
		    "partition %2d: offset 0x%llx, "
		    "size 0x%llx\n",
		    partno,
		    (unsigned long long) grub_le_to_cpu64 (dpart.boffset),
		    (unsigned long long) grub_le_to_cpu64 (dpart.bsize));

      /* Is partition initialized? */
      if (dpart.bsize == 0)
	continue;

      part.number = partno;
      part.start = grub_le_to_cpu64 (dpart.boffset) >> GRUB_DISK_SECTOR_BITS;
      part.len = grub_le_to_cpu64 (dpart.bsize) >> GRUB_DISK_SECTOR_BITS;

      /* This is counter-intuitive, but part.offset and sector have
       * the same type, and offset (NOT part.offset) is guaranteed
       * to fit into part.index. */
      part.offset = sector;
      part.index = offset;

      if (hook (disk, &part, hook_data))
	return grub_errno;
    }

  return GRUB_ERR_NONE;
}

/* Partition map type.  */
static struct grub_partition_map grub_dfly_partition_map =
{
    .name = "dfly",
    .iterate = dfly_partition_map_iterate,
};

GRUB_MOD_INIT(part_dfly)
{
  grub_partition_map_register (&grub_dfly_partition_map);
}

GRUB_MOD_FINI(part_dfly)
{
  grub_partition_map_unregister (&grub_dfly_partition_map);
}
