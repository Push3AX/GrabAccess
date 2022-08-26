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
#include <grub/xen.h>
#include <grub/term.h>

GRUB_MOD_LICENSE ("GPLv3+");

static int
hook (const char *dir, void *hook_data __attribute__ ((unused)))
{
  grub_printf ("%s\n", dir);
  return 0;
}

static grub_err_t
grub_cmd_lsxen (grub_command_t cmd __attribute__ ((unused)),
		int argc, char **args)
{
  char *dir;
  grub_err_t err;
  char *buf;

  if (argc >= 1)
    return grub_xenstore_dir (args[0], hook, NULL);

  buf = grub_xenstore_get_file ("domid", NULL);
  if (!buf)
    return grub_errno;
  dir = grub_xasprintf ("/local/domain/%s", buf);
  grub_free (buf);
  err = grub_xenstore_dir (dir, hook, NULL);
  grub_free (dir);
  return err;
}

static grub_err_t
grub_cmd_catxen (grub_command_t cmd __attribute__ ((unused)),
		 int argc, char **args)
{
  const char *dir = "domid";
  char *buf;

  if (argc >= 1)
    dir = args[0];

  buf = grub_xenstore_get_file (dir, NULL);
  if (!buf)
    return grub_errno;
  grub_xputs (buf);
  grub_xputs ("\n");
  grub_free (buf);
  return GRUB_ERR_NONE;

}

static grub_command_t cmd_ls, cmd_cat;

GRUB_MOD_INIT (lsxen)
{
  cmd_ls = grub_register_command ("xen_ls", grub_cmd_lsxen, N_("[DIR]"),
				  N_("List Xen storage."));
  cmd_cat = grub_register_command ("xen_cat", grub_cmd_catxen, N_("[DIR]"),
				   N_("List Xen storage."));
}

GRUB_MOD_FINI (lsxen)
{
  grub_unregister_command (cmd_ls);
  grub_unregister_command (cmd_cat);
}
