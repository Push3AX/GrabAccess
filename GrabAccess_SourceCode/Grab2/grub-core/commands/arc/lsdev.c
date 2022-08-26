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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/arc/arc.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Helper for grub_cmd_lsdev.  */
static int
grub_cmd_lsdev_iter (const char *name,
		     const struct grub_arc_component *comp __attribute__ ((unused)),
		     void *data __attribute__ ((unused)))
{
  grub_printf ("%s\n", name);
  return 0;
}

static grub_err_t
grub_cmd_lsdev (grub_command_t cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char **args __attribute__ ((unused)))
{
  grub_arc_iterate_devs (grub_cmd_lsdev_iter, 0, 0);
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(lsdev)
{
  cmd = grub_register_command ("lsdev", grub_cmd_lsdev, "",
			      N_("List devices."));
}

GRUB_MOD_FINI(lsdev)
{
  grub_unregister_command (cmd);
}
