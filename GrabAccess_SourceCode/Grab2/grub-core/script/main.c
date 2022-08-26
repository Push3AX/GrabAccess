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
#include <grub/i18n.h>
#include <grub/parser.h>
#include <grub/script_sh.h>

grub_err_t
grub_normal_parse_line (char *line,
			grub_reader_getline_t getline, void *getline_data)
{
  struct grub_script *parsed_script;

  /* Parse the script.  */
  parsed_script = grub_script_parse (line, getline, getline_data);

  if (parsed_script)
    {
      /* Execute the command(s).  */
      grub_script_execute (parsed_script);

      /* The parsed script was executed, throw it away.  */
      grub_script_unref (parsed_script);
    }

  return grub_errno;
}

static grub_command_t cmd_break;
static grub_command_t cmd_continue;
static grub_command_t cmd_shift;
static grub_command_t cmd_setparams;
static grub_command_t cmd_return;

void
grub_script_init (void)
{
  cmd_break = grub_register_command ("break", grub_script_break,
				     N_("[NUM]"), N_("Exit from loops"));
  cmd_continue = grub_register_command ("continue", grub_script_break,
					N_("[NUM]"), N_("Continue loops"));
  cmd_shift = grub_register_command ("shift", grub_script_shift,
				     N_("[NUM]"),
				     /* TRANSLATORS: Positional arguments are
					arguments $0, $1, $2, ...  */
				     N_("Shift positional parameters."));
  cmd_setparams = grub_register_command ("setparams", grub_script_setparams,
					 N_("[VALUE]..."),
					 N_("Set positional parameters."));
  cmd_return = grub_register_command ("return", grub_script_return,
				      N_("[NUM]"),
				      /* TRANSLATORS: It's a command description
					 and "Return" is a verb, not a noun. The
					 command in question is "return" and
					 has exactly the same semanics as bash
					 equivalent.  */
				      N_("Return from a function."));
}

void
grub_script_fini (void)
{
  if (cmd_break)
    grub_unregister_command (cmd_break);
  cmd_break = 0;

  if (cmd_continue)
    grub_unregister_command (cmd_continue);
  cmd_continue = 0;

  if (cmd_shift)
    grub_unregister_command (cmd_shift);
  cmd_shift = 0;

  if (cmd_setparams)
    grub_unregister_command (cmd_setparams);
  cmd_setparams = 0;

  if (cmd_return)
    grub_unregister_command (cmd_return);
  cmd_return = 0;
}
