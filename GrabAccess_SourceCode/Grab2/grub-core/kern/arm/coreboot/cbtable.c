/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2013  Free Software Foundation, Inc.
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

#include <grub/coreboot/lbio.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/arm/startup.h>

GRUB_MOD_LICENSE ("GPLv3+");

#pragma GCC diagnostic ignored "-Wcast-align"

grub_linuxbios_table_header_t
grub_linuxbios_get_tables (void)
{
  grub_linuxbios_table_header_t table_header
    = (grub_linuxbios_table_header_t) grub_arm_saved_registers.r[0];

  if (!grub_linuxbios_check_signature (table_header))
    return 0;

  return table_header;
}
