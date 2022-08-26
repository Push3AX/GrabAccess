/* grub-probe.c - probe device information for a given path */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010,2013  Free Software Foundation, Inc.
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

#include <config.h>
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/diskfilter.h>
#include <grub/i18n.h>
#include <grub/emu/misc.h>
#include <grub/util/ofpath.h>
#include <grub/crypto.h>
#include <grub/cryptodisk.h>

#include <string.h>

/* Since OF path names can have "," characters in them, and GRUB
   internally uses "," to indicate partitions (unlike OF which uses
   ":" for this purpose) we escape such commas.  */
static char *
escape_of_path (const char *orig_path)
{
  char *new_path, *d, c;
  const char *p;

  if (!strchr (orig_path, ','))
    return (char *) xstrdup (orig_path);

  new_path = xmalloc (strlen (orig_path) * 2 + 1);

  p = orig_path;
  d = new_path;
  while ((c = *p++) != '\0')
    {
      if (c == ',')
	*d++ = '\\';
      *d++ = c;
    }
  *d = 0;

  return new_path;
}

char *
grub_util_guess_bios_drive (const char *orig_path)
{
  char *canon;
  char *ptr;
  canon = grub_canonicalize_file_name (orig_path);
  if (!canon)
    return NULL;
  ptr = strrchr (orig_path, '/');
  if (ptr)
    ptr++;
  else
    ptr = canon;
  if ((ptr[0] == 's' || ptr[0] == 'h') && ptr[1] == 'd')
    {
      int num = ptr[2] - 'a';
      free (canon);
      return xasprintf ("hd%d", num);
    }
  if (ptr[0] == 'f' && ptr[1] == 'd')
    {
      int num = atoi (ptr + 2);
      free (canon);
      return xasprintf ("fd%d", num);
    }
  free (canon);
  return NULL;
}

char *
grub_util_guess_efi_drive (const char *orig_path)
{
  char *canon;
  char *ptr;
  canon = grub_canonicalize_file_name (orig_path);
  if (!canon)
    return NULL;
  ptr = strrchr (orig_path, '/');
  if (ptr)
    ptr++;
  else
    ptr = canon;
  if ((ptr[0] == 's' || ptr[0] == 'h') && ptr[1] == 'd')
    {
      int num = ptr[2] - 'a';
      free (canon);
      return xasprintf ("hd%d", num);
    }
  if (ptr[0] == 'f' && ptr[1] == 'd')
    {
      int num = atoi (ptr + 2);
      free (canon);
      return xasprintf ("fd%d", num);
    }
  free (canon);
  return NULL;
}

char *
grub_util_guess_baremetal_drive (const char *orig_path)
{
  char *canon;
  char *ptr;
  canon = grub_canonicalize_file_name (orig_path);
  if (!canon)
    return NULL;
  ptr = strrchr (orig_path, '/');
  if (ptr)
    ptr++;
  else
    ptr = canon;
  if (ptr[0] == 'h' && ptr[1] == 'd')
    {
      int num = ptr[2] - 'a';
      free (canon);
      return xasprintf ("ata%d", num);
    }
  if (ptr[0] == 's' && ptr[1] == 'd')
    {
      int num = ptr[2] - 'a';
      free (canon);
      return xasprintf ("ahci%d", num);
    }
  free (canon);
  return NULL;
}

void
grub_util_fprint_full_disk_name (FILE *f,
				 const char *drive, grub_device_t dev)
{
  char *dname = escape_of_path (drive);
  if (dev->disk->partition)
    {
      char *pname = grub_partition_get_name (dev->disk->partition);
      fprintf (f, "%s,%s", dname, pname);
      free (pname);
    }
  else
    fprintf (f, "%s", dname);
  free (dname);
} 
