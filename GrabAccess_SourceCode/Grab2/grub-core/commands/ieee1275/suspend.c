/* suspend.c - command to suspend GRUB and return to Open Firmware  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/ieee1275/ieee1275.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_suspend (grub_command_t cmd __attribute__ ((unused)),
		  int argc __attribute__ ((unused)),
		  char **args __attribute__ ((unused)))
{
  grub_puts_ (N_("Run `go' to resume GRUB."));
  grub_ieee1275_enter ();
  grub_cls ();
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(ieee1275_suspend)
{
  cmd = grub_register_command ("suspend", grub_cmd_suspend,
			       0, N_("Return to IEEE1275 prompt."));
}

GRUB_MOD_FINI(ieee1275_suspend)
{
  grub_unregister_command (cmd);
}
