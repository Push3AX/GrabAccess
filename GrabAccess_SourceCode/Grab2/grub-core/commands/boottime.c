/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

GRUB_MOD_LICENSE ("GPLv3+");


static grub_err_t
grub_cmd_boottime (struct grub_command *cmd __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char *argv[] __attribute__ ((unused)))
{
  struct grub_boot_time *cur;
  grub_uint64_t last_time = 0, start_time = 0;
  if (!grub_boot_time_head)
    {
      grub_puts_ (N_("No boot time statistics is available\n"));
      return 0;
    }
  start_time = last_time = grub_boot_time_head->tp;
  for (cur = grub_boot_time_head; cur; cur = cur->next)
    {
      grub_uint32_t tmabs = cur->tp - start_time;
      grub_uint32_t tmrel = cur->tp - last_time;
      last_time = cur->tp;

      grub_printf ("%3d.%03ds %2d.%03ds %s:%d %s\n", 
		   tmabs / 1000, tmabs % 1000, tmrel / 1000, tmrel % 1000, cur->file, cur->line,
		   cur->msg);
    }
 return 0;
}

static grub_command_t cmd_boottime;

GRUB_MOD_INIT(boottime)
{
  cmd_boottime =
    grub_register_command ("boottime", grub_cmd_boottime,
			   0, N_("Show boot time statistics."));
}

GRUB_MOD_FINI(boottime)
{
  grub_unregister_command (cmd_boottime);
}
