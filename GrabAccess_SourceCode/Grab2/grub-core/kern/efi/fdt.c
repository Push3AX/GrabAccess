/* fdt.c - EFI Flattened Device Tree interaction */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007  Free Software Foundation, Inc.
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

#include <grub/efi/efi.h>
#include <grub/mm.h>

void *
grub_efi_get_firmware_fdt (void)
{
  grub_efi_configuration_table_t *tables;
  grub_efi_guid_t fdt_guid = GRUB_EFI_DEVICE_TREE_GUID;
  void *firmware_fdt = NULL;
  unsigned int i;

  /* Look for FDT in UEFI config tables. */
  tables = grub_efi_system_table->configuration_table;

  for (i = 0; i < grub_efi_system_table->num_table_entries; i++)
    if (grub_memcmp (&tables[i].vendor_guid, &fdt_guid, sizeof (fdt_guid)) == 0)
      {
	firmware_fdt = tables[i].vendor_table;
	grub_dprintf ("linux", "found registered FDT @ %p\n", firmware_fdt);
	break;
      }

  return firmware_fdt;
}
