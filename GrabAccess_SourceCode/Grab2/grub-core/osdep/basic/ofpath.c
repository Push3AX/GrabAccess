/* ofpath.c - calculate OpenFirmware path names given an OS device */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009, 2011,2012, 2013  Free Software Foundation, Inc.
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

#include <grub/util/ofpath.h>
#include <grub/mm.h>

char *
grub_util_devname_to_ofpath (const char *sys_devname __attribute__ ((unused)))
{
  return NULL;
}


