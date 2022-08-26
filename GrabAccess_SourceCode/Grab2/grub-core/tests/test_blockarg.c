/* test_blockarg.c - print and execute block argument  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/extcmd.h>
#include <grub/script_sh.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
test_blockarg (grub_extcmd_context_t ctxt, int argc, char **args)
{
  if (! ctxt->script)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no block parameter");

  grub_printf ("%s\n", args[argc - 1]);
  grub_script_execute (ctxt->script);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(test_blockarg)
{
  cmd = grub_register_extcmd ("test_blockarg", test_blockarg,
			      GRUB_COMMAND_FLAG_BLOCKS,
			      N_("BLOCK"),
			      /* TRANSLATORS: this is the BLOCK-argument, not
			       environment block.  */
			      N_("Print and execute block argument."), 0);
}

GRUB_MOD_FINI(test_blockarg)
{
  grub_unregister_extcmd (cmd);
}
