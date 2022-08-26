/* Compute amount of lower and upper memory till the first hole. */
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

#include <grub/memory.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/cpu/memory.h>

/* Helper for grub_mmap_get_lower.  */
static int
lower_hook (grub_uint64_t addr, grub_uint64_t size, grub_memory_type_t type,
	    void *data)
{
  grub_uint64_t *lower = data;

  if (type != GRUB_MEMORY_AVAILABLE)
    return 0;
  if (addr == 0)
    *lower = size;
  return 0;
}

grub_uint64_t
grub_mmap_get_lower (void)
{
  grub_uint64_t lower = 0;

  grub_mmap_iterate (lower_hook, &lower);
  if (lower > GRUB_ARCH_LOWMEMMAXSIZE)
    lower = GRUB_ARCH_LOWMEMMAXSIZE;
  return lower;
}

/* Helper for grub_mmap_get_upper.  */
static int
upper_hook (grub_uint64_t addr, grub_uint64_t size, grub_memory_type_t type,
	    void *data)
{
  grub_uint64_t *upper = data;

  if (type != GRUB_MEMORY_AVAILABLE)
    return 0;
  if (addr <= GRUB_ARCH_HIGHMEMPSTART && addr + size
      > GRUB_ARCH_HIGHMEMPSTART)
    *upper = addr + size - GRUB_ARCH_HIGHMEMPSTART;
  return 0;
}

grub_uint64_t
grub_mmap_get_upper (void)
{
  grub_uint64_t upper = 0;

  grub_mmap_iterate (upper_hook, &upper);
  return upper;
}
