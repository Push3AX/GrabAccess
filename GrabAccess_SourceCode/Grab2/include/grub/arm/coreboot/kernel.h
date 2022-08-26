/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#ifndef GRUB_KERNEL_MACHINE_HEADER
#define GRUB_KERNEL_MACHINE_HEADER	1

#ifndef ASM_FILE

#include <grub/symbol.h>
#include <grub/types.h>

struct grub_fdt_board
{
  const char *vendor, *part;
  const grub_uint8_t *dtb;
  grub_size_t dtb_size;
};

extern struct grub_fdt_board grub_fdt_boards[];
void grub_machine_timer_init (void);
void grub_pl050_init (void);
void grub_cros_init (void);
void grub_rk3288_spi_init (void);
extern grub_addr_t EXPORT_VAR (start_of_ram);
#endif /* ! ASM_FILE */

#define GRUB_KERNEL_MACHINE_STACK_SIZE GRUB_KERNEL_ARM_STACK_SIZE

#endif /* ! GRUB_KERNEL_MACHINE_HEADER */
