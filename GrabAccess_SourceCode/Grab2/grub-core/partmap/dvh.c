/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2006,2007,2011  Free Software Foundation, Inc.
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

#include <grub/partition.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/symbol.h>
#include <grub/types.h>
#include <grub/err.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define DVH_MAGIC 0x0be5a941

struct grub_dvh_partition_descriptor
{
  grub_uint32_t length;
  grub_uint32_t start;
  grub_uint32_t type;  
} GRUB_PACKED;

struct grub_dvh_block
{
  grub_uint32_t magic;
  grub_uint8_t unused[308];
  struct grub_dvh_partition_descriptor parts[16];
  grub_uint32_t checksum;
  grub_uint32_t unused2;
} GRUB_PACKED;

static struct grub_partition_map grub_dvh_partition_map;

/* Verify checksum (true=ok).  */
static int
grub_dvh_is_valid (grub_uint32_t *label)
{
  grub_uint32_t *pos;
  grub_uint32_t sum = 0;

  for (pos = label;
       pos < (label + sizeof (struct grub_dvh_block) / 4);
       pos++)
    sum += grub_be_to_cpu32 (*pos);

  return ! sum;
}

static grub_err_t
dvh_partition_map_iterate (grub_disk_t disk,
			   grub_partition_iterate_hook_t hook, void *hook_data)
{
  struct grub_partition p;
  union
  {
    struct grub_dvh_block dvh;
    grub_uint32_t raw[0];
  } block;
  unsigned partnum;
  grub_err_t err;

  p.partmap = &grub_dvh_partition_map;
  err = grub_disk_read (disk, 0, 0, sizeof (struct grub_dvh_block),
			&block);
  if (err)
    return err;

  if (DVH_MAGIC != grub_be_to_cpu32 (block.dvh.magic))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "not a dvh partition table");

  if (! grub_dvh_is_valid (block.raw))
      return grub_error (GRUB_ERR_BAD_PART_TABLE, "invalid checksum");
  
  /* Maybe another error value would be better, because partition
     table _is_ recognized but invalid.  */
  for (partnum = 0; partnum < ARRAY_SIZE (block.dvh.parts); partnum++)
    {
      if (block.dvh.parts[partnum].length == 0)
	continue;

      if (partnum == 10)
	continue;

      p.start = grub_be_to_cpu32 (block.dvh.parts[partnum].start);
      p.len = grub_be_to_cpu32 (block.dvh.parts[partnum].length);
      p.number = p.index = partnum;
      if (hook (disk, &p, hook_data))
	break;
    }

  return grub_errno;
}


/* Partition map type.  */
static struct grub_partition_map grub_dvh_partition_map =
  {
    .name = "dvh",
    .iterate = dvh_partition_map_iterate,
  };

GRUB_MOD_INIT(part_dvh)
{
  grub_partition_map_register (&grub_dvh_partition_map);
}

GRUB_MOD_FINI(part_dvh)
{
  grub_partition_map_unregister (&grub_dvh_partition_map);
}

