 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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
 */

#ifndef _STDDEF_H
#define _STDDEF_H

#include <stdint.h>

#define __unused __attribute__ (( unused ))

#define L( x ) _L ( x )
#define _L( x ) L ## x

#ifndef NULL
#define NULL ((void *) 0)
#endif

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *) NULL)->MEMBER)

#endif /* _STDDEF_H */
