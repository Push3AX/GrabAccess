/* help.c - command to show a help text.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/term.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/mm.h>
#include <grub/normal.h>
#include <grub/charset.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_help (grub_extcmd_context_t ctxt __attribute__ ((unused)), int argc,
	       char **args)
{
  int cnt = 0;
  char *currarg;

  if (argc == 0)
    {
      grub_command_t cmd;
      FOR_COMMANDS(cmd)
      {
	if ((cmd->prio & GRUB_COMMAND_FLAG_ACTIVE))
	  {
	    struct grub_term_output *term;
	    const char *summary_translated = _(cmd->summary);
	    char *command_help;
	    grub_uint32_t *unicode_command_help;
	    grub_uint32_t *unicode_last_position;

	    command_help = grub_xasprintf ("%s %s", cmd->name, summary_translated);
	    if (!command_help)
	      break;

	    grub_utf8_to_ucs4_alloc (command_help, &unicode_command_help,
				     &unicode_last_position);

	    FOR_ACTIVE_TERM_OUTPUTS(term)
	    {
	      unsigned stringwidth;
	      grub_uint32_t *unicode_last_screen_position;

	      unicode_last_screen_position = unicode_command_help;

	      stringwidth = 0;

	      while (unicode_last_screen_position < unicode_last_position && 
		     stringwidth < ((grub_term_width (term) / 2) - 2))
		{
		  struct grub_unicode_glyph glyph;
		  unicode_last_screen_position 
		    += grub_unicode_aglomerate_comb (unicode_last_screen_position,
						     unicode_last_position
						     - unicode_last_screen_position,
						     &glyph);

		  stringwidth
		    += grub_term_getcharwidth (term, &glyph);
		}

	      grub_print_ucs4 (unicode_command_help,
			       unicode_last_screen_position, 0, 0, term);
	      if (!(cnt % 2))
		grub_print_spaces (term, grub_term_width (term) / 2
				   - stringwidth);
	    }

	    if (cnt % 2)
	      grub_printf ("\n");
	    cnt++;
	  
	    grub_free (command_help);
	    grub_free (unicode_command_help);
	  }
      }
      if (!(cnt % 2))
	grub_printf ("\n");
    }
  else
    {
      int i;
      grub_command_t cmd_iter, cmd, cmd_next;

      for (i = 0; i < argc; i++)
	{
	  currarg = args[i];

	  FOR_COMMANDS_SAFE (cmd_iter, cmd_next)
	  {
	    if (!(cmd_iter->prio & GRUB_COMMAND_FLAG_ACTIVE))
	      continue;

	    if (grub_strncmp (cmd_iter->name, currarg,
			      grub_strlen (currarg)) != 0)
	      continue;
	    if (cmd_iter->flags & GRUB_COMMAND_FLAG_DYNCMD)
	      cmd = grub_dyncmd_get_cmd (cmd_iter);
	    else
	      cmd = cmd_iter;
	    if (!cmd)
	      {
		grub_print_error ();
		continue;
	      }
	    if (cnt++ > 0)
	      grub_printf ("\n\n");

	    if ((cmd->flags & GRUB_COMMAND_FLAG_EXTCMD) &&
		! (cmd->flags & GRUB_COMMAND_FLAG_DYNCMD))
	      grub_arg_show_help ((grub_extcmd_t) cmd->data);
	    else
	      grub_printf ("%s %s %s\n%s\n", _("Usage:"), cmd->name,
			   _(cmd->summary), _(cmd->description));
	  }
	}
    }

  return 0;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(help)
{
  cmd = grub_register_extcmd ("help", grub_cmd_help, 0,
			      N_("[PATTERN ...]"),
			      N_("Show a help message."), 0);
}

GRUB_MOD_FINI(help)
{
  grub_unregister_extcmd (cmd);
}
