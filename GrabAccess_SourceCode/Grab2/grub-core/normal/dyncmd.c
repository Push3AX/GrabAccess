/* dyncmd.c - support dynamic command */
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
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/command.h>
#include <grub/normal.h>
#include <grub/extcmd.h>
#include <grub/script_sh.h>
#include <grub/i18n.h>

grub_command_t
grub_dyncmd_get_cmd (grub_command_t cmd)
{
  grub_extcmd_t extcmd = cmd->data;
  char *modname;
  char *name;
  grub_dl_t mod;

  modname = extcmd->data;
  mod = grub_dl_load (modname);
  if (!mod)
    return NULL;

  grub_free (modname);
  grub_dl_ref (mod);

  name = (char *) cmd->name;
  grub_unregister_extcmd (extcmd);

  cmd = grub_command_find (name);

  grub_free (name);

  return cmd;
}

static grub_err_t
grub_dyncmd_dispatcher (struct grub_extcmd_context *ctxt,
			int argc, char **args)
{
  char *modname;
  grub_dl_t mod;
  grub_err_t ret;
  grub_extcmd_t extcmd = ctxt->extcmd;
  grub_command_t cmd = extcmd->cmd;
  char *name;

  modname = extcmd->data;
  mod = grub_dl_load (modname);
  if (!mod)
    return grub_errno;

  grub_free (modname);
  grub_dl_ref (mod);

  name = (char *) cmd->name;
  grub_unregister_extcmd (extcmd);

  cmd = grub_command_find (name);
  if (cmd)
    {
      if (cmd->flags & GRUB_COMMAND_FLAG_BLOCKS &&
	  cmd->flags & GRUB_COMMAND_FLAG_EXTCMD)
	ret = grub_extcmd_dispatcher (cmd, argc, args, ctxt->script);
      else
	ret = (cmd->func) (cmd, argc, args);
    }
  else
    ret = grub_errno;

  grub_free (name);

  return ret;
}

/* Read the file command.lst for auto-loading.  */
void
read_command_list (const char *prefix)
{
  if (prefix)
    {
      char *filename;

      filename = grub_xasprintf ("%s/" GRUB_TARGET_CPU "-" GRUB_PLATFORM
				 "/command.lst", prefix);
      if (filename)
	{
	  grub_file_t file;

	  file = grub_file_open (filename, GRUB_FILE_TYPE_GRUB_MODULE_LIST);
	  if (file)
	    {
	      char *buf = NULL;
	      grub_command_t ptr, last = 0, next;

	      /* Override previous commands.lst.  */
	      for (ptr = grub_command_list; ptr; ptr = next)
		{
		  next = ptr->next;
		  if (ptr->flags & GRUB_COMMAND_FLAG_DYNCMD)
		    {
		      if (last)
			last->next = ptr->next;
		      else
			grub_command_list = ptr->next;
		      grub_free (ptr->data); /* extcmd struct */
		      grub_free (ptr);
		    }
		  else
		    last = ptr;
		}

	      for (;; grub_free (buf))
		{
		  char *p, *name, *modname;
		  grub_extcmd_t cmd;
		  int prio = 0;

		  buf = grub_file_getline (file);

		  if (! buf)
		    break;

		  name = buf;
		  while (grub_isspace (name[0]))
		    name++;

		  if (*name == '*')
		    {
		      name++;
		      prio++;
		    }

		  if (! grub_isgraph (name[0]))
		    continue;

		  p = grub_strchr (name, ':');
		  if (! p)
		    continue;

		  *p = '\0';
		  p++;
		  while (*p == ' ' || *p == '\t')
		    p++;

		  if (! grub_isgraph (*p))
		    continue;

		  if (grub_dl_get (p))
		    continue;

		  name = grub_strdup (name);
		  if (! name)
		    continue;

		  modname = grub_strdup (p);
		  if (! modname)
		    {
		      grub_free (name);
		      continue;
		    }

		  cmd = grub_register_extcmd_prio (name,
						   grub_dyncmd_dispatcher,
						   GRUB_COMMAND_FLAG_BLOCKS
						   | GRUB_COMMAND_FLAG_EXTCMD
						   | GRUB_COMMAND_FLAG_DYNCMD,
						   0, N_("module isn't loaded"),
						   0, prio);
		  if (! cmd)
		    {
		      grub_free (name);
		      grub_free (modname);
		      continue;
		    }
		  cmd->data = modname;

		  /* Update the active flag.  */
		  grub_command_find (name);
		}

	      grub_file_close (file);
	    }

	  grub_free (filename);
	}
    }

  /* Ignore errors.  */
  grub_errno = GRUB_ERR_NONE;
}
