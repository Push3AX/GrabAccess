/* grub-setup.c - make GRUB usable */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011  Free Software Foundation, Inc.
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

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/util/misc.h>
#include <grub/util/install.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void
grub_install_get_blocklist (grub_device_t root_dev,
			    const char *core_path,
			    const char *core_img __attribute__ ((unused)),
			    size_t core_size,
			    void (*callback) (grub_disk_addr_t sector,
					      unsigned offset,
					      unsigned length,
					      void *data),
			    void *hook_data)
{
  grub_partition_t container = root_dev->disk->partition;
  grub_uint64_t container_start = grub_partition_get_start (container);
  struct fiemap fie1;
  int fd;

  /* Write the first two sectors of the core image onto the disk.  */
  grub_util_info ("opening the core image `%s'", core_path);
  fd = open (core_path, O_RDONLY);
  if (fd < 0)
    grub_util_error (_("cannot open `%s': %s"), core_path,
		     strerror (errno));

  grub_memset (&fie1, 0, sizeof (fie1));
  fie1.fm_length = core_size;
  fie1.fm_flags = FIEMAP_FLAG_SYNC;

  if (ioctl (fd, FS_IOC_FIEMAP, &fie1) < 0)
    {
      int nblocks, i;
      int bsize;
      int mul;

      grub_util_info ("FIEMAP failed. Reverting to FIBMAP");

      if (ioctl (fd, FIGETBSZ, &bsize) < 0)
	grub_util_error (_("can't retrieve blocklists: %s"),
			 strerror (errno));
      if (bsize & (GRUB_DISK_SECTOR_SIZE - 1))
	grub_util_error ("%s", _("blocksize is not divisible by 512"));
      if (!bsize)
	grub_util_error ("%s", _("invalid zero blocksize"));
      mul = bsize >> GRUB_DISK_SECTOR_BITS;
      nblocks = (core_size + bsize - 1) / bsize;
      if (mul == 0 || nblocks == 0)
	grub_util_error ("%s", _("can't retrieve blocklists"));
      for (i = 0; i < nblocks; i++)
	{
	  unsigned blk = i;
	  int rest;
	  if (ioctl (fd, FIBMAP, &blk) < 0)
	    grub_util_error (_("can't retrieve blocklists: %s"),
			     strerror (errno));
	    
	  rest = core_size - ((i * mul) << GRUB_DISK_SECTOR_BITS);
	  if (rest <= 0)
	    break;
	  if (rest > GRUB_DISK_SECTOR_SIZE * mul)
	    rest = GRUB_DISK_SECTOR_SIZE * mul;
	  callback (((grub_uint64_t) blk) * mul
		    + container_start,
		    0, rest, hook_data);
	}
    }
  else
    {
      struct fiemap *fie2;
      int i;
      fie2 = xmalloc (sizeof (*fie2)
		      + fie1.fm_mapped_extents
		      * sizeof (fie1.fm_extents[1]));
      memset (fie2, 0, sizeof (*fie2)
	      + fie1.fm_mapped_extents * sizeof (fie2->fm_extents[1]));
      fie2->fm_length = core_size;
      fie2->fm_flags = FIEMAP_FLAG_SYNC;
      fie2->fm_extent_count = fie1.fm_mapped_extents;
      if (ioctl (fd, FS_IOC_FIEMAP, fie2) < 0)
	grub_util_error (_("can't retrieve blocklists: %s"),
			 strerror (errno));
      for (i = 0; i < fie2->fm_mapped_extents; i++)
	{
	  callback ((fie2->fm_extents[i].fe_physical
		     >> GRUB_DISK_SECTOR_BITS)
		    + container_start,
		    fie2->fm_extents[i].fe_physical
		    & (GRUB_DISK_SECTOR_SIZE - 1),
		    fie2->fm_extents[i].fe_length, hook_data);
	}
      free (fie2);
    }
  close (fd);
}
