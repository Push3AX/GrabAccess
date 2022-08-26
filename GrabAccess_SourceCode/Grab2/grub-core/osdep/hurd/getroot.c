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

#include <hurd.h>
#include <hurd/lookup.h>
#include <hurd/fs.h>
#include <sys/mman.h>

static char *
grub_util_find_hurd_root_device (const char *path)
{
  file_t file;
  error_t err;
  char *argz = NULL, *name = NULL, *ret;
  size_t argz_len = 0;
  int i;

  file = file_name_lookup (path, 0, 0);
  if (file == MACH_PORT_NULL)
    /* TRANSLATORS: The first %s is the file being looked at, the second %s is
       the error message.  */
    grub_util_error (_("cannot open `%s': %s"), path, strerror (errno));

  /* This returns catenated 0-terminated strings.  */
  err = file_get_fs_options (file, &argz, &argz_len);
  if (err)
    /* TRANSLATORS: On GNU/Hurd, a "translator" is similar to a filesystem
       mount, but handled by a userland daemon, whose invocation command line
       is being fetched here.  First %s is the file being looked at (for which
       we are fetching the "translator" command line), second %s is the error
       message.
       */
    grub_util_error (_("cannot get translator command line "
                       "for path `%s': %s"), path, strerror(err));
  if (argz_len == 0)
    grub_util_error (_("translator command line is empty for path `%s'"), path);

  /* Make sure the string is terminated.  */
  argz[argz_len-1] = 0;

  /* Skip first word (translator path) and options.  */
  for (i = strlen (argz) + 1; i < argz_len; i += strlen (argz + i) + 1)
    {
      if (argz[i] != '-')
        {
          /* Non-option.  Only accept one, assumed to be the FS path.  */
          /* XXX: this should be replaced by an RPC to the translator.  */
          if (name)
            /* TRANSLATORS: we expect to get something like
               /hurd/foobar --option1 --option2=baz /dev/something
             */
            grub_util_error (_("translator `%s' for path `%s' has several "
                               "non-option words, at least `%s' and `%s'"),
                               argz, path, name, argz + i);
          name = argz + i;
        }
    }

  if (!name)
    /* TRANSLATORS: we expect to get something like
       /hurd/foobar --option1 --option2=baz /dev/something
     */
    grub_util_error (_("translator `%s' for path `%s' is given only options, "
                       "cannot find device part"), argz, path);

  if (strncmp (name, "device:", sizeof ("device:") - 1) == 0)
    {
      char *dev_name = name + sizeof ("device:") - 1;
      size_t size = sizeof ("/dev/") - 1 + strlen (dev_name) + 1;
      char *next;
      ret = malloc (size);
      next = stpncpy (ret, "/dev/", size);
      stpncpy (next, dev_name, size - (next - ret));
    }
  else if (!strncmp (name, "file:", sizeof ("file:") - 1))
    ret = strdup (name + sizeof ("file:") - 1);
  else
    ret = strdup (name);

  munmap (argz, argz_len);
  return ret;
}

static int
is_fulldisk (const char *child, const char *parent)
{
  if (strcmp (parent, child) == 0)
    return 1;
  if (strncmp (parent, "/dev/", sizeof ("/dev/") - 1) == 0
      && child[0] !=0 && strcmp (parent + sizeof ("/dev/") - 1, child) == 0)
    return 1;
  if (strncmp (child, "/dev/", sizeof ("/dev/") - 1) == 0
      && parent[0] != 0 && strcmp (child + sizeof ("/dev/") - 1, parent) == 0)
    return 1;
  return 0;
}

char *
grub_util_part_to_disk (const char *os_dev,
			struct stat *st,
			int *is_part)
{
  char *path;
  grub_disk_addr_t offset;
  char *p;

  if (! S_ISBLK (st->st_mode))
    {
      *is_part = 0;
      return xstrdup (os_dev);
    }

  if (!grub_util_hurd_get_disk_info (os_dev, NULL, &offset, NULL, &path))
    return xstrdup (os_dev);

  /* Some versions of Hurd use badly glued Linux code to handle partitions
     resulting in partitions being promoted to disks.  */
  if (path && !(offset == 0 && is_fulldisk (path, os_dev)
		&& (strncmp ("/dev/sd", os_dev, 7) == 0
		    || strncmp ("/dev/hd", os_dev, 7) == 0)))
    {
      *is_part = !is_fulldisk (path, os_dev);
      if (path[0] != '/')
	{
	  char *n = xasprintf ("/dev/%s", path);
	  free (path);
	  path = n;
	}
      return path;
    }
  free (path);

  path = xstrdup (os_dev);

  p = strchr (path + 7, 's');
  if (p)
    {
      *is_part = 1;
      *p = '\0';
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
grub_util_find_partition_start_os (const char *dev)
{
  grub_uint32_t secsize;
  grub_disk_addr_t offset;
  char *path;
  if (!grub_util_hurd_get_disk_info (dev, &secsize, &offset, NULL, &path))
    return 0;
  if (path && !(offset == 0 && is_fulldisk (path, dev)
		&& (strncmp ("/dev/sd", dev, 7) == 0
		    || strncmp ("/dev/hd", dev, 7) == 0)))
    {
      free (path);
      return (secsize / 512) * offset;
    }
  free (path);
  return -1;
}

char **
grub_guess_root_devices (const char *dir)
{
  char **os_dev = NULL;

  os_dev = xmalloc (2 * sizeof (os_dev[0]));

  /* GNU/Hurd specific function.  */
  os_dev[0] = grub_util_find_hurd_root_device (dir);

  if (!os_dev[0])
    {
      free (os_dev);
      return 0;
    }

  os_dev[1] = 0;

  return os_dev;
}
