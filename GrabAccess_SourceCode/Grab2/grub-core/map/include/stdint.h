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

#ifndef _STDINT_H
#define _STDINT_H

#include <grub/types.h>

typedef grub_uint8_t uint8_t;
typedef grub_uint16_t uint16_t;
typedef grub_uint32_t uint32_t;
typedef grub_uint64_t uint64_t;

typedef grub_int8_t int8_t;
typedef grub_int16_t int16_t;
typedef grub_int32_t int32_t;
typedef grub_int64_t int64_t;

typedef unsigned long intptr_t;

typedef grub_size_t size_t;
typedef grub_ssize_t ssize_t;

typedef __WCHAR_TYPE__ wchar_t;
typedef __WINT_TYPE__ wint_t;

#endif /* _STDINT_H */
