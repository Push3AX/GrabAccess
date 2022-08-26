/* echo.c - Command to display a line of text  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011  Free Software Foundation, Inc.
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

#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");


static grub_err_t
grub_cmd_time (grub_command_t ctxt __attribute__ ((unused)),
	       int argc, char **args)
{
  grub_command_t cmd;
  grub_uint32_t start;
  grub_uint32_t end;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("no command is specified"));

  cmd = grub_command_find (args[0]);

  if (!cmd)
    return grub_error (GRUB_ERR_UNKNOWN_COMMAND, N_("can't find command `%s'"),
		       args[0]);

  start = grub_get_time_ms ();
  (cmd->func) (cmd, argc - 1, &args[1]);
  end = grub_get_time_ms ();

  grub_printf_ (N_("Elapsed time: %d.%03d seconds \n"), (end - start) / 1000,
		(end - start) % 1000);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(time)
{
  cmd = grub_register_command ("time", grub_cmd_time,
			      N_("COMMAND [ARGS]"),
			       N_("Measure time used by COMMAND"));
}

GRUB_MOD_FINI(time)
{
  grub_unregister_command (cmd);
}
