/* keystatus.c - Command to check key modifier status.  */
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
#include <grub/extcmd.h>
#include <grub/term.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
  {
    /* TRANSLATORS: "Check" in a sense that if this key is pressed then
       "true" is returned, otherwise "false".  */
    {"shift", 's', 0, N_("Check Shift key."), 0, 0},
    {"ctrl", 'c', 0, N_("Check Control key."), 0, 0},
    {"alt", 'a', 0, N_("Check Alt key."), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };

static grub_err_t
grub_cmd_keystatus (grub_extcmd_context_t ctxt,
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
  int expect_mods = 0;
  int mods;

  if (state[0].set)
    expect_mods |= (GRUB_TERM_STATUS_LSHIFT | GRUB_TERM_STATUS_RSHIFT);
  if (state[1].set)
    expect_mods |= (GRUB_TERM_STATUS_LCTRL | GRUB_TERM_STATUS_RCTRL);
  if (state[2].set)
    expect_mods |= (GRUB_TERM_STATUS_LALT | GRUB_TERM_STATUS_RALT);

  grub_dprintf ("keystatus", "expect_mods: %d\n", expect_mods);

  /* Without arguments, just check whether getkeystatus is supported at
     all.  */
  if (expect_mods == 0)
    {
      grub_term_input_t term;
      int nterms = 0;

      FOR_ACTIVE_TERM_INPUTS (term)
	if (!term->getkeystatus)
	  return grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
	else
	  nterms++;
      if (!nterms)
	return grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
      return 0;
    }

  mods = grub_getkeystatus ();
  grub_dprintf ("keystatus", "mods: %d\n", mods);
  if (mods >= 0 && (mods & expect_mods) != 0)
    return 0;
  else
    return grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(keystatus)
{
  cmd = grub_register_extcmd ("keystatus", grub_cmd_keystatus, 0,
			      "[--shift] [--ctrl] [--alt]",
			      /* TRANSLATORS: there are 3 modifiers.  */
			      N_("Check key modifier status."),
			      options);
}

GRUB_MOD_FINI(keystatus)
{
  grub_unregister_extcmd (cmd);
}
