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

# include <sys/ioctl.h>
# include <sys/disklabel.h>    /* struct disklabel */
# include <sys/disk.h>    /* struct dkwedge_info */
# ifdef HAVE_GETRAWPARTITION
#  include <util.h>    /* getrawpartition */
# endif /* HAVE_GETRAWPARTITION */
# if defined(__NetBSD__)
# include <sys/fdio.h>
# endif
# if defined(__OpenBSD__)
# include <sys/dkio.h>
# endif

#if defined(__NetBSD__)
/* Adjust device driver parameters.  This function should be called just
   after successfully opening the device.  For now, it simply prevents the
   floppy driver from retrying operations on failure, as otherwise the
   driver takes a while to abort when there is no floppy in the drive.  */
static void
configure_device_driver (grub_util_fd_t fd)
{
  struct stat st;

  if (fstat (fd, &st) < 0 || ! S_ISCHR (st.st_mode))
    return;
  if (major(st.st_rdev) == RAW_FLOPPY_MAJOR)
    {
      int floppy_opts;

      if (ioctl (fd, FDIOCGETOPTS, &floppy_opts) == -1)
	return;
      floppy_opts |= FDOPT_NORETRY;
      if (ioctl (fd, FDIOCSETOPTS, &floppy_opts) == -1)
	return;
    }
}
grub_util_fd_t
grub_util_fd_open (const char *os_dev, int flags)
{
  grub_util_fd_t ret;

#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif
#ifdef O_BINARY
  flags |= O_BINARY;
#endif

  ret = open (os_dev, flags, S_IROTH | S_IRGRP | S_IRUSR | S_IWUSR);
  if (ret >= 0)
    configure_device_driver (fd);
  return ret;
}

#endif

grub_int64_t
grub_util_get_fd_size_os (grub_util_fd_t fd, const char *name, unsigned *log_secsize)
{
  struct disklabel label;
  unsigned sector_size, log_sector_size;

#if defined(__NetBSD__)
  grub_hostdisk_configure_device_driver (fd);
#endif

  if (ioctl (fd, DIOCGDINFO, &label) == -1)
    return -1;

  sector_size = label.d_secsize;
  if (sector_size & (sector_size - 1) || !sector_size)
    return -1;
  for (log_sector_size = 0;
       (1 << log_sector_size) < sector_size;
       log_sector_size++);

  if (log_secsize)
    *log_secsize = log_sector_size;

  return (grub_uint64_t) label.d_secperunit << log_sector_size;
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}
