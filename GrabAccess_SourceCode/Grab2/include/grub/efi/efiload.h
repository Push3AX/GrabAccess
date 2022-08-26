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
 */

#ifndef GRUB_EFI_EFILOAD_HEADER
#define GRUB_EFI_EFILOAD_HEADER	1

#include <grub/efi/api.h>
#include <grub/types.h>
#include <grub/symbol.h>

grub_err_t grub_efi_driver_load (grub_size_t size, void *boot_image, int connect);
grub_efi_status_t connect_all_efi (void);

#endif /* ! GRUB_EFI_EFILOAD_HEADER */
