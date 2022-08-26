/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2013  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/cmos.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_cmosdump (struct grub_command *cmd __attribute__ ((unused)),
		   int argc __attribute__ ((unused)), char *argv[] __attribute__ ((unused)))
{
  int i;

  for (i = 0; i < 256; i++)
    {
      grub_err_t err;
      grub_uint8_t value;
      if ((i & 0xf) == 0)
	grub_printf ("%02x: ", i);

      err = grub_cmos_read (i, &value);
      if (err)
	return err;

      grub_printf ("%02x ", value);
      if ((i & 0xf) == 0xf)
	grub_printf ("\n");
    }
  return GRUB_ERR_NONE;
}

static grub_command_t cmd;


GRUB_MOD_INIT(cmosdump)
{
  cmd = grub_register_command ("cmosdump", grub_cmd_cmosdump,
			       0,
			       N_("Show raw dump of the CMOS contents."));
}

GRUB_MOD_FINI(cmosdump)
{
  grub_unregister_command (cmd);
}
