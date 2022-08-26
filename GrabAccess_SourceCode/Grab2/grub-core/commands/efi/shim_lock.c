/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2017  Free Software Foundation, Inc.
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
 *  EFI shim lock verifier.
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/sb.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/verify.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_efi_guid_t shim_lock_guid = GRUB_EFI_SHIM_LOCK_GUID;

/* List of modules which cannot be loaded if UEFI secure boot mode is enabled. */
static const char * const disabled_mods[] =
{
  "iorw",
  "memrw",
  "wrmsr",
  "setup_var",
  "sbpolicy",
  "setenv",
  NULL,
};

static grub_err_t
shim_lock_init (grub_file_t io, enum grub_file_type type,
		void **context __attribute__ ((unused)),
		enum grub_verify_flags *flags)
{
  const char *b, *e;
  int i;

  *flags = GRUB_VERIFY_FLAGS_SKIP_VERIFICATION;

  switch (type & GRUB_FILE_TYPE_MASK)
    {
    case GRUB_FILE_TYPE_GRUB_MODULE:
      /* Establish GRUB module name. */
      b = grub_strrchr (io->name, '/');
      e = grub_strrchr (io->name, '.');

      b = b ? (b + 1) : io->name;
      e = e ? e : io->name + grub_strlen (io->name);
      e = (e > b) ? e : io->name + grub_strlen (io->name);

      for (i = 0; disabled_mods[i]; i++)
	if (!grub_strncmp (b, disabled_mods[i], grub_strlen (b) - grub_strlen (e)))
	  {
	    grub_error (GRUB_ERR_ACCESS_DENIED,
			N_("module cannot be loaded in UEFI secure boot mode: %s"),
			io->name);
	    return GRUB_ERR_ACCESS_DENIED;
	  }

      /* Fall through. */

    case GRUB_FILE_TYPE_ACPI_TABLE:
    case GRUB_FILE_TYPE_DEVICE_TREE_IMAGE:
      *flags = GRUB_VERIFY_FLAGS_DEFER_AUTH;

      return GRUB_ERR_NONE;

    case GRUB_FILE_TYPE_LINUX_KERNEL:
    case GRUB_FILE_TYPE_MULTIBOOT_KERNEL:
    case GRUB_FILE_TYPE_BSD_KERNEL:
    case GRUB_FILE_TYPE_XNU_KERNEL:
    case GRUB_FILE_TYPE_PLAN9_KERNEL:
      for (i = 0; disabled_mods[i]; i++)
	if (grub_dl_get (disabled_mods[i]))
	  {
	    grub_error (GRUB_ERR_ACCESS_DENIED,
			N_("cannot boot due to dangerous module in memory: %s"),
			disabled_mods[i]);
	    return GRUB_ERR_ACCESS_DENIED;
	  }

      *flags = GRUB_VERIFY_FLAGS_SINGLE_CHUNK;

      /* Fall through. */

    default:
      return GRUB_ERR_NONE;
    }
}

static grub_err_t
shim_lock_write (void *context __attribute__ ((unused)), void *buf, grub_size_t size)
{
  grub_efi_shim_lock_protocol_t *sl = grub_efi_locate_protocol (&shim_lock_guid, 0);

  if (sl == NULL)
    return grub_error (GRUB_ERR_ACCESS_DENIED, N_("shim_lock protocol not found"));

  if (sl->verify (buf, size) != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_SIGNATURE, N_("bad shim signature"));

  return GRUB_ERR_NONE;
}

struct grub_file_verifier shim_lock =
  {
    .name = "shim_lock",
    .init = shim_lock_init,
    .write = shim_lock_write
  };

GRUB_MOD_INIT(shim_lock)
{
  grub_efi_shim_lock_protocol_t *sl = grub_efi_locate_protocol (&shim_lock_guid, 0);

  if (sl == NULL || grub_efi_get_secureboot () != GRUB_EFI_SECUREBOOT_MODE_ENABLED)
    return;

  grub_verifier_register (&shim_lock);

  grub_dl_set_persistent (mod);
}

GRUB_MOD_FINI(shim_lock)
{
  grub_verifier_unregister (&shim_lock);
}
