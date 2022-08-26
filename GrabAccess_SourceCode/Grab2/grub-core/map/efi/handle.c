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
 *
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/device.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/loader.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/term.h>
#include <grub/ventoy.h>

#include <guid.h>
#include <misc.h>

static grub_efi_status_t (EFIAPI *orig_locate_handle)
                      (grub_efi_locate_search_type_t search_type,
                       grub_efi_guid_t *protocol,
                       void *search_key,
                       grub_efi_uintn_t *buffer_size,
                       grub_efi_handle_t *buffer) = NULL;

static int
compare_guid (grub_efi_guid_t *a, grub_efi_guid_t *b)
{
  int i;
  if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3)
    return 0;
  for (i = 0; i < 8; i++)
  {
    if (a->data4[i] != b->data4[i])
      return 0;
  }
  return 1;
}

static grub_efi_handle_t saved_handle;

static grub_efi_status_t EFIAPI
locate_handle_wrapper (grub_efi_locate_search_type_t search_type,
                       grub_efi_guid_t *protocol,
                       void *search_key,
                       grub_efi_uintn_t *buffer_size,
                       grub_efi_handle_t *buffer)
{
  grub_efi_uintn_t i;
  grub_efi_handle_t handle = NULL;
  grub_efi_status_t status = GRUB_EFI_SUCCESS;
  grub_efi_guid_t guid = GRUB_EFI_BLOCK_IO_GUID;

  status = efi_call_5 (orig_locate_handle, search_type,
                       protocol, search_key, buffer_size, buffer);

  if (status != GRUB_EFI_SUCCESS || !protocol)
    return status;

  if (!compare_guid (&guid, protocol))
    return status;

  for (i = 0; i < (*buffer_size) / sizeof(grub_efi_handle_t); i++)
  {
    if (buffer[i] == saved_handle)
    {
      handle = buffer[0];
      buffer[0] = buffer[i];
      buffer[i] = handle;
      break;
    }
  }

  return status;
}

void
grub_efi_set_first_disk (grub_efi_handle_t handle)
{
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  if (!orig_locate_handle)
  {
    orig_locate_handle = (void *) b->locate_handle;
    b->locate_handle = (void *) locate_handle_wrapper;
  }
  saved_handle = handle;
}
