/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#ifndef GRUB_MEMORY_MACHINE_HEADER
#define GRUB_MEMORY_MACHINE_HEADER	1

#include <grub/err.h>
#include <grub/types.h>

/* The term "page" in UEFI refers only to a 4 KiB-aligned 4 KiB size region of
   memory. It is not concerned with underlying translation management concepts,
   but only used as the granule for memory allocations. */
#define GRUB_EFI_PAGE_SHIFT             12
#define GRUB_EFI_PAGE_SIZE              (1 << GRUB_EFI_PAGE_SHIFT)
#define GRUB_EFI_BYTES_TO_PAGES(bytes)  (((bytes) + 0xfff) >> GRUB_EFI_PAGE_SHIFT)

#define GRUB_MMAP_REGISTER_BY_FIRMWARE  1

grub_err_t grub_machine_mmap_register (grub_uint64_t start, grub_uint64_t size,
				       int type, int handle);
grub_err_t grub_machine_mmap_unregister (int handle);

#endif /* ! GRUB_MEMORY_MACHINE_HEADER */
