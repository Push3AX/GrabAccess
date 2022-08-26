/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config.h>
#include <config-util.h>

#include <grub/emu/hostfile.h>
#include <grub/emu/config.h>
#include <grub/util/install.h>
#include <grub/util/misc.h>

void
grub_util_load_config (struct grub_util_config *cfg)
{
  const char *cfgfile;
  FILE *f = NULL;
  const char *v;

  cfgfile = grub_util_get_config_filename ();
  if (!grub_util_is_regular (cfgfile))
    return;

  memset (cfg, 0, sizeof (*cfg));

  v = getenv ("GRUB_ENABLE_CRYPTODISK");
  if (v && v[0] == 'y' && v[1] == '\0')
    cfg->is_cryptodisk_enabled = 1;

  v = getenv ("GRUB_DISTRIBUTOR");
  if (v)
    cfg->grub_distributor = xstrdup (v);

  f = grub_util_fopen (cfgfile, "r");
  if (f)
    {
      grub_util_parse_config (f, cfg, 0);
      fclose (f);
    }
  else
    grub_util_warn (_("cannot open configuration file `%s': %s"),
		    cfgfile, strerror (errno));
}
