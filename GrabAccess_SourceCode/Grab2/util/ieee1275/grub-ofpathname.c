/* grub-ofpathname.c - Find OpenBOOT path for a given device */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2010  Free Software Foundation, Inc.
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

#include <grub/util/misc.h>
#include <grub/util/ofpath.h>

#include <grub/i18n.h>

#include "progname.h"

#include <string.h>

int main(int argc, char **argv)
{
  char *of_path;

  grub_util_host_init (&argc, &argv);

  if (argc != 2 || strcmp (argv[1], "--help") == 0)
    {
      printf(_("Usage: %s DEVICE\n"), program_name);
      return 1;
    }
  if (strcmp (argv[1], "--version") == 0)
    {
      printf ("%s\n", PACKAGE_STRING);
      return 1;
    }

  of_path = grub_util_devname_to_ofpath (argv[1]);

  if (of_path)
    printf ("%s\n", of_path);

  free (of_path);

  return 0;
}
