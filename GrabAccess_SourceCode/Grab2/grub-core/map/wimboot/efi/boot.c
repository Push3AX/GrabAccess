 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
 *
 */

#include <grub/misc.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/efi/graphics_output.h>
#include <grub/script_sh.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <guid.h>
#include <misc.h>
#include <vfat.h>
#include <wimboot.h>

static uint8_t gui;

typedef grub_efi_status_t
(EFIAPI *open_protocol) (grub_efi_handle_t handle,
                         grub_efi_guid_t *protocol,
                         void **protocol_interface,
                         grub_efi_handle_t agent_handle,
                         grub_efi_handle_t controller_handle,
                         grub_efi_uint32_t attributes);

static open_protocol orig_open_protocol;

static grub_efi_status_t EFIAPI
efi_open_protocol_wrapper (grub_efi_handle_t handle,
                           grub_efi_guid_t *protocol,
                           void **interface, grub_efi_handle_t agent_handle,
                           grub_efi_handle_t controller_handle,
                           grub_efi_uint32_t attributes)
{
  static unsigned int count;
  grub_efi_status_t status;
  grub_efi_guid_t gop_guid = GRUB_EFI_GOP_GUID;

  /* Open the protocol */
  status = efi_call_6 (orig_open_protocol, handle, protocol, interface,
                       agent_handle, controller_handle, attributes);
  if (status != GRUB_EFI_SUCCESS)
    return status;

  /* Block first attempt by bootmgfw.efi to open
   * EFI_GRAPHICS_OUTPUT_PROTOCOL.  This forces error messages
   * to be displayed in text mode (thereby avoiding the totally
   * blank error screen if the fonts are missing).  We must
   * allow subsequent attempts to succeed, otherwise the OS will
   * fail to boot.
   */
  if ((memcmp (protocol, &gop_guid, sizeof (*protocol)) == 0) &&
      (count++ == 0) && (! gui))
  {
    printf ("Forcing text mode output\n");
    return GRUB_EFI_INVALID_PARAMETER;
  }
  return GRUB_EFI_SUCCESS;
}

void
grub_wimboot_boot (struct wimboot_cmdline *cmd)
{
  grub_efi_boot_services_t *b;
  b = grub_efi_system_table->boot_services;
  grub_efi_device_path_t *path = NULL;
  grub_efi_physical_address_t phys;
  void *data;
  unsigned int pages;
  grub_efi_handle_t handle;
  grub_efi_status_t status;
  grub_efi_loaded_image_t *loaded = NULL;
  struct vfat_file *file = cmd->bootmgfw;
  char *efi_filename;

  gui = cmd->gui;
  /* Allocate memory */
  pages = ((file->len + PAGE_SIZE - 1) / PAGE_SIZE);
  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
                       GRUB_EFI_BOOT_SERVICES_DATA, pages, &phys);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_pause_fatal ("Could not allocate %d pages\n", pages);
  }
  data = ((void *)(intptr_t) phys);

  /* Read image */
  file->read (file, data, 0, file->len);
  printf ("Read %s\n", file->name);

  /* DevicePath */
  efi_filename = grub_xasprintf ("/efi/boot/%s", file->name);
  path = grub_efi_file_device_path (grub_efi_get_device_path (wimboot_part.handle),
                                    efi_filename);
  grub_free (efi_filename);

  /* Load image */
  status = efi_call_6 (b->load_image, FALSE, grub_efi_image_handle,
                         path, data, file->len, &handle);
  if (path)
    free (path);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_pause_fatal ("Could not load %s\n", file->name);
  }
  printf ("Loaded %s\n", file->name);

  /* Get loaded image protocol */
  loaded = grub_efi_get_loaded_image (handle);
  if (! loaded)
    grub_pause_fatal ("no loaded image available");
  /* Force correct device handle */
  if (loaded->device_handle != wimboot_part.handle)
    loaded->device_handle = wimboot_part.handle;
  /* Intercept calls to OpenProtocol() */
  orig_open_protocol =
          (open_protocol) loaded->system_table->boot_services->open_protocol;
  *(open_protocol *)&loaded->system_table->boot_services->open_protocol =
          efi_open_protocol_wrapper;
  /* Start image */
  if (cmd->pause)
    grub_pause_boot ();
  grub_script_execute_sourcecode ("terminal_output console");
  grub_printf ("Booting VFAT ...\n");
  status = efi_call_3 (b->start_image, handle, NULL, NULL);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_pause_fatal ("Could not start %s\n", file->name);
  }
  grub_pause_fatal ("%s returned\n", file->name);
}
