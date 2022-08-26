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

#include <grub/auth.h>
#include <grub/crypto.h>
#include <grub/list.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;

static grub_err_t
check_password (const char *user, const char *entered,
		void *password)
{
  if (grub_crypto_memcmp (entered, password, GRUB_AUTH_MAX_PASSLEN) != 0)
    return GRUB_ACCESS_DENIED;

  grub_auth_authenticate (user);

  return GRUB_ERR_NONE;
}

grub_err_t
grub_normal_set_password (const char *user, const char *password)
{
  grub_err_t err;
  char *pass;
  int copylen;

  pass = grub_zalloc (GRUB_AUTH_MAX_PASSLEN);
  if (!pass)
    return grub_errno;
  copylen = grub_strlen (password);
  if (copylen >= GRUB_AUTH_MAX_PASSLEN)
    copylen = GRUB_AUTH_MAX_PASSLEN - 1;
  grub_memcpy (pass, password, copylen);

  err = grub_auth_register_authentication (user, check_password, pass);
  if (err)
    {
      grub_free (pass);
      return err;
    }
  grub_dl_ref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_password (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));
  return grub_normal_set_password (args[0], args[1]);
}

static grub_command_t cmd;

GRUB_MOD_INIT(password)
{
  my_mod = mod;
  cmd = grub_register_command ("password", grub_cmd_password,
			       N_("USER PASSWORD"),
			       N_("Set user password (plaintext). "
			       "Unrecommended and insecure."));
}

GRUB_MOD_FINI(password)
{
  grub_unregister_command (cmd);
}
