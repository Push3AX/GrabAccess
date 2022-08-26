/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012  Free Software Foundation, Inc.
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
#include <grub/efi/edid.h>
#include <grub/efi/pci.h>
#include <grub/efi/efi.h>
#include <grub/efi/uga_draw.h>
#include <grub/efi/graphics_output.h>
#include <grub/efi/console_control.h>
#include <grub/command.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct known_protocol
{
  grub_efi_guid_t guid;
  const char *name;
} known_protocols[] = 
  {
    { GRUB_EFI_DISK_IO_GUID, "disk" },
    { GRUB_EFI_BLOCK_IO_GUID, "block" },
    { GRUB_EFI_SERIAL_IO_GUID, "serial" },
    { GRUB_EFI_SIMPLE_NETWORK_GUID, "network" },
    { GRUB_EFI_PXE_GUID, "pxe" },
    { GRUB_EFI_DEVICE_PATH_GUID, "device path" },
    { GRUB_EFI_PCI_IO_GUID, "PCI" },
    { GRUB_EFI_PCI_ROOT_IO_GUID, "PCI root" },
    { GRUB_EFI_EDID_ACTIVE_GUID, "active EDID" },
    { GRUB_EFI_EDID_DISCOVERED_GUID, "discovered EDID" },
    { GRUB_EFI_EDID_OVERRIDE_GUID, "override EDID" },
    { GRUB_EFI_GOP_GUID, "GOP" },
    { GRUB_EFI_UGA_DRAW_GUID, "UGA draw" },
    { GRUB_EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID, "simple text output" },
    { GRUB_EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID, "simple text input" },
    { GRUB_EFI_SIMPLE_POINTER_PROTOCOL_GUID, "simple pointer" },
    { GRUB_EFI_CONSOLE_CONTROL_GUID, "console control" },
    { GRUB_EFI_ABSOLUTE_POINTER_PROTOCOL_GUID, "absolute pointer" },
    { GRUB_EFI_DRIVER_BINDING_PROTOCOL_GUID, "EFI driver binding" },
    { GRUB_EFI_LOAD_FILE_PROTOCOL_GUID, "load file" },
    { GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, "simple FS" },
    { GRUB_EFI_TAPE_IO_PROTOCOL_GUID, "tape I/O" },
    { GRUB_EFI_UNICODE_COLLATION_PROTOCOL_GUID, "unicode collation" },
    { GRUB_EFI_SCSI_IO_PROTOCOL_GUID, "SCSI I/O" },
    { GRUB_EFI_USB2_HC_PROTOCOL_GUID, "USB host" },
    { GRUB_EFI_DEBUG_SUPPORT_PROTOCOL_GUID, "debug support" },
    { GRUB_EFI_DEBUGPORT_PROTOCOL_GUID, "debug port" },
    { GRUB_EFI_DECOMPRESS_PROTOCOL_GUID, "decompress" },
    { GRUB_EFI_LOADED_IMAGE_PROTOCOL_GUID, "loaded image" },
    { GRUB_EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID, "device path to text" },
    { GRUB_EFI_DEVICE_PATH_UTILITIES_PROTOCOL_GUID, "device path utilities" },
    { GRUB_EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL_GUID, "device path from text" },
    { GRUB_EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID, "HII config routing" },
    { GRUB_EFI_HII_DATABASE_PROTOCOL_GUID, "HII database" },
    { GRUB_EFI_HII_STRING_PROTOCOL_GUID, "HII string" },
    { GRUB_EFI_HII_IMAGE_PROTOCOL_GUID, "HII image" },
    { GRUB_EFI_HII_FONT_PROTOCOL_GUID, "HII font" },
    { GRUB_EFI_COMPONENT_NAME2_PROTOCOL_GUID, "component name 2" },
    { GRUB_EFI_HII_CONFIGURATION_ACCESS_PROTOCOL_GUID,
      "HII configuration access" },
    { GRUB_EFI_USB_IO_PROTOCOL_GUID, "USB I/O" },
  };

static grub_err_t
grub_cmd_lsefi (grub_command_t cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char **args __attribute__ ((unused)))
{
  grub_efi_handle_t *handles;
  grub_efi_uintn_t num_handles;
  unsigned i, j, k;

  handles = grub_efi_locate_handle (GRUB_EFI_ALL_HANDLES,
				    NULL, NULL, &num_handles);

  for (i = 0; i < num_handles; i++)
    {
      grub_efi_handle_t handle = handles[i];
      grub_efi_status_t status;
      grub_efi_uintn_t num_protocols;
      grub_efi_packed_guid_t **protocols;
      grub_efi_device_path_t *dp;

      grub_printf ("Handle %p\n", handle);

      dp = grub_efi_get_device_path (handle);
      if (dp)
	{
	  grub_printf ("  ");
	  grub_efi_print_device_path (dp);
	}

      status = efi_call_3 (grub_efi_system_table->boot_services->protocols_per_handle,
			   handle, &protocols, &num_protocols);
      if (status != GRUB_EFI_SUCCESS) {
	grub_printf ("Unable to retrieve protocols\n");
	continue;
      }
      for (j = 0; j < num_protocols; j++)
	{
	  for (k = 0; k < ARRAY_SIZE (known_protocols); k++)
	    if (grub_memcmp (protocols[j], &known_protocols[k].guid,
			     sizeof (known_protocols[k].guid)) == 0)
		break;
	  if (k < ARRAY_SIZE (known_protocols))
	    grub_printf ("  %s\n", known_protocols[k].name);
	  else
	    grub_printf ("  %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
			 protocols[j]->data1,
			 protocols[j]->data2,
			 protocols[j]->data3,
			 (unsigned) protocols[j]->data4[0],
			 (unsigned) protocols[j]->data4[1],
			 (unsigned) protocols[j]->data4[2],
			 (unsigned) protocols[j]->data4[3],
			 (unsigned) protocols[j]->data4[4],
			 (unsigned) protocols[j]->data4[5],
			 (unsigned) protocols[j]->data4[6],
			 (unsigned) protocols[j]->data4[7]);
	}

    }

  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(lsefi)
{
  cmd = grub_register_command ("lsefi", grub_cmd_lsefi,
			       NULL, "Display EFI handles.");
}

GRUB_MOD_FINI(lsefi)
{
  grub_unregister_command (cmd);
}
