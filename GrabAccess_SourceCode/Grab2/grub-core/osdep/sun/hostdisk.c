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

# include <sys/dkio.h>

grub_int64_t
grub_util_get_fd_size_os (grub_util_fd_t fd, const char *name, unsigned *log_secsize)
{
  struct dk_minfo minfo;
  unsigned sector_size, log_sector_size;

  if (!ioctl (fd, DKIOCGMEDIAINFO, &minfo))
    return -1;

  sector_size = minfo.dki_lbsize;

  if (sector_size & (sector_size - 1) || !sector_size)
    return -1;
  for (log_sector_size = 0;
       (1 << log_sector_size) < sector_size;
       log_sector_size++);

  if (log_secsize)
    *log_secsize = log_sector_size;

  return minfo.dki_capacity << log_sector_size;
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}
