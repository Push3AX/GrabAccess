/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

static struct grub_partition_map grub_plan_partition_map;

static grub_err_t
plan_partition_map_iterate (grub_disk_t disk,
			    grub_partition_iterate_hook_t hook,
			    void *hook_data)
{
  struct grub_partition p;
  int ptr = 0;
  grub_err_t err;

  p.partmap = &grub_plan_partition_map;
  p.msdostype = 0;

  for (p.number = 0; ; p.number++)
    {
      char sig[sizeof ("part ") - 1];
      char c;

      p.offset = (ptr >> GRUB_DISK_SECTOR_BITS) + 1;
      p.index = ptr & (GRUB_DISK_SECTOR_SIZE - 1);

      err = grub_disk_read (disk, 1, ptr, sizeof (sig), sig);
      if (err)
	return err;
      if (grub_memcmp (sig, "part ", sizeof ("part ") - 1) != 0)
	break;
      ptr += sizeof (sig);
      do
	{
	  err = grub_disk_read (disk, 1, ptr, 1, &c);
	  if (err)
	    return err;
	  ptr++;
	}
      while (grub_isdigit (c) || grub_isalpha (c));
      if (c != ' ')
	break;
      p.start = 0;
      while (1)
	{
	  err = grub_disk_read (disk, 1, ptr, 1, &c);
	  if (err)
	    return err;
	  ptr++;
	  if (!grub_isdigit (c))
	    break;
	  p.start = p.start * 10 + (c - '0');
	}
      if (c != ' ')
	break;
      p.len = 0;
      while (1)
	{
	  err = grub_disk_read (disk, 1, ptr, 1, &c);
	  if (err)
	    return err;
	  ptr++;
	  if (!grub_isdigit (c))
	    break;
	  p.len = p.len * 10 + (c - '0');
	}
      if (c != '\n')
	break;
      p.len -= p.start;
      if (hook (disk, &p, hook_data))
	return grub_errno;
    }
  if (p.number == 0)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "not a plan partition table");

  return GRUB_ERR_NONE;
}

/* Partition map type.  */
static struct grub_partition_map grub_plan_partition_map =
  {
    .name = "plan",
    .iterate = plan_partition_map_iterate,
  };

GRUB_MOD_INIT(part_plan)
{
  grub_partition_map_register (&grub_plan_partition_map);
}

GRUB_MOD_FINI(part_plan)
{
  grub_partition_map_unregister (&grub_plan_partition_map);
}

