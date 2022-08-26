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

# include <sys/ioctl.h>
# include <sys/disklabel.h>    /* struct disklabel */
# include <sys/disk.h>    /* struct dkwedge_info */
# ifdef HAVE_GETRAWPARTITION
#  include <util.h>    /* getrawpartition */
# endif /* HAVE_GETRAWPARTITION */
#if defined(__NetBSD__)
# include <sys/fdio.h>
#endif
#if defined(__OpenBSD__)
# include <sys/dkio.h>
#endif

char *
grub_util_part_to_disk (const char *os_dev, struct stat *st,
			int *is_part)
{
  int rawpart = -1;

  if (! S_ISCHR (st->st_mode))
    {
      *is_part = 0;
      return xstrdup (os_dev);
    }

# ifdef HAVE_GETRAWPARTITION
  rawpart = getrawpartition();
# endif /* HAVE_GETRAWPARTITION */
  if (rawpart < 0)
    return xstrdup (os_dev);

#if defined(__NetBSD__)
  /* NetBSD disk wedges are of the form "/dev/rdk.*".  */
  if (strncmp ("/dev/rdk", os_dev, sizeof("/dev/rdk") - 1) == 0)
    {
      struct dkwedge_info dkw;
      int fd;

      fd = open (os_dev, O_RDONLY);
      if (fd == -1)
	{
	  grub_error (GRUB_ERR_BAD_DEVICE,
		      N_("cannot open `%s': %s"), os_dev,
		      strerror (errno));
	  return xstrdup (os_dev);
	}
      if (ioctl (fd, DIOCGWEDGEINFO, &dkw) == -1)
	{
	  grub_error (GRUB_ERR_BAD_DEVICE,
		      "cannot get disk wedge info of `%s'", os_dev);
	  close (fd);
	  return xstrdup (os_dev);
	}
      *is_part = (dkw.dkw_offset != 0);
      close (fd);
      return xasprintf ("/dev/r%s%c", dkw.dkw_parent, 'a' + rawpart);
    }
#endif

  /* NetBSD (disk label) partitions are of the form "/dev/r[a-z]+[0-9][a-z]".  */
  if (strncmp ("/dev/r", os_dev, sizeof("/dev/r") - 1) == 0 &&
      (os_dev[sizeof("/dev/r") - 1] >= 'a' && os_dev[sizeof("/dev/r") - 1] <= 'z') &&
      strncmp ("fd", os_dev + sizeof("/dev/r") - 1, sizeof("fd") - 1) != 0)    /* not a floppy device name */
    {
      char *path = xstrdup (os_dev);
      char *p;
      for (p = path + sizeof("/dev/r"); *p >= 'a' && *p <= 'z'; p++);
      if (grub_isdigit(*p))
	{
	  p++;
	  if ((*p >= 'a' && *p <= 'z') && (*(p+1) == '\0'))
	    {
	      if (*p != 'a' + rawpart)
		*is_part = 1;
	      /* path matches the required regular expression and
		 p points to its last character.  */
	      *p = 'a' + rawpart;
	    }
	}
      return path;
    }

  return xstrdup (os_dev);
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
grub_util_find_partition_start_os (const char *dev)
{
  int fd;
#  if defined(__NetBSD__)
  struct dkwedge_info dkw;
#  endif /* defined(__NetBSD__) */
  struct disklabel label;
  int p_index;

  fd = grub_util_fd_open (dev, O_RDONLY);
  if (fd == -1)
    {
      grub_error (GRUB_ERR_BAD_DEVICE, N_("cannot open `%s': %s"),
		  dev, strerror (errno));
      return 0;
    }

#  if defined(__NetBSD__)
  /* First handle the case of disk wedges.  */
  if (ioctl (fd, DIOCGWEDGEINFO, &dkw) == 0)
    {
      close (fd);
      return (grub_disk_addr_t) dkw.dkw_offset;
    }
#  endif /* defined(__NetBSD__) */

  if (ioctl (fd, DIOCGDINFO, &label) == -1)
    {
      grub_error (GRUB_ERR_BAD_DEVICE,
		  "cannot get disk label of `%s'", dev);
      close (fd);
      return 0;
    }

  close (fd);

  if (dev[0])
    p_index = dev[strlen(dev) - 1] - 'a';
  else
    p_index = -1;
  
  if (p_index >= label.d_npartitions || p_index < 0)
    {
      grub_error (GRUB_ERR_BAD_DEVICE,
		  "no disk label entry for `%s'", dev);
      return 0;
    }
  return (grub_disk_addr_t) label.d_partitions[p_index].p_offset;
}
