/* lsefimemmap.c  - Display memory map.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/command.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define ADD_MEMORY_DESCRIPTOR(desc, size)	\
  ((grub_efi_memory_descriptor_t *) ((char *) (desc) + (size)))

static grub_err_t
grub_cmd_lsefimmap (grub_command_t cmd __attribute__ ((unused)),
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

  grub_printf
    ("Type      Physical start  - end             #Pages   "
     "     Size Attributes\n");
  memory_map_end = ADD_MEMORY_DESCRIPTOR (memory_map, map_size);
  for (desc = memory_map;
       desc < memory_map_end;
       desc = ADD_MEMORY_DESCRIPTOR (desc, desc_size))
    {
      grub_efi_uint64_t size;
      grub_efi_uint64_t attr;
      static const char types_str[][9] = 
	{
	  "reserved", 
	  "ldr-code",
	  "ldr-data",
	  "BS-code ",
	  "BS-data ",
	  "RT-code ",
	  "RT-data ",
	  "conv-mem",
	  "unusable",
	  "ACPI-rec",
	  "ACPI-nvs",
	  "MMIO    ",
	  "IO-ports",
	  "PAL-code",
	  "persist ",
	};
      if (desc->type < ARRAY_SIZE (types_str))
	grub_printf ("%s ", types_str[desc->type]);
      else
	grub_printf ("Unk %02x   ", desc->type);
      
      grub_printf (" %016" PRIxGRUB_UINT64_T "-%016" PRIxGRUB_UINT64_T
		   " %08" PRIxGRUB_UINT64_T,
		   desc->physical_start,
		   desc->physical_start + (desc->num_pages << 12) - 1,
		   desc->num_pages);

      size = desc->num_pages << 12;	/* 4 KiB page size */
      /*
       * Since size is a multiple of 4 KiB, no need to handle units
       * of just Bytes (which would use a mask of 0x3ff).
       *
       * 14 characters would support the largest possible number of 4 KiB
       * pages that are not a multiple of larger units (e.g., MiB):
       * 17592186044415 (0xffffff_fffff000), but that uses a lot of
       * whitespace for a rare case.  6 characters usually suffices;
       * columns will be off if not, but this is preferable to rounding.
       */
      if (size & 0xfffff)
	grub_printf (" %6" PRIuGRUB_UINT64_T "KiB", size >> 10);
      else if (size & 0x3fffffff)
	grub_printf (" %6" PRIuGRUB_UINT64_T "MiB", size >> 20);
      else if (size & 0xffffffffff)
	grub_printf (" %6" PRIuGRUB_UINT64_T "GiB", size >> 30);
      else if (size & 0x3ffffffffffff)
	grub_printf (" %6" PRIuGRUB_UINT64_T "TiB", size >> 40);
      else if (size & 0xfffffffffffffff)
	grub_printf (" %6" PRIuGRUB_UINT64_T "PiB", size >> 50);
      else
	grub_printf (" %6" PRIuGRUB_UINT64_T "EiB", size >> 60);

      attr = desc->attribute;
      if (attr & GRUB_EFI_MEMORY_RUNTIME)
	grub_printf (" RT");
      if (attr & GRUB_EFI_MEMORY_UC)
	grub_printf (" UC");
      if (attr & GRUB_EFI_MEMORY_WC)
	grub_printf (" WC");
      if (attr & GRUB_EFI_MEMORY_WT)
	grub_printf (" WT");
      if (attr & GRUB_EFI_MEMORY_WB)
	grub_printf (" WB");
      if (attr & GRUB_EFI_MEMORY_UCE)
	grub_printf (" UCE");
      if (attr & GRUB_EFI_MEMORY_WP)
	grub_printf (" WP");
      if (attr & GRUB_EFI_MEMORY_RP)
	grub_printf (" RP");
      if (attr & GRUB_EFI_MEMORY_XP)
	grub_printf (" XP");
      if (attr & GRUB_EFI_MEMORY_NV)
	grub_printf (" NV");
      if (attr & GRUB_EFI_MEMORY_MORE_RELIABLE)
	grub_printf (" MR");
      if (attr & GRUB_EFI_MEMORY_RO)
	grub_printf (" RO");

      grub_printf ("\n");
    }

 fail:
  grub_free (memory_map);
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(lsefimmap)
{
  cmd = grub_register_command ("lsefimmap", grub_cmd_lsefimmap,
			       "", "Display EFI memory map.");
}

GRUB_MOD_FINI(lsefimmap)
{
  grub_unregister_command (cmd);
}
