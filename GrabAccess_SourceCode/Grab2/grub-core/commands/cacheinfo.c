/* cacheinfo.c - disk cache statistics  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2010  Free Software Foundation, Inc.
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
#include <grub/disk.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_rescue_cmd_info (struct grub_command *cmd __attribute__ ((unused)),
    int argc __attribute__ ((unused)),
    char *argv[] __attribute__ ((unused)))
{
  unsigned long hits, misses;

  grub_disk_cache_get_performance (&hits, &misses);
  if (hits + misses)
    {
      unsigned long ratio = hits * 10000 / (hits + misses);
      grub_printf ("(%lu.%lu%%)\n", ratio / 100, ratio % 100);
      grub_printf_ (N_("Disk cache statistics: hits = %lu (%lu.%02lu%%),"
		     " misses = %lu\n"), ratio / 100, ratio % 100,
		    hits, misses);
    }
  else
    grub_printf ("%s\n", _("No disk cache statistics available\n"));    

 return 0;
}

static grub_command_t cmd_cacheinfo;

GRUB_MOD_INIT(cacheinfo)
{
  cmd_cacheinfo =
    grub_register_command ("cacheinfo", grub_rescue_cmd_info,
			   0, N_("Get disk cache info."));
}

GRUB_MOD_FINI(cacheinfo)
{
  grub_unregister_command (cmd_cacheinfo);
}
