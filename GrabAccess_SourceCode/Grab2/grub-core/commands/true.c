/* true.c - true and false commands.  */
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

#include <grub/dl.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_true (struct grub_command *cmd __attribute__ ((unused)),
	       int argc __attribute__ ((unused)),
	       char *argv[] __attribute__ ((unused)))
{
  return 0;
}

static grub_err_t
grub_cmd_false (struct grub_command *cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char *argv[] __attribute__ ((unused)))
{
  return grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
}

static grub_command_t cmd_true, cmd_false;


GRUB_MOD_INIT(true)
{
  cmd_true =
    grub_register_command ("true", grub_cmd_true,
			   /* TRANSLATORS: it's a command description.  */
			   0, N_("Do nothing, successfully."));
  cmd_false =
    grub_register_command ("false", grub_cmd_false,
			   /* TRANSLATORS: it's a command description.  */
			   0, N_("Do nothing, unsuccessfully."));
}

GRUB_MOD_FINI(true)
{
  grub_unregister_command (cmd_true);
  grub_unregister_command (cmd_false);
}
