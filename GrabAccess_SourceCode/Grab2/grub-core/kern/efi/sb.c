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
 *  UEFI Secure Boot related checkings.
 */

#include <grub/efi/efi.h>
#include <grub/efi/pe32.h>
#include <grub/efi/sb.h>
#include <grub/err.h>
#include <grub/i386/linux.h>
#include <grub/mm.h>
#include <grub/types.h>

/*
 * Determine whether we're in secure boot mode.
 *
 * Please keep the logic in sync with the Linux kernel,
 * drivers/firmware/efi/libstub/secureboot.c:efi_get_secureboot().
 */
grub_uint8_t
grub_efi_get_secureboot (void)
{
  static grub_efi_guid_t efi_variable_guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  static grub_efi_guid_t efi_shim_lock_guid = GRUB_EFI_SHIM_LOCK_GUID;
  grub_efi_status_t status;
  grub_efi_uint32_t attr = 0;
  grub_size_t size = 0;
  grub_uint8_t *secboot = NULL;
  grub_uint8_t *setupmode = NULL;
  grub_uint8_t *moksbstate = NULL;
  grub_uint8_t secureboot = GRUB_EFI_SECUREBOOT_MODE_UNKNOWN;
  const char *secureboot_str = "UNKNOWN";

  status = grub_efi_get_variable ("SecureBoot", &efi_variable_guid,
                                  &size, (void **) &secboot);

  if (status == GRUB_EFI_NOT_FOUND)
    {
      secureboot = GRUB_EFI_SECUREBOOT_MODE_DISABLED;
      goto out;
    }

  if (status != GRUB_EFI_SUCCESS)
    goto out;

  status = grub_efi_get_variable ("SetupMode", &efi_variable_guid,
                                  &size, (void **) &setupmode);

  if (status != GRUB_EFI_SUCCESS)
    goto out;

  if ((*secboot == 0) || (*setupmode == 1))
    {
      secureboot = GRUB_EFI_SECUREBOOT_MODE_DISABLED;
      goto out;
    }

  /*
   * See if a user has put the shim into insecure mode. If so, and if the
   * variable doesn't have the runtime attribute set, we might as well
   * honor that.
   */
  status = grub_efi_get_variable_with_attributes ("MokSBState", &efi_shim_lock_guid,
                                    &size, (void **) &moksbstate, &attr);

  /* If it fails, we don't care why. Default to secure. */
  if (status != GRUB_EFI_SUCCESS)
    {
      secureboot = GRUB_EFI_SECUREBOOT_MODE_ENABLED;
      goto out;
    }

  if (!(attr & GRUB_EFI_VARIABLE_RUNTIME_ACCESS) && *moksbstate == 1)
    {
      secureboot = GRUB_EFI_SECUREBOOT_MODE_DISABLED;
      goto out;
    }

  secureboot = GRUB_EFI_SECUREBOOT_MODE_ENABLED;

 out:
  grub_free (moksbstate);
  grub_free (setupmode);
  grub_free (secboot);

  if (secureboot == GRUB_EFI_SECUREBOOT_MODE_DISABLED)
    secureboot_str = "Disabled";
  else if (secureboot == GRUB_EFI_SECUREBOOT_MODE_ENABLED)
    secureboot_str = "Enabled";

  grub_dprintf ("efi", "UEFI Secure Boot state: %s\n", secureboot_str);

  return secureboot;
}
