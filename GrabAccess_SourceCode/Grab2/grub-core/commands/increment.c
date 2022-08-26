/* increment.c - Commands to increment and decrement variables. */
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
#include <grub/env.h>

GRUB_MOD_LICENSE ("GPLv3+");

typedef enum {
    INCREMENT,
    DECREMENT,
} operation;

static grub_err_t
incr_decr(operation op, int argc, char **args)
{
  const char *old;
  char *new;
  long value;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_ ("no variable specified"));
  if (argc > 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_ ("too many arguments"));

  old = grub_env_get (*args);
  if (!old)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("No such variable \"%s\""),
		       *args);

  value = grub_strtol (old, NULL, 0);
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  switch (op)
    {
    case INCREMENT:
      value += 1;
      break;
    case DECREMENT:
      value -= 1;
      break;
    }

  new = grub_xasprintf ("%ld", value);
  if (!new)
    return grub_errno;

  grub_env_set (*args, new);
  grub_free (new);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_incr(struct grub_command *cmd __attribute__ ((unused)),
              int argc, char **args)
{
  return incr_decr(INCREMENT, argc, args);
}

static grub_err_t
grub_cmd_decr(struct grub_command *cmd __attribute__ ((unused)),
              int argc, char **args)
{
  return incr_decr(DECREMENT, argc, args);
}

static grub_command_t cmd_incr, cmd_decr;

GRUB_MOD_INIT(increment)
{
  cmd_incr = grub_register_command ("increment", grub_cmd_incr, N_("VARIABLE"),
                                    N_("increment VARIABLE"));
  cmd_decr = grub_register_command ("decrement", grub_cmd_decr, N_("VARIABLE"),
                                    N_("decrement VARIABLE"));
}

GRUB_MOD_FINI(increment)
{
  grub_unregister_command (cmd_incr);
  grub_unregister_command (cmd_decr);
}
