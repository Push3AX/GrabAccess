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
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/env.h>
#include <grub/lib/envblk.h>
#include <grub/command.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const grub_efi_guid_t grub_env_guid = GRUB_EFI_GRUB_VARIABLE_GUID;

static grub_err_t
grub_efi_export_env(grub_command_t cmd __attribute__ ((unused)),
                    int argc, char *argv[])
{
  const char *value;
  char *old_value;
  struct grub_envblk envblk_s = { NULL, 0 };
  grub_envblk_t envblk = &envblk_s;
  grub_err_t err;
  int changed = 1;
  grub_efi_status_t status;

  grub_dprintf ("efienv", "argc:%d\n", argc);
  for (int i = 0; i < argc; i++)
    grub_dprintf ("efienv", "argv[%d]: %s\n", i, argv[i]);

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("variable name expected"));

  grub_efi_get_variable ("GRUB_ENV", &grub_env_guid,
                         &envblk_s.size, (void **) &envblk_s.buf);
  if (!envblk_s.buf || envblk_s.size < 1)
    {
      char *buf = grub_malloc (1025);
      if (!buf)
        return grub_errno;

      grub_memcpy (buf, GRUB_ENVBLK_SIGNATURE, sizeof (GRUB_ENVBLK_SIGNATURE) - 1);
      grub_memset (buf + sizeof (GRUB_ENVBLK_SIGNATURE) - 1, '#',
	      DEFAULT_ENVBLK_SIZE - sizeof (GRUB_ENVBLK_SIGNATURE) + 1);
      buf[1024] = '\0';

      envblk_s.buf = buf;
      envblk_s.size = 1024;
    }
  else
    {
      char *buf = grub_realloc (envblk_s.buf, envblk_s.size + 1);
      if (!buf)
	return grub_errno;

      envblk_s.buf = buf;
      envblk_s.buf[envblk_s.size] = '\0';
    }

  err = grub_envblk_get(envblk, argv[0], &old_value);
  if (err != GRUB_ERR_NONE)
    {
      grub_dprintf ("efienv", "grub_envblk_get returned %d\n", err);
      return err;
    }

  value = grub_env_get(argv[0]);
  if ((!value && !old_value) ||
      (value && old_value && !grub_strcmp(old_value, value)))
    changed = 0;

  if (old_value)
    grub_free(old_value);

  if (changed == 0)
    {
      grub_dprintf ("efienv", "No changes necessary\n");
      return 0;
    }

  if (value)
    {
      grub_dprintf ("efienv", "setting \"%s\" to \"%s\"\n", argv[0], value);
      grub_envblk_set(envblk, argv[0], value);
    }
  else
    {
      grub_dprintf ("efienv", "deleting \"%s\" from envblk\n", argv[0]);
      grub_envblk_delete(envblk, argv[0]);
    }

  grub_dprintf ("efienv", "envblk is %lu bytes:\n\"%s\"\n", (unsigned long) envblk_s.size, envblk_s.buf);

  grub_dprintf ("efienv", "removing GRUB_ENV\n");
  status = grub_efi_set_variable ("GRUB_ENV", &grub_env_guid, NULL, 0);
  if (status != GRUB_EFI_SUCCESS)
    grub_dprintf ("efienv", "removal returned %ld\n", (long) status);

  grub_dprintf ("efienv", "setting GRUB_ENV\n");
  status = grub_efi_set_variable ("GRUB_ENV", &grub_env_guid,
				  envblk_s.buf, envblk_s.size);
  if (status != GRUB_EFI_SUCCESS)
    grub_dprintf ("efienv", "setting GRUB_ENV returned %ld\n", (long) status);

  return 0;
}

static int
set_var (const char *name, const char *value,
	 void *whitelist __attribute__((__unused__)))
{
  grub_env_set (name, value);
  return 0;
}

static grub_err_t
grub_efi_load_env(grub_command_t cmd __attribute__ ((unused)),
                    int argc, char *argv[] __attribute__((__unused__)))
{
  struct grub_envblk envblk_s = { NULL, 0 };
  grub_envblk_t envblk = &envblk_s;

  grub_efi_get_variable ("GRUB_ENV", &grub_env_guid,
                         &envblk_s.size, (void **) &envblk_s.buf);
  if (!envblk_s.buf || envblk_s.size < 1)
    return 0;

  if (argc > 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unexpected argument"));

  grub_envblk_iterate (envblk, NULL, set_var);
  grub_free (envblk_s.buf);
  
  return GRUB_ERR_NONE;
}

static grub_command_t export_cmd, loadenv_cmd;

GRUB_MOD_INIT(efienv)
{
  export_cmd = grub_register_command ("efi-export-env", grub_efi_export_env,
	    N_("VARIABLE_NAME"), N_("Export environment variable to UEFI."));
  loadenv_cmd = grub_register_command ("efi-load-env", grub_efi_load_env,
	    NULL, N_("Load the grub environment from UEFI."));
}

GRUB_MOD_FINI(efienv)
{
  grub_unregister_command (export_cmd);
  grub_unregister_command (loadenv_cmd);
}
