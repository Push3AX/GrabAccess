/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2021  Free Software Foundation, Inc.
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
 *
 *  Fix for glibc 2.25 which is deprecating the namespace pollution of
 *  sys/types.h injecting major(), minor(), and makedev() into the
 *  compilation environment.
 */

#ifndef GRUB_OSDEP_MAJOR_H
#define GRUB_OSDEP_MAJOR_H	1

#include <sys/types.h>

#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#elif defined (MAJOR_IN_SYSMACROS)
# include <sys/sysmacros.h>
#endif
#endif /* GRUB_OSDEP_MAJOR_H */
