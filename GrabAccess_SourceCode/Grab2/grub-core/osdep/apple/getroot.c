/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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
#include <config.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include <grub/types.h>

#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>

#include <sys/wait.h>

# include <sys/disk.h>
# include <sys/param.h>
# include <sys/sysctl.h>
# include <sys/mount.h>

char *
grub_util_part_to_disk (const char *os_dev, struct stat *st,
			int *is_part)
{
  char *path = xstrdup (os_dev);

  if (! S_ISCHR (st->st_mode))
    {
      *is_part = 0;
      return path;
    }

  if (strncmp ("/dev/", path, 5) == 0)
    {
      char *p;
      for (p = path + 5; *p; ++p)
        if (grub_isdigit(*p))
          {
            p = strpbrk (p, "sp");
            if (p)
	      {
		*is_part = 1;
		*p = '\0';
	      }
            break;
          }
    }
  return path;
}

enum grub_dev_abstraction_types
grub_util_get_dev_abstraction_os (const char *os_dev __attribute__((unused)))
{
  return GRUB_DEV_ABSTRACTION_NONE;
}

int
grub_util_pull_device_os (const char *os_dev __attribute__ ((unused)),
			  enum grub_dev_abstraction_types ab __attribute__ ((unused)))
{
  return 0;
}

char *
grub_util_get_grub_dev_os (const char *os_dev __attribute__ ((unused)))
{
  return NULL;
}


grub_disk_addr_t
grub_util_find_partition_start_os (const char *dev __attribute__ ((unused)))
{
  return 0;
}
