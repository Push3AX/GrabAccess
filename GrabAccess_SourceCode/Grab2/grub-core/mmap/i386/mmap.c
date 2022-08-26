/* Mmap management. */
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

#include <grub/machine/memory.h>
#include <grub/i386/memory.h>
#include <grub/memory.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>


#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE

/* Context for grub_mmap_malign_and_register.  */
struct grub_mmap_malign_and_register_ctx
{
  grub_uint64_t align, size, highestlow;
};

/* Helper for grub_mmap_malign_and_register.  */
static int
find_hook (grub_uint64_t start, grub_uint64_t rangesize,
	   grub_memory_type_t memtype, void *data)
{
  struct grub_mmap_malign_and_register_ctx *ctx = data;
  grub_uint64_t end = start + rangesize;

  if (memtype != GRUB_MEMORY_AVAILABLE)
    return 0;
  if (end > 0x100000)
    end = 0x100000;
  if (end > start + ctx->size
      && ctx->highestlow < ((end - ctx->size)
			    - ((end - ctx->size) & (ctx->align - 1))))
    ctx->highestlow = (end - ctx->size)
		      - ((end - ctx->size) & (ctx->align - 1));
  return 0;
}

void *
grub_mmap_malign_and_register (grub_uint64_t align, grub_uint64_t size,
			       int *handle, int type, int flags)
{
  struct grub_mmap_malign_and_register_ctx ctx = {
    .align = align,
    .size = size,
    .highestlow = 0
  };

  void *ret;
  if (flags & GRUB_MMAP_MALLOC_LOW)
    {
      /* FIXME: use low-memory mm allocation once it's available. */
      grub_mmap_iterate (find_hook, &ctx);
      ret = (void *) (grub_addr_t) ctx.highestlow;
    }
  else
    ret = grub_memalign (align, size);

  if (! ret)
    {
      *handle = 0;
      return 0;
    }

  *handle = grub_mmap_register ((grub_addr_t) ret, size, type);
  if (! *handle)
    {
      grub_free (ret);
      return 0;
    }

  return ret;
}

void
grub_mmap_free_and_unregister (int handle)
{
  struct grub_mmap_region *cur;
  grub_uint64_t addr;

  for (cur = grub_mmap_overlays; cur; cur = cur->next)
    if (cur->handle == handle)
      break;

  if (! cur)
    return;

  addr = cur->start;

  grub_mmap_unregister (handle);

  if (addr >= 0x100000)
    grub_free ((void *) (grub_addr_t) addr);
}

#endif
