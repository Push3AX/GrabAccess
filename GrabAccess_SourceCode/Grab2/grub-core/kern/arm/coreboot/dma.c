/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2009  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/dma.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/mm_private.h>
#include <grub/cache.h>

struct grub_pci_dma_chunk *
grub_memalign_dma32 (grub_size_t align, grub_size_t size)
{
  void *ret;
  if (align < 64)
    align = 64;
  size = ALIGN_UP (size, align);
  ret = grub_memalign (align, size);
  if (!ret)
    return 0;
  grub_arch_sync_dma_caches (ret, size);
  return ret;
}

void
grub_dma_free (struct grub_pci_dma_chunk *ch)
{
  grub_size_t size = (((struct grub_mm_header *) ch) - 1)->size * GRUB_MM_ALIGN;
  grub_arch_sync_dma_caches (ch, size);
  grub_free (ch);
}

volatile void *
grub_dma_get_virt (struct grub_pci_dma_chunk *ch)
{
  return (void *) ch;
}

grub_uint32_t
grub_dma_get_phys (struct grub_pci_dma_chunk *ch)
{
  return (grub_uint32_t) (grub_addr_t) ch;
}

