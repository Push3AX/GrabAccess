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

#include <grub/types.h>

#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/charset.h>
#include <grub/util/windows.h>
#include <windows.h>
#include <winioctl.h>

static size_t
tclen (const TCHAR *s)
{
  const TCHAR *s0 = s;
  while (*s)
      s++;
  return s - s0;
}

char *
grub_make_system_path_relative_to_its_root (const char *path)
{
  TCHAR *dirwindows, *mntpointwindows;
  TCHAR *ptr;
  size_t offset, flen;
  TCHAR *ret;
  char *cret;

  dirwindows = grub_util_get_windows_path (path);
  if (!dirwindows)
    return xstrdup (path);

  mntpointwindows = grub_get_mount_point (dirwindows);

  if (!mntpointwindows)
    {
      offset = 0;
      if (dirwindows[0] && dirwindows[1] == ':')
	offset = 2;
    }
  offset = tclen (mntpointwindows);
  free (mntpointwindows);
  flen = tclen (dirwindows);
  if (offset > flen)
    {
      offset = 0;
      if (dirwindows[0] && dirwindows[1] == ':')
	offset = 2;
    }
  ret = xcalloc (flen - offset + 2, sizeof (ret[0]));
  if (dirwindows[offset] != '\\'
      && dirwindows[offset] != '/'
      && dirwindows[offset])
    {
      ret[0] = '\\';
      memcpy (ret + 1, dirwindows + offset, (flen - offset + 1) * sizeof (ret[0]));
    }
  else
    memcpy (ret, dirwindows + offset, (flen - offset + 1) * sizeof (ret[0]));

  free (dirwindows);

  for (ptr = ret; *ptr; ptr++)
    if (*ptr == '\\')
      *ptr = '/';

  cret = grub_util_tchar_to_utf8 (ret);
  free (ret);

  return cret;
}
