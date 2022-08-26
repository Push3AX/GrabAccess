/* loadbios.c - command to load a bios dump  */
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/efi/efi.h>
#include <grub/pci.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_efi_guid_t acpi_guid = GRUB_EFI_ACPI_TABLE_GUID;
static grub_efi_guid_t acpi2_guid = GRUB_EFI_ACPI_20_TABLE_GUID;
static grub_efi_guid_t smbios_guid = GRUB_EFI_SMBIOS_TABLE_GUID;
static grub_efi_guid_t smbios3_guid = GRUB_EFI_SMBIOS3_TABLE_GUID;

#define EBDA_SEG_ADDR	0x40e
#define LOW_MEM_ADDR	0x413
#define FAKE_EBDA_SEG	0x9fc0

#define BLANK_MEM	0xffffffff
#define VBIOS_ADDR	0xc0000
#define SBIOS_ADDR	0xf0000

void
grub_efi_unlock_rom_area (void)
{
  grub_pci_address_t addr;
  grub_pci_device_t dev = { .bus = 0, .device = 0, .function = 0};
  /* FIXME: should be macroified.  */
  addr = grub_pci_make_address (dev, 144);
  grub_pci_write_byte (addr++, 0x30);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr++, 0x33);
  grub_pci_write_byte (addr, 0);
}

static int
grub_efi_enable_rom_area (void)
{
  grub_uint32_t *rom_ptr;

  rom_ptr = (grub_uint32_t *) VBIOS_ADDR;
  if (*rom_ptr != BLANK_MEM)
    {
      grub_puts_ (N_("ROM image is present."));
      return 0;
    }

  grub_efi_unlock_rom_area ();

  *rom_ptr = 0;
  if (*rom_ptr != 0)
    {
      grub_puts_ (N_("Can\'t enable ROM area."));
      return 0;
    }

  return 1;
}

void
grub_efi_lock_rom_area (void)
{
  grub_pci_address_t addr;
  grub_pci_device_t dev = { .bus = 0, .device = 0, .function = 0};

  /* FIXME: should be macroified.  */
  addr = grub_pci_make_address (dev, 144);
  grub_pci_write_byte (addr++, 0x10);
  grub_pci_write_byte (addr++, 0x11);
  grub_pci_write_byte (addr++, 0x11);
  grub_pci_write_byte (addr++, 0x11);
  grub_pci_write_byte (addr, 0x11);
}

void
grub_efi_fake_bios_data (int use_rom)
{
  unsigned i;
  void *acpi, *smbios, *smbios3;
  grub_uint16_t *ebda_seg_ptr, *low_mem_ptr;

  ebda_seg_ptr = (grub_uint16_t *) EBDA_SEG_ADDR;
  low_mem_ptr = (grub_uint16_t *) LOW_MEM_ADDR;
  if ((*ebda_seg_ptr) || (*low_mem_ptr))
    return;

  acpi = 0;
  smbios = 0;
  smbios3 = 0;
  for (i = 0; i < grub_efi_system_table->num_table_entries; i++)
    {
      grub_efi_packed_guid_t *guid =
	&grub_efi_system_table->configuration_table[i].vendor_guid;

      if (! grub_memcmp (guid, &acpi2_guid, sizeof (grub_efi_guid_t)))
	{
	  acpi = grub_efi_system_table->configuration_table[i].vendor_table;
	  grub_dprintf ("efi", "ACPI2: %p\n", acpi);
	}
      else if (! grub_memcmp (guid, &acpi_guid, sizeof (grub_efi_guid_t)))
	{
	  void *t;

	  t = grub_efi_system_table->configuration_table[i].vendor_table;
	  if (! acpi)
	    acpi = t;
	  grub_dprintf ("efi", "ACPI: %p\n", t);
	}
      else if (! grub_memcmp (guid, &smbios_guid, sizeof (grub_efi_guid_t)))
	{
	  smbios = grub_efi_system_table->configuration_table[i].vendor_table;
	  grub_dprintf ("efi", "SMBIOS: %p\n", smbios);
	}
      else if (! grub_memcmp (guid, &smbios3_guid, sizeof (grub_efi_guid_t)))
	{
	  smbios3 = grub_efi_system_table->configuration_table[i].vendor_table;
	  grub_dprintf ("efi", "SMBIOS3: %p\n", smbios3);
	}
    }

  *ebda_seg_ptr = FAKE_EBDA_SEG;
  *low_mem_ptr = (FAKE_EBDA_SEG >> 6);

  *((grub_uint16_t *) (FAKE_EBDA_SEG << 4)) = 640 - *low_mem_ptr;

  if (acpi)
    grub_memcpy ((char *) ((FAKE_EBDA_SEG << 4) + 16), acpi, 1024 - 16);

  if (use_rom)
    {
      if (smbios)
	grub_memcpy ((char *) SBIOS_ADDR, (char *) smbios, 31);
      if (smbios3)
	grub_memcpy ((char *) SBIOS_ADDR + 32, (char *) smbios3, 24);
    }
}

static grub_err_t
grub_cmd_fakebios (struct grub_command *cmd __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char *argv[] __attribute__ ((unused)))
{
  if (grub_efi_enable_rom_area ())
    {
      grub_efi_fake_bios_data (1);
      grub_efi_lock_rom_area ();
    }
  else
    grub_efi_fake_bios_data (0);

  return 0;
}

static grub_err_t
grub_cmd_loadbios (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char *argv[])
{
  grub_file_t file;
  int size;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (argc > 1)
    {
      file = grub_file_open (argv[1], GRUB_FILE_TYPE_VBE_DUMP);
      if (! file)
	return grub_errno;

      if (file->size != 4)
	grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid int10 dump size");
      else
	grub_file_read (file, (void *) 0x40, 4);

      grub_file_close (file);
      if (grub_errno)
	return grub_errno;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_VBE_DUMP);
  if (! file)
    return grub_errno;

  size = file->size;
  if ((size < 0x10000) || (size > 0x40000))
    grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid bios dump size");
  else if (grub_efi_enable_rom_area ())
    {
      grub_file_read (file, (void *) VBIOS_ADDR, size);
      grub_efi_fake_bios_data (size <= 0x40000);
      grub_efi_lock_rom_area ();
    }

  grub_file_close (file);
  return grub_errno;
}

static grub_command_t cmd_fakebios, cmd_loadbios;

GRUB_MOD_INIT(loadbios)
{
  cmd_fakebios = grub_register_command ("fakebios", grub_cmd_fakebios,
					0, N_("Create BIOS-like structures for"
					      " backward compatibility with"
					      " existing OS."));

  cmd_loadbios = grub_register_command ("loadbios", grub_cmd_loadbios,
					N_("BIOS_DUMP [INT10_DUMP]"),
					N_("Load BIOS dump."));
}

GRUB_MOD_FINI(loadbios)
{
  grub_unregister_command (cmd_fakebios);
  grub_unregister_command (cmd_loadbios);
}
