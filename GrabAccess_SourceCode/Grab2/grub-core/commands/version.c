/* version.c - Command to print the grub version and build info. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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
#include <grub/term.h>
#include <grub/time.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/charset.h>
#include <grub/env.h>
#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#ifdef GRUB_MACHINE_EFI
static char uefi_ver[11] = "\0";

static void
grub_get_uefi_version (void)
{
  grub_efi_uint16_t uefi_major_rev =
            grub_efi_system_table->hdr.revision >> 16;
  grub_efi_uint16_t uefi_minor_rev =
            grub_efi_system_table->hdr.revision & 0xffff;
  grub_efi_uint8_t uefi_minor_1 = uefi_minor_rev / 10;
  grub_efi_uint8_t uefi_minor_2 = uefi_minor_rev % 10;
  grub_snprintf (uefi_ver, 10, "%d.%d", uefi_major_rev, uefi_minor_1);
  if (uefi_minor_2)
    grub_snprintf (uefi_ver, 10, "%s.%d", uefi_ver, uefi_minor_2);
}
#endif

static grub_err_t
grub_cmd_version (grub_command_t cmd __attribute__ ((unused)),
                  int argc __attribute__ ((unused)),
                  char **args __attribute__ ((unused)))
{
  grub_printf (_("GNU GRUB version: %s\n"), PACKAGE_VERSION);
  if (grub_strlen(GRUB_VERSION_GIT) != 0)
    grub_printf (_("GIT code version: %s\n"), GRUB_VERSION_GIT);
  grub_printf (_("Platform: %s-%s\n"), GRUB_TARGET_CPU, GRUB_PLATFORM);
  if (grub_strlen(GRUB_RPM_VERSION) != 0)
    grub_printf (_("RPM package version: %s\n"), GRUB_RPM_VERSION);
  grub_printf (_("Compiler version: %s\n"), __VERSION__);
  grub_printf (_("Build date: %s\n"), GRUB_BUILD_DATE);

#ifdef GRUB_MACHINE_EFI
  grub_get_uefi_version ();
  grub_printf ("UEFI revision: v%s (", uefi_ver);
  {
    char *vendor;
    grub_uint16_t *vendor_utf16;

    for (vendor_utf16 = grub_efi_system_table->firmware_vendor; *vendor_utf16; vendor_utf16++);
    vendor = grub_malloc (4 *
        (vendor_utf16 - grub_efi_system_table->firmware_vendor) + 1);
    if (!vendor)
      return grub_errno;
    *grub_utf16_to_utf8 ((grub_uint8_t *) vendor,
                grub_efi_system_table->firmware_vendor,
                vendor_utf16 - grub_efi_system_table->firmware_vendor) = 0;
    grub_printf ("%s, ", vendor);
    grub_free (vendor);
  }
  grub_printf ("0x%08x)\n", grub_efi_system_table->firmware_revision);
#endif
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(version)
{
  cmd = grub_register_command ("version", grub_cmd_version, NULL,
                               N_("Print version and build information."));
  grub_env_set ("grub_version", GRUB_VERSION);
  grub_env_export ("grub_version");
  grub_env_set ("grub_pkg_version", PACKAGE_VERSION);
  grub_env_export ("grub_pkg_version");
  grub_env_set ("grub_build_date", GRUB_BUILD_DATE);
  grub_env_export ("grub_build_date");
#ifdef GRUB_MACHINE_EFI
  grub_get_uefi_version ();
  grub_env_set ("grub_uefi_version", uefi_ver);
  grub_env_export ("grub_uefi_version");
#endif
}

GRUB_MOD_FINI(version)
{
  grub_unregister_command (cmd);
}
