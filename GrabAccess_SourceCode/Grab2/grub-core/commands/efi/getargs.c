/* getargs.c - process command line */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#include <grub/efi/efi.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/charset.h>

GRUB_MOD_LICENSE ("GPLv3+");

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla"
#endif

static const struct grub_arg_option options_getargs[] = {
  {"key", 'k', 0, N_("Show whether the argument is set."), 0, 0},
  {"value", 'v', 0, N_("Show the value of the argument."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options_getargs
{
  GETARGS_KEY,
  GETARGS_VALUE
};

static grub_err_t
process_cmdline (unsigned char *cmdline, char *arg, char *env, int val)
{
  char *tmp = (char *) cmdline;
  char *key;
  char *value;
  if (!env)
    return GRUB_ERR_TEST_FAILURE;
  grub_env_set (env, "0");
  /* Do nothing if we have no command line */
  if ((cmdline == NULL) || (cmdline[0] == '\0'))
    return GRUB_ERR_TEST_FAILURE;
  /* Parse command line */
  while (*tmp)
  {
    /* Skip whitespace */
    while (grub_isspace (*tmp))
      tmp++;
    /* Find value (if any) and end of this argument */
    key = tmp;
    value = NULL;
    while (*tmp)
    {
      if (grub_isspace (*tmp))
      {
        *(tmp++) = '\0';
        break;
      }
      else if (*tmp == '=')
      {
        *(tmp++) = '\0';
        value = tmp;
      }
      else
      {
        tmp++;
      }
    }
    /* Process this argument */
    if (grub_strcmp (key, arg) == 0)
    {
      grub_dprintf ("args", "Argument %s found.\n", key);
      if (!val)
      {
        grub_env_set (env, "1");
        return GRUB_ERR_NONE;
      }
      if ((! value) || (! value[0]))
      {
        grub_env_set (env, "0");
        grub_dprintf ("args", "Argument %s has no values.\n", key);
        break;
      }
      grub_env_set (env, value);
      grub_dprintf ("args", "The value of argument %s is %s.\n", key, value);
      return GRUB_ERR_NONE;
    }
  }
  return GRUB_ERR_TEST_FAILURE;
}

static grub_err_t
grub_cmd_getargs (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;

  if (argc != 2)
  {
    grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unexpected arguments"));
    goto out;
  }

  grub_efi_loaded_image_t *image = NULL;
  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!image)
  {
    grub_error (GRUB_ERR_BUG, N_("unknown error"));
    goto out;
  }
  {
    grub_err_t errno;
    grub_ssize_t cmdline_len = (image->load_options_size / sizeof (grub_efi_char16_t));
    const grub_efi_char16_t *wcmdline = image->load_options;
    unsigned char cmdline[cmdline_len + 1];
    grub_utf16_to_utf8 (cmdline, wcmdline, sizeof (cmdline));

    grub_dprintf ("args", "Command line: %s\n", cmdline);

    if (state[GETARGS_VALUE].set)
      errno = process_cmdline (cmdline, args[0], args[1], 1);
    else
      errno = process_cmdline (cmdline, args[0], args[1], 0);
    return errno;
  }

out:
  return grub_errno;
}

static grub_extcmd_t cmd_getargs;

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

GRUB_MOD_INIT(getargs)
{
  cmd_getargs = grub_register_extcmd ("getargs", grub_cmd_getargs, 0,
                   N_("--key|--value ARGS VARNAME"),
                   N_("process command line."),
                   options_getargs);
}

GRUB_MOD_FINI(getargs)
{
  grub_unregister_extcmd (cmd_getargs);
}
