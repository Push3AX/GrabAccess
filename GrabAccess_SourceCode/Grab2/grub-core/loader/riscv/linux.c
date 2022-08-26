/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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

#include <grub/command.h>
#include <grub/dl.h>
#include <grub/lib/cmdline.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_initrd (grub_command_t cmd __attribute__ ((unused)),
		 int argc __attribute__ ((unused)),
		 char *argv[] __attribute__ ((unused)))
{
  grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, N_("Linux not supported yet"));

  return grub_errno;
}

static grub_err_t
grub_cmd_linux (grub_command_t cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char *argv[] __attribute__ ((unused)))
{
  grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, N_("Linux not supported yet"));

  return grub_errno;
}

static grub_command_t cmd_linux, cmd_initrd;

GRUB_MOD_INIT (linux)
{
  cmd_linux = grub_register_command ("linux", grub_cmd_linux, 0,
				     N_("Load Linux."));
  cmd_initrd = grub_register_command ("initrd", grub_cmd_initrd, 0,
				      N_("Load initrd."));
}

GRUB_MOD_FINI (linux)
{
  grub_unregister_command (cmd_linux);
  grub_unregister_command (cmd_initrd);
}
