/* tr.c -- The tr command.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2013  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/env.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/extcmd.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
  {
    { "set", 's', 0, N_("Set a variable to return value."), N_("VARNAME"), ARG_TYPE_STRING },
    { "upcase", 'U', 0, N_("Translate to upper case."), 0, 0 },
    { "downcase", 'D', 0, N_("Translate to lower case."), 0, 0 },
    { 0, 0, 0, 0, 0, 0 }
  };

static const char *letters_lowercase = "abcdefghijklmnopqrstuvwxyz";
static const char *letters_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static grub_err_t
grub_cmd_tr (grub_extcmd_context_t ctxt, int argc, char **args)
{
  char *var = 0;
  const char *input = 0;
  char *output = 0, *optr;
  const char *s1 = 0;
  const char *s2 = 0;
  const char *iptr;

  /* Select the defaults from options. */
  if (ctxt->state[0].set) {
    var = ctxt->state[0].arg;
    input = grub_env_get (var);
  }

  if (ctxt->state[1].set) {
    s1 = letters_lowercase;
    s2 = letters_uppercase;
  }

  if (ctxt->state[2].set) {
    s1 = letters_uppercase;
    s2 = letters_lowercase;
  }

  /* Check for arguments and update the defaults.  */
  if (argc == 1)
    input = args[0];

  else if (argc == 2) {
    s1 = args[0];
    s2 = args[1];

  } else if (argc == 3) {
    s1 = args[0];
    s2 = args[1];
    input = args[2];

  } else if (argc > 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "too many parameters");

  if (!s1 || !s2 || !input)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "missing parameters");

  if (grub_strlen (s1) != grub_strlen (s2))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "set sizes did not match");

  /* Translate input into output buffer.  */

  output = grub_malloc (grub_strlen (input) + 1);
  if (! output)
    return grub_errno;

  optr = output;
  for (iptr = input; *iptr; iptr++)
    {
      char *p = grub_strchr (s1, *iptr);
      if (p)
	*optr++ = s2[p - s1];
      else
	*optr++ = *iptr;
    }
  *optr = '\0';

  if (ctxt->state[0].set)
    grub_env_set (var, output);
  else
    grub_printf ("%s\n", output);

  grub_free (output);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(tr)
{
  cmd = grub_register_extcmd ("tr", grub_cmd_tr, 0, N_("[OPTIONS] [SET1] [SET2] [STRING]"),
			      N_("Translate SET1 characters to SET2 in STRING."), options);
}

GRUB_MOD_FINI(tr)
{
  grub_unregister_extcmd (cmd);
}
