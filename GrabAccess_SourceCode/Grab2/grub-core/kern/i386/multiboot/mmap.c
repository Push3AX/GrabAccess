/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/machine/kernel.h>
#include <grub/multiboot.h>
#include <grub/err.h>
#include <grub/misc.h>

void
grub_machine_mmap_init (void)
{
  if ((grub_multiboot_info->flags & MULTIBOOT_INFO_MEM_MAP) == 0)
    grub_fatal ("Missing Multiboot memory information");
}

grub_err_t
grub_machine_mmap_iterate (grub_memory_hook_t hook, void *hook_data)
{
  struct multiboot_mmap_entry *entry = (void *) grub_multiboot_info->mmap_addr;

  while ((unsigned long) entry < grub_multiboot_info->mmap_addr
                                 + grub_multiboot_info->mmap_length)
  {
    if (hook (entry->addr, entry->len, entry->type, hook_data))
      break;

    entry = (void *) ((grub_addr_t) entry + entry->size + sizeof (entry->size));
  }

  return 0;
}
