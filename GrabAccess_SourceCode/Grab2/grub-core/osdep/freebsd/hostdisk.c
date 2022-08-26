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

# include <sys/disk.h> /* DIOCGMEDIASIZE */
# include <sys/param.h>
# include <sys/sysctl.h>
# include <sys/mount.h>
# include <libgeom.h>

grub_int64_t
grub_util_get_fd_size_os (grub_util_fd_t fd, const char *name, unsigned *log_secsize)
{
  unsigned long long nr;
  unsigned sector_size, log_sector_size;

  if (ioctl (fd, DIOCGMEDIASIZE, &nr))
    return -1;

  if (ioctl (fd, DIOCGSECTORSIZE, &sector_size))
    return -1;
  if (sector_size & (sector_size - 1) || !sector_size)
    return -1;
  for (log_sector_size = 0;
       (1 << log_sector_size) < sector_size;
       log_sector_size++);

  if (log_secsize)
    *log_secsize = log_sector_size;

  if (nr & (sector_size - 1))
    grub_util_error ("%s", _("unaligned device size"));

  return nr;
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}

grub_util_fd_t
grub_util_fd_open (const char *os_dev, int flags)
{
  grub_util_fd_t ret;
  int sysctl_flags, sysctl_oldflags;
  size_t sysctl_size = sizeof (sysctl_flags);

#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif
#ifdef O_BINARY
  flags |= O_BINARY;
#endif

  if (sysctlbyname ("kern.geom.debugflags", &sysctl_oldflags, &sysctl_size, NULL, 0))
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "cannot get current flags of sysctl kern.geom.debugflags");
      return GRUB_UTIL_FD_INVALID;
    }
  sysctl_flags = sysctl_oldflags | 0x10;
  if (! (sysctl_oldflags & 0x10)
      && sysctlbyname ("kern.geom.debugflags", NULL , 0, &sysctl_flags, sysctl_size))
    {
      if (errno == EPERM)
	/* Running as an unprivileged user; don't worry about restoring
	   flags, although if we try to write to anything interesting such
	   as the MBR then we may fail later.  */
	sysctl_oldflags = 0x10;
      else
	{
	  grub_error (GRUB_ERR_BAD_DEVICE, "cannot set flags of sysctl kern.geom.debugflags");
	  return GRUB_UTIL_FD_INVALID;
	}
    }

  ret = open (os_dev, flags, S_IROTH | S_IRGRP | S_IRUSR | S_IWUSR);

  if (! (sysctl_oldflags & 0x10)
      && sysctlbyname ("kern.geom.debugflags", NULL , 0, &sysctl_oldflags, sysctl_size))
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "cannot set flags back to the old value for sysctl kern.geom.debugflags");
      close (ret);
      return GRUB_UTIL_FD_INVALID;
    }

  return ret;
}
