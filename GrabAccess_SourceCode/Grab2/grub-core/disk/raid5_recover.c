/* raid5_recover.c - module to recover from faulty RAID4/5 arrays.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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
#include <grub/crypto.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_raid5_recover (struct grub_diskfilter_segment *array, int disknr,
                    char *buf, grub_disk_addr_t sector, grub_size_t size)
{
  char *buf2;
  int i;

  size <<= GRUB_DISK_SECTOR_BITS;
  buf2 = grub_malloc (size);
  if (!buf2)
    return grub_errno;

  grub_memset (buf, 0, size);

  for (i = 0; i < (int) array->node_count; i++)
    {
      grub_err_t err;

      if (i == disknr)
        continue;

      err = grub_diskfilter_read_node (&array->nodes[i], sector,
				       size >> GRUB_DISK_SECTOR_BITS, buf2);

      if (err)
        {
          grub_free (buf2);
          return err;
        }

      grub_crypto_xor (buf, buf, buf2, size);
    }

  grub_free (buf2);

  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(raid5rec)
{
  grub_raid5_recover_func = grub_raid5_recover;
}

GRUB_MOD_FINI(raid5rec)
{
  grub_raid5_recover_func = 0;
}
