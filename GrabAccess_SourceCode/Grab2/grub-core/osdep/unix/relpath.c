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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <grub/util/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/mm.h>

/* This function never prints trailing slashes (so that its output
   can be appended a slash unconditionally).  */
char *
grub_make_system_path_relative_to_its_root (const char *path)
{
  struct stat st;
  char *p, *buf, *buf2, *buf3, *ret;
  uintptr_t offset = 0;
  dev_t num;
  size_t len;
  char *poolfs = NULL;

  /* canonicalize.  */
  p = grub_canonicalize_file_name (path);
  if (p == NULL)
    grub_util_error (_("failed to get canonical path of `%s'"), path);

#ifdef __linux__
  ret = grub_make_system_path_relative_to_its_root_os (p);
  if (ret)
    {
      free (p);
      return ret;
    }
#endif

  /* For ZFS sub-pool filesystems.  */
#ifndef __HAIKU__
  {
    char *dummy;
    grub_find_zpool_from_dir (p, &dummy, &poolfs);
  }
#endif

  len = strlen (p) + 1;
  buf = xstrdup (p);
  free (p);

  if (stat (buf, &st) < 0)
    grub_util_error (_("cannot stat `%s': %s"), buf, strerror (errno));

  buf2 = xstrdup (buf);
  num = st.st_dev;

  /* This loop sets offset to the number of chars of the root
     directory we're inspecting.  */
  while (1)
    {
      p = strrchr (buf, '/');
      if (p == NULL)
	/* This should never happen.  */
	grub_util_error ("%s",
			 /* TRANSLATORS: canonical pathname is the
			    complete one e.g. /etc/fstab. It has
			    to contain `/' normally, if it doesn't
			    we're in trouble and throw this error.  */
			 _("no `/' in canonical filename"));
      if (p != buf)
	*p = 0;
      else
	*++p = 0;

      if (stat (buf, &st) < 0)
	grub_util_error (_("cannot stat `%s': %s"), buf, strerror (errno));

      /* buf is another filesystem; we found it.  */
      if (st.st_dev != num)
	{
	  /* offset == 0 means path given is the mount point.
	     This works around special-casing of "/" in Un*x.  This function never
	     prints trailing slashes (so that its output can be appended a slash
	     unconditionally).  Each slash in is considered a preceding slash, and
	     therefore the root directory is an empty string.  */
	  if (offset == 0)
	    {
	      free (buf);
	      free (buf2);
	      if (poolfs)
		return xasprintf ("/%s/@", poolfs);
	      return xstrdup ("");
	    }
	  else
	    break;
	}

      offset = p - buf;
      /* offset == 1 means root directory.  */
      if (offset == 1)
	{
	  /* Include leading slash.  */
	  offset = 0;
	  break;
	}
    }
  free (buf);
  buf3 = xstrdup (buf2 + offset);
  buf2[offset] = 0;
  
  free (buf2);

  /* Remove trailing slashes, return empty string if root directory.  */
  len = strlen (buf3);
  while (len > 0 && buf3[len - 1] == '/')
    {
      buf3[len - 1] = '\0';
      len--;
    }

  if (poolfs)
    {
      ret = xasprintf ("/%s/@%s", poolfs, buf3);
      free (buf3);
    }
  else
    ret = buf3;

  return ret;
}
