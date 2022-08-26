/* datehook.c - Module to install datetime hooks.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/datetime.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const char *grub_datetime_names[] =
{
  "YEAR",
  "MONTH",
  "DAY",
  "HOUR",
  "MINUTE",
  "SECOND",
  "WEEKDAY",
};

static const char *
grub_read_hook_datetime (struct grub_env_var *var,
                         const char *val __attribute__ ((unused)))
{
  struct grub_datetime datetime;
  static char buf[6];

  buf[0] = 0;
  if (! grub_get_datetime (&datetime))
    {
      int i;

      for (i = 0; i < 7; i++)
        if (grub_strcmp (var->name, grub_datetime_names[i]) == 0)
          {
            int n;

            switch (i)
              {
              case 0:
                n = datetime.year;
                break;
              case 1:
                n = datetime.month;
                break;
              case 2:
                n = datetime.day;
                break;
              case 3:
                n = datetime.hour;
                break;
              case 4:
                n = datetime.minute;
                break;
              case 5:
                n = datetime.second;
                break;
              default:
                return grub_get_weekday_name (&datetime);
              }

            grub_snprintf (buf, sizeof (buf), "%d", n);
            break;
          }
    }

  return buf;
}

GRUB_MOD_INIT(datehook)
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE (grub_datetime_names); i++)
    {
      grub_register_variable_hook (grub_datetime_names[i],
				   grub_read_hook_datetime, 0);
      grub_env_export (grub_datetime_names[i]);
    }
}

GRUB_MOD_FINI(datehook)
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE (grub_datetime_names); i++)
    {
      grub_register_variable_hook (grub_datetime_names[i], 0, 0);
      grub_env_unset (grub_datetime_names[i]);
    }
}
