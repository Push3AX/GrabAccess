/* menuentry.c - menuentry command */
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/normal.h>
#include <grub/env_private.h>
#include <grub/mm.h>

static const struct grub_arg_option options[] =
{
  {"class", 1, GRUB_ARG_OPTION_REPEATABLE,
    N_("Menu entry type."), N_("STRING"), ARG_TYPE_STRING},
  {"users", 2, GRUB_ARG_OPTION_OPTIONAL,
    N_("List of users allowed to boot this entry."), N_("USERNAME[,USERNAME]"),
    ARG_TYPE_STRING},
  {"hotkey", 3, 0,
    N_("Keyboard key to quickly boot this entry."), N_("KEYBOARD_KEY"),
    ARG_TYPE_STRING},
  {"source", 4, 0,
    N_("Use STRING as menu entry body."), N_("STRING"), ARG_TYPE_STRING},
  {"id", 0, 0, N_("Menu entry identifier."), N_("STRING"), ARG_TYPE_STRING},
  /* TRANSLATORS: menu entry can either be bootable by anyone or only by
     handful of users. By default when security is active only superusers can
     boot a given menu entry. With --unrestricted (this option) anyone can boot it. */
  {"unrestricted", 0, 0, N_("This entry can be booted by any user."), 0,
    ARG_TYPE_NONE},
  {"help-msg", 0, GRUB_ARG_OPTION_OPTIONAL,  N_("Menu entry help message."),
    N_("STRING"), ARG_TYPE_STRING},
  {"submenu", 0, 0, N_("Define a submenu."), 0, ARG_TYPE_NONE},
  {"hidden", 0, 0, N_("Define a hidden menu entry."), 0, ARG_TYPE_NONE},
  {0, 0, 0, 0, 0, 0}
};

extern int grub_normal_exit_level;

static char *
setparams_prefix (int argc, char **args)
{
  int i;
  int j;
  char *p;
  char *result;
  grub_size_t len = 10;

  /* Count resulting string length */
  for (i = 0; i < argc; i++)
  {
    len += 3; /* 3 = 1 space + 2 quotes */
    p = args[i];
    while (*p)
      len += (*p++ == '\'' ? 4 : 1);
  }

  result = grub_malloc (len + 2);
  if (! result)
    return 0;

  grub_strcpy (result, "setparams");
  p = result + 9;

  for (j = 0; j < argc; j++)
  {
    *p++ = ' ';
    *p++ = '\'';
    p = grub_strchrsub (p, args[j], '\'', "'\\''");
    *p++ = '\'';
  }
  *p++ = '\n';
  *p = '\0';
  return result;
}

static grub_err_t
grub_cmd_menuentry (grub_extcmd_context_t ctxt, int argc, char **args)
{
  char ch;
  char *src;
  char *prefix;
  unsigned len;
  grub_err_t r;
  const char *users;
  grub_uint8_t flag = 0;

  if (ctxt->extcmd->cmd->name[0] == 's' || ctxt->state[7].set)
    flag |= GRUB_MENU_FLAG_SUBMENU;
  if (ctxt->extcmd->cmd->name[0] == 'h' || ctxt->state[8].set)
    flag |= GRUB_MENU_FLAG_HIDDEN;

  if (! argc)
  {
    if (flag & GRUB_MENU_FLAG_HIDDEN)
    {
      argc = 1;
      args = grub_zalloc (2 * sizeof (char *));
      if (!args)
        return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
      args[0] = grub_strdup ("");
      args[1] = NULL;
    }
    else
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "missing arguments");
  }

  if (ctxt->state[3].set && ctxt->script)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "multiple menuentry definitions");

  if (! ctxt->state[3].set && ! ctxt->script)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no menuentry definition");

  if (ctxt->state[1].set && ctxt->state[1].arg)
    users = ctxt->state[1].arg;
  else if (ctxt->state[5].set)
    users = NULL;
  else
    users = "";

  if (! ctxt->script)
    return grub_normal_add_menu_entry (argc, (const char **) args,
              (ctxt->state[0].set ? ctxt->state[0].args : NULL),
              ctxt->state[4].arg, users, ctxt->state[2].arg, 0, ctxt->state[3].arg,
              (ctxt->state[6].set ? ctxt->state[6].arg : NULL),
              flag, NULL, NULL);

  src = args[argc - 1];
  args[argc - 1] = NULL;

  len = grub_strlen(src);
  ch = src[len - 1];
  src[len - 1] = '\0';

  prefix = setparams_prefix (argc - 1, args);
  if (! prefix)
    return grub_errno;

  r = grub_normal_add_menu_entry (argc - 1, (const char **) args,
                ctxt->state[0].args, ctxt->state[4].arg, users, ctxt->state[2].arg,
                prefix, src + 1, (ctxt->state[6].set ? ctxt->state[6].arg : NULL),
                flag, NULL, NULL);

  src[len - 1] = ch;
  args[argc - 1] = src;
  grub_free (prefix);
  return r;
}

