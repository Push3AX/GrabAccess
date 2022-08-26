/* memory.h - describe the memory map */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2007  Free Software Foundation, Inc.
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

#ifndef _GRUB_MEMORY_MACHINE_MB_HEADER
#define _GRUB_MEMORY_MACHINE_MB_HEADER      1

#include <grub/symbol.h>

#ifndef ASM_FILE
#include <grub/err.h>
#include <grub/types.h>
#include <grub/memory.h>
#endif

#include <grub/i386/memory.h>
#include <grub/i386/memory_raw.h>

#define GRUB_MEMORY_MACHINE_BIOS_DATA_AREA_ADDR 0x400

#define GRUB_MEMORY_MACHINE_PART_TABLE_ADDR     0x7be
#define GRUB_MEMORY_MACHINE_BOOT_LOADER_ADDR    0x7c00

#ifndef ASM_FILE

/* See http://heim.ifi.uio.no/~stanisls/helppc/bios_data_area.html for a
   description of the BIOS Data Area layout.  */
struct grub_machine_bios_data_area
{
  grub_uint8_t unused1[0x17];
  grub_uint8_t keyboard_flag_lower; /* 0x17 */
  grub_uint8_t unused2[0xf0 - 0x18];
};

void grub_machine_mmap_init (void);

static inline grub_err_t
grub_machine_mmap_register (grub_uint64_t start __attribute__ ((unused)),
                            grub_uint64_t size __attribute__ ((unused)),
                            int type __attribute__ ((unused)),
                            int handle __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}
static inline grub_err_t
grub_machine_mmap_unregister (int handle  __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

#endif

#endif /* ! _GRUB_MEMORY_MACHINE_HEADER */
