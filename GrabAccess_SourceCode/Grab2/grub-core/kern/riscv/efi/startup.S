/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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

#include <grub/symbol.h>

#if __riscv_xlen == 64
#define sl		sd
#define ll		ld
#else
#define sl		sw
#define ll		lw
#endif


	.file 	"startup.S"
	.text
FUNCTION(_start)
	/*
	 *  EFI_SYSTEM_TABLE and EFI_HANDLE are passed in a1/a0.
	 */

	ll	a2, efi_image_handle_val
	sl	a0, 0(a2)
	ll	a2, efi_system_table_val
	sl	a1, 0(a2)
	ll	a2, grub_main_val
	jr	a2
grub_main_val:
	.quad	EXT_C(grub_main)
efi_system_table_val:
	.quad	EXT_C(grub_efi_system_table)
efi_image_handle_val:
	.quad	EXT_C(grub_efi_image_handle)