static grub_err_t
grub_cmd_pop_env (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                  int argc, char **args)
{

  while (argc)
  {
    struct grub_env_context *cc = grub_current_context;
    const char *value;

    value = grub_env_get (args[0]);
    if (value)
    {
      grub_current_context = grub_current_context->prev;
      while(grub_current_context && grub_env_get(args[0]))
      {
        grub_env_set(args[0], value);
        grub_current_context = grub_current_context->prev;
      }
      grub_current_context = cc;
    }
    argc--;
    args++;
  }
  return 0;
}

static grub_err_t
grub_cmd_submenu_exit (grub_extcmd_context_t ctxt __attribute__ ((unused)), int argc __attribute__ ((unused)), char **args __attribute__ ((unused)))
{
  grub_normal_exit_level = -1;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_clear_menu (grub_extcmd_context_t ctxt __attribute__ ((unused)), int argc __attribute__ ((unused)), char **args __attribute__ ((unused)))
{
  grub_normal_clear_menu ();
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd, cmd_sub, cmd_hidden, cmd_pop, cmd_sub_exit, cmd_clear_menu;

void
grub_menu_init (void)
{
  cmd = grub_register_extcmd ("menuentry", grub_cmd_menuentry,
                  GRUB_COMMAND_FLAG_BLOCKS
                  | GRUB_COMMAND_ACCEPT_DASH
                  | GRUB_COMMAND_FLAG_EXTRACTOR,
                  N_("BLOCK"), N_("Define a menu entry."), options);
  cmd_sub = grub_register_extcmd ("submenu", grub_cmd_menuentry,
                  GRUB_COMMAND_FLAG_BLOCKS
                  | GRUB_COMMAND_ACCEPT_DASH
                  | GRUB_COMMAND_FLAG_EXTRACTOR,
                  N_("BLOCK"), N_("Define a submenu."), options);
  cmd_hidden = grub_register_extcmd ("hiddenentry", grub_cmd_menuentry,
                GRUB_COMMAND_FLAG_BLOCKS
                | GRUB_COMMAND_ACCEPT_DASH
                | GRUB_COMMAND_FLAG_EXTRACTOR,
                N_("BLOCK"),
                N_("Define a hidden menu entry."), options);
  cmd_pop = grub_register_extcmd ("pop_env", grub_cmd_pop_env, 0,
                N_("variable_name [...]"),
                N_("Pass variable value to parent contexts."), 0);
  cmd_sub_exit = grub_register_extcmd ("submenu_exit", grub_cmd_submenu_exit, 0, 0,
                N_("Exit from current submenu."), 0);
  cmd_clear_menu = grub_register_extcmd ("clear_menu", grub_cmd_clear_menu, 0, 0,
                N_("Clear the current (sub)menu."), 0);
}

void
grub_menu_fini (void)
{
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd_sub);
  grub_unregister_extcmd (cmd_hidden);
  grub_unregister_extcmd (cmd_pop);
  grub_unregister_extcmd (cmd_sub_exit);
  grub_unregister_extcmd (cmd_clear_menu);
}
