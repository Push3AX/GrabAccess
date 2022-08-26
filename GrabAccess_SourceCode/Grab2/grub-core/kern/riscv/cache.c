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

#include <grub/cache.h>
#include <grub/misc.h>

static grub_int64_t dlinesz;
static grub_int64_t ilinesz;

/* Prototypes for asm functions. */
void grub_arch_clean_dcache_range (grub_addr_t beg, grub_addr_t end,
				   grub_size_t line_size);
void grub_arch_invalidate_icache_range (grub_addr_t beg, grub_addr_t end,
					grub_size_t line_size);

static void
probe_caches (void)
{
  /* TODO */
  dlinesz = 32;
  ilinesz = 32;
}

void
grub_arch_sync_caches (void *address, grub_size_t len)
{
  grub_size_t start, end, max_align;

  if (dlinesz == 0)
    probe_caches();
  if (dlinesz == 0)
    grub_fatal ("Unknown cache line size!");

  max_align = dlinesz > ilinesz ? dlinesz : ilinesz;

  start = ALIGN_DOWN ((grub_size_t) address, max_align);
  end = ALIGN_UP ((grub_size_t) address + len, max_align);

  grub_arch_clean_dcache_range (start, end, dlinesz);
  grub_arch_invalidate_icache_range (start, end, ilinesz);
}

void
grub_arch_sync_dma_caches (volatile void *address __attribute__((unused)),
			   grub_size_t len __attribute__((unused)))
{
  /* DMA incoherent devices not supported yet */
}
