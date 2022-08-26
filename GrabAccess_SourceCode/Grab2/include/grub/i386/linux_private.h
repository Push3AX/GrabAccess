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

#ifndef GRUB_I386_LINUX_PRIVATE_HEADER
#define GRUB_I386_LINUX_PRIVATE_HEADER	1

/* Context for grub_linux_boot.  */
struct grub_linux_boot_ctx
{
  grub_addr_t real_mode_target;
  grub_size_t real_size;
  struct linux_kernel_params *params;
  int e820_num;
};

static inline grub_size_t
page_align (grub_size_t size)
{
  return (size + (1 << 12) - 1) & (~((1 << 12) - 1));
}

grub_size_t find_mmap_size (void);

grub_err_t grub_linux_setup_video (struct linux_kernel_params *params);

grub_err_t
grub_e820_add_region (struct grub_e820_mmap *e820_map, int *e820_num,
                      grub_uint64_t start, grub_uint64_t size,
                      grub_uint32_t type);

/* GRUB types conveniently match E820 types.  */
int
grub_linux_boot_mmap_fill (grub_uint64_t addr, grub_uint64_t size,
			   grub_memory_type_t type, void *data);

#endif /* ! GRUB_I386_LINUX_PRIVATE_HEADER */