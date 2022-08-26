/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

#ifndef GRUB_EFI_SB_H
#define GRUB_EFI_SB_H     1

#include <grub/types.h>
#include <grub/dl.h>

#define GRUB_EFI_SECUREBOOT_MODE_UNSET	0
#define GRUB_EFI_SECUREBOOT_MODE_UNKNOWN	1
#define GRUB_EFI_SECUREBOOT_MODE_DISABLED	2
#define GRUB_EFI_SECUREBOOT_MODE_ENABLED	3

#ifdef GRUB_MACHINE_EFI
extern grub_uint8_t
EXPORT_FUNC (grub_efi_get_secureboot) (void);
#else
static inline grub_uint8_t
grub_efi_get_secureboot (void)
{
  return GRUB_EFI_SECUREBOOT_MODE_UNSET;
}
#endif
#endif /* GRUB_EFI_SB_H */
