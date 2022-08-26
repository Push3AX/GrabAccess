/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2014  Free Software Foundation, Inc.
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
#ifndef GRUB_EFI_LINUX_HEADER
#define GRUB_EFI_LINUX_HEADER	1

#include <grub/efi/api.h>
#include <grub/err.h>
#include <grub/symbol.h>

grub_err_t
EXPORT_FUNC(grub_efi_linux_boot) (void *kernel_address, grub_off_t offset,
				  void *kernel_param);

#endif /* ! GRUB_EFI_LINUX_HEADER */
