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

#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/test.h>
#include <grub/mm.h>

GRUB_MOD_LICENSE ("GPLv3+");

static void
xnu_uuid_test (void)
{
  grub_command_t cmd;
  cmd = grub_command_find ("xnu_uuid");
  char *args[] = { (char *) "fedcba98", (char *) "tstvar", NULL };
  const char *val;

  if (!cmd)
    {
      grub_test_assert (0, "can't find command `%s'", "xnu_uuid");
      return;
    }
  if ((cmd->func) (cmd, 2, args))
    {
      grub_test_assert (0, "%d: %s", grub_errno, grub_errmsg);
      return;
    }

  val = grub_env_get ("tstvar");
  if (!val)
    {
      grub_test_assert (0, "tstvar isn't set");
      return;
    }
  grub_test_assert (grub_strcmp (val, "944F9DED-DBED-391C-9402-77C8CEE04173")
		    == 0, "UUIDs don't match");
}

GRUB_FUNCTIONAL_TEST (xnu_uuid_test, xnu_uuid_test);
