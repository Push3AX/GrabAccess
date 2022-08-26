/* extcmd.c - support extended command */
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

#include <grub/mm.h>
#include <grub/list.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/script_sh.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

grub_err_t
grub_extcmd_dispatcher (struct grub_command *cmd, int argc, char **args,
			struct grub_script *script)
{
  int new_argc;
  char **new_args;
  struct grub_arg_list *state;
  struct grub_extcmd_context context;
  grub_err_t ret;
  grub_extcmd_t ext = cmd->data;

  context.state = 0;
  context.extcmd = ext;
  context.script = script;

  if (! ext->options)
    {
      ret = (ext->func) (&context, argc, args);
      return ret;
    }

  state = grub_arg_list_alloc (ext, argc, args);
  if (grub_arg_parse (ext, argc, args, state, &new_args, &new_argc))
    {
      context.state = state;
      ret = (ext->func) (&context, new_argc, new_args);
      grub_free (new_args);
      grub_free (state);
      return ret;
    }

  grub_free (state);
  return grub_errno;
}

static grub_err_t
grub_extcmd_dispatch (struct grub_command *cmd, int argc, char **args)
{
  return grub_extcmd_dispatcher (cmd, argc, args, 0);
}

grub_extcmd_t
grub_register_extcmd_prio (const char *name, grub_extcmd_func_t func,
			   grub_command_flags_t flags, const char *summary,
			   const char *description,
			   const struct grub_arg_option *parser,
			   int prio)
{
  grub_extcmd_t ext;
  grub_command_t cmd;

  ext = (grub_extcmd_t) grub_malloc (sizeof (*ext));
  if (! ext)
    return 0;

  cmd = grub_register_command_prio (name, grub_extcmd_dispatch,
				    summary, description, prio);
  if (! cmd)
    {
      grub_free (ext);
      return 0;
    }

  cmd->flags = (flags | GRUB_COMMAND_FLAG_EXTCMD);
  cmd->data = ext;

  ext->cmd = cmd;
  ext->func = func;
  ext->options = parser;
  ext->data = 0;

  return ext;
}

grub_extcmd_t
grub_register_extcmd (const char *name, grub_extcmd_func_t func,
		      grub_command_flags_t flags, const char *summary,
		      const char *description,
		      const struct grub_arg_option *parser)
{
  return grub_register_extcmd_prio (name, func, flags,
				    summary, description, parser, 1);
}

void
grub_unregister_extcmd (grub_extcmd_t ext)
{
  grub_unregister_command (ext->cmd);
  grub_free (ext);
}
