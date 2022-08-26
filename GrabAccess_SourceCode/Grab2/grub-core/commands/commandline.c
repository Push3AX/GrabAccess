/* commandline.c */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2010,2017  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_commandline (grub_command_t cmd __attribute__ ((unused)),
	      int argc __attribute__ ((unused)), char **args __attribute__ ((unused)))

{
  grub_cmdline_run (1, 0);
  return GRUB_ERR_NONE;
}

static grub_command_t cmd;

GRUB_MOD_INIT(commandline)
{
  cmd = grub_register_command ("commandline", grub_cmd_commandline,
			       N_(" "),
			       N_("GRUB Command line."));
}

GRUB_MOD_FINI(commandline)
{
  grub_unregister_command (cmd);
}
