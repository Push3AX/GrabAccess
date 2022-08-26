/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config-util.h>

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <Drivers.h>
#include <StorageDefs.h>

grub_int64_t
grub_util_get_fd_size_os (grub_util_fd_t fd,
			  const char *name __attribute__ ((unused)),
			  unsigned *log_secsize)
{
  device_geometry part;
  unsigned lg;
  if (ioctl (fd, B_GET_GEOMETRY, &part, sizeof (part)) < 0)  
    return -1;
  for (lg = 0; (1 << lg) < part.bytes_per_sector; lg++);
  if (log_secsize)
    *log_secsize= lg;
  return ((grub_uint64_t) part.cylinder_count
	  * (grub_uint64_t) part.head_count
	  * (grub_uint64_t) part.sectors_per_track
	  * (grub_uint64_t) part.bytes_per_sector);
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}
