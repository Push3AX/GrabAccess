/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/i386/multiboot/kernel.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/err.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct multiboot_info mbi;

static grub_err_t
grub_cmd_mbinfo (grub_command_t cmd __attribute__ ((unused)),
                 int argc __attribute__ ((unused)),
                 char *argv[] __attribute__ ((unused)))
{
  grub_printf ("flags: 0x%x\n", (unsigned)mbi.flags);

  if (mbi.flags & MULTIBOOT_INFO_MEMORY)
    grub_printf ("mem_lower: %u\nmem_upper: %u\n",
                 (unsigned)mbi.mem_lower, (unsigned)mbi.mem_upper);

  if (mbi.flags & MULTIBOOT_INFO_CMDLINE)
    grub_printf ("cmdline: %s\n", (char *)mbi.cmdline);

  if (mbi.flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)
    grub_printf ("bootloader: %s\n", (char *)mbi.boot_loader_name);

  if (mbi.flags & MULTIBOOT_INFO_MODS)
  {
    multiboot_module_t *mod = (void *) (grub_addr_t) mbi.mods_addr;
    unsigned i;
    grub_printf ("mods_count: %u\nmods_addr: %p\n", mbi.mods_count, mod);
    for (i = 0; i < mbi.mods_count; i++, mod++)
    {
      grub_printf ("[%u] 0x%08x - 0x%08x %s\n", i + 1,
                   mod->mod_start, mod->mod_end, (char *) mod->cmdline);
    }
  }
  if (mbi.flags & MULTIBOOT_INFO_MEM_MAP)
  {
    multiboot_memory_map_t *mmap = (void *) (grub_addr_t) mbi.mmap_addr;

    grub_printf("mmap_addr: %p\nmmap_length: %u\n", mmap, mbi.mmap_length);
    for (; (grub_addr_t) mmap < mbi.mmap_addr + mbi.mmap_length;
         mmap = (multiboot_memory_map_t *)
                ((grub_addr_t) mmap + mmap->size + sizeof(mmap->size)))
    {
      grub_printf (
          "size: %u, addr: 0x%llx, length: 0x%llx, type: %u\n",
          mmap->size, (unsigned long long) mmap->addr,
          (unsigned long long) mmap->len, mmap->type);
    }
  }

  return GRUB_ERR_NONE;
}

static grub_command_t cmd_mbi;

GRUB_MOD_INIT(mbinfo)
{
  if (grub_multiboot_info)
  {
    grub_memmove (&mbi, grub_multiboot_info, sizeof (struct multiboot_info));
    cmd_mbi = grub_register_command ("mbinfo", grub_cmd_mbinfo, 0,
                                     N_("Display Multiboot info."));
  }
}

GRUB_MOD_FINI(mbinfo)
{
  if (grub_multiboot_info)
  {
    grub_unregister_command (cmd_mbi);
  }
}
