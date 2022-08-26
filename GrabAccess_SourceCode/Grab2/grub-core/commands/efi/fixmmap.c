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
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/command.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define ADD_MEMORY_DESCRIPTOR(desc, size)    \
  ((grub_efi_memory_descriptor_t *) ((char *) (desc) + (size)))

static grub_err_t
grub_cmd_fixmmap (grub_command_t cmd __attribute__ ((unused)),
                  int argc __attribute__ ((unused)),
                  char **args __attribute__ ((unused)))
{
  grub_efi_uintn_t map_size;
  grub_efi_memory_descriptor_t *memory_map;
  grub_efi_memory_descriptor_t *memory_map_end;
  grub_efi_memory_descriptor_t *desc;
  grub_efi_uintn_t desc_size;

  map_size = 0;
  if (grub_efi_get_memory_map (&map_size, NULL, NULL, &desc_size, 0) < 0)
    return 0;

  memory_map = grub_malloc (map_size);
  if (memory_map == NULL)
    return grub_errno;
  if (grub_efi_get_memory_map (&map_size, memory_map, NULL, &desc_size, 0) <= 0)
    goto fail;

  memory_map_end = ADD_MEMORY_DESCRIPTOR (memory_map, map_size);
  for (desc = memory_map;
       desc < memory_map_end;
       desc = ADD_MEMORY_DESCRIPTOR (desc, desc_size))
  {
    grub_efi_physical_address_t start = desc->physical_start;
    grub_efi_uint64_t num = desc->num_pages;
    grub_efi_uint64_t size = num << 12;
    grub_efi_physical_address_t end = start + size - 1;
    if (desc->type != GRUB_EFI_CONVENTIONAL_MEMORY)
      continue;
    grub_printf ("%016" PRIxGRUB_UINT64_T "-%016" PRIxGRUB_UINT64_T
                 " %08" PRIxGRUB_UINT64_T " %s\n", start, end, num,
                 grub_get_human_size (size, GRUB_HUMAN_SIZE_SHORT));
    if (num > 0x6400) /* 100 MiB */
      continue;
    if (grub_efi_allocate_pages_real (start, num,
            GRUB_EFI_ALLOCATE_ADDRESS, GRUB_EFI_BOOT_SERVICES_DATA))
      grub_printf ("EFI_BOOT_SERVICES.AllocatePages OK.\n");
    else
      grub_printf ("EFI_BOOT_SERVICES.AllocatePages FAILED.\n");
  }

fail:
  grub_free (memory_map);
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(fixmmap)
{
  cmd = grub_register_command ("fixmmap", grub_cmd_fixmmap,
                               "", "Fix EFI memory map.");
}

GRUB_MOD_FINI(fixmmap)
{
  grub_unregister_command (cmd);
}
