/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>

#include <string.h>
#include <dos/dos.h>
#include <dos/filesystem.h>
#include <dos/exall.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <devices/trackdisk.h>

char *
grub_make_system_path_relative_to_its_root (const char *path)
{
  char *p;
  unsigned char *tmp;
  char *ret;
  BPTR lck;

  if (path[0] == '/' && path[1] == '/' && path[2] == ':')
    return xstrdup (path);

  tmp = xmalloc (2048);

  lck = Lock ((const unsigned char *) path, SHARED_LOCK);
  if (!lck || !NameFromLock (lck, tmp, 2040))
    {
      free (tmp);
      tmp = (unsigned char *) xstrdup (path);
    }
  if (lck)
    UnLock (lck);
  p = strchr ((char *) tmp, ':');
  if (!p)
    return (char *) tmp;
  if (p[1] == '/' || p[1] == '\0')
    {
      ret = xstrdup (p + 1);
    }
  else
    {
      ret = xmalloc (strlen (p + 1) + 2);
      ret[0] = '/';
      strcpy (ret + 1, p + 1);
    }

  free (tmp);
  return ret;
}
