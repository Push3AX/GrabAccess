 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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

#ifndef GRUB_MAPLIB_GUID_H
#define GRUB_MAPLIB_GUID_H

#include <grub/types.h>

void grub_guidgen (grub_packed_guid_t *guid);

int
grub_guidcmp (const grub_packed_guid_t *g1, const grub_packed_guid_t *g2);

grub_packed_guid_t *
grub_guidcpy (grub_packed_guid_t *dst, const grub_packed_guid_t *src);

#endif
