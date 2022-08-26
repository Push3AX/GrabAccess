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
#include <grub/misc.h>
#include <grub/script_sh.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/term.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_eval (grub_command_t cmd __attribute__((__unused__)),
	       int argc, char *argv[])
{
  int i;
  grub_size_t size = argc; /* +1 for final zero */
  char *str, *p;
  grub_err_t ret;

  if (argc == 0)
    return GRUB_ERR_NONE;

  for (i = 0; i < argc; i++)
    size += grub_strlen (argv[i]);

  str = p = grub_malloc (size);
  if (!str)
    return grub_errno;

  for (i = 0; i < argc; i++)
    {
      p = grub_stpcpy (p, argv[i]);
      *p++ = ' ';
    }
  *--p = '\0';

  ret = grub_script_execute_sourcecode (str);
  grub_free (str);
  return ret;
}

static grub_command_t cmd;

GRUB_MOD_INIT(eval)
{
  cmd = grub_register_command ("eval", grub_cmd_eval, N_("STRING ..."),
				N_("Evaluate arguments as GRUB commands"));
}

GRUB_MOD_FINI(eval)
{
  grub_unregister_command (cmd);
}

