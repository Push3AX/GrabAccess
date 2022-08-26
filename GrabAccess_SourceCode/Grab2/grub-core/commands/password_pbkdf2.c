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

struct pbkdf2_password
{
  grub_uint8_t *salt;
  grub_size_t saltlen;
  unsigned int c;
  grub_uint8_t *expected;
  grub_size_t buflen;
};

static grub_err_t
check_password (const char *user, const char *entered, void *pin)
{
  grub_uint8_t *buf;
  struct pbkdf2_password *pass = pin;
  gcry_err_code_t err;
  grub_err_t ret;

  buf = grub_malloc (pass->buflen);
  if (!buf)
    return grub_crypto_gcry_error (GPG_ERR_OUT_OF_MEMORY);

  err = grub_crypto_pbkdf2 (GRUB_MD_SHA512, (grub_uint8_t *) entered,
			    grub_strlen (entered),
			    pass->salt, pass->saltlen, pass->c,
			    buf, pass->buflen);
  if (err)
      ret = grub_crypto_gcry_error (err);
  else if (grub_crypto_memcmp (buf, pass->expected, pass->buflen) != 0)
      ret = GRUB_ACCESS_DENIED;
  else
    {
      grub_auth_authenticate (user);
      ret = GRUB_ERR_NONE;
    }

  grub_free (buf);
  return ret;
}

static inline int
hex2val (char hex)
{
  if ('0' <= hex && hex <= '9')
    return hex - '0';
  if ('a' <= hex && hex <= 'f')
    return hex - 'a' + 10;
  if ('A' <= hex && hex <= 'F')
    return hex - 'A' + 10;
  return -1;
}

static grub_err_t
grub_cmd_password (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  grub_err_t err;
  const char *ptr, *ptr2;
  grub_uint8_t *ptro;
  struct pbkdf2_password *pass;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  if (grub_memcmp (args[1], "grub.pbkdf2.sha512.",
		   sizeof ("grub.pbkdf2.sha512.") - 1) != 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid PBKDF2 password"));

  ptr = args[1] + sizeof ("grub.pbkdf2.sha512.") - 1;

  pass = grub_malloc (sizeof (*pass));
  if (!pass)
    return grub_errno;

  pass->c = grub_strtoul (ptr, &ptr, 0);
  if (grub_errno)
    {
      grub_free (pass);
      return grub_errno;
    }
  if (*ptr != '.')
    {
      grub_free (pass);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid PBKDF2 password"));
    }
  ptr++;

  ptr2 = grub_strchr (ptr, '.');
  if (!ptr2 || ((ptr2 - ptr) & 1) || grub_strlen (ptr2 + 1) & 1)
    {
      grub_free (pass);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid PBKDF2 password"));
    }

  pass->saltlen = (ptr2 - ptr) >> 1;
  pass->buflen = grub_strlen (ptr2 + 1) >> 1;
  ptro = pass->salt = grub_malloc (pass->saltlen);
  if (!ptro)
    {
      grub_free (pass);
      return grub_errno;
    }
  while (ptr < ptr2)
    {
      int hex1, hex2;
      hex1 = hex2val (*ptr);
      ptr++;
      hex2 = hex2val (*ptr);
      ptr++;
      if (hex1 < 0 || hex2 < 0)
	{
	  grub_free (pass->salt);
	  grub_free (pass);
	  return grub_error (GRUB_ERR_BAD_ARGUMENT,
			     /* TRANSLATORS: it means that the string which
				was supposed to be a password hash doesn't
				have a correct format, not to password
				mismatch.  */
			     N_("invalid PBKDF2 password"));
	}

      *ptro = (hex1 << 4) | hex2;
      ptro++;
    }

  ptro = pass->expected = grub_malloc (pass->buflen);
  if (!ptro)
    {
      grub_free (pass->salt);
      grub_free (pass);
      return grub_errno;
    }
  ptr = ptr2 + 1;
  ptr2 += grub_strlen (ptr2); 
  while (ptr < ptr2)
    {
      int hex1, hex2;
      hex1 = hex2val (*ptr);
      ptr++;
      hex2 = hex2val (*ptr);
      ptr++;
      if (hex1 < 0 || hex2 < 0)
	{
	  grub_free (pass->expected);
	  grub_free (pass->salt);
	  grub_free (pass);
	  return grub_error (GRUB_ERR_BAD_ARGUMENT,
			     N_("invalid PBKDF2 password"));
	}

      *ptro = (hex1 << 4) | hex2;
      ptro++;
    }

  err = grub_auth_register_authentication (args[0], check_password, pass);
  if (err)
    {
      grub_free (pass);
      return err;
    }
  grub_dl_ref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_command_t cmd;

GRUB_MOD_INIT(password_pbkdf2)
{
  my_mod = mod;
  cmd = grub_register_command ("password_pbkdf2", grub_cmd_password,
			       N_("USER PBKDF2_PASSWORD"),
			       N_("Set user password (PBKDF2). "));
}

GRUB_MOD_FINI(password_pbkdf2)
{
  grub_unregister_command (cmd);
}
