/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

struct grub_alias
{
  char *name;
  char *cmd;
  char *help;
  grub_command_t func;
  struct grub_alias *next;
};
typedef struct grub_alias *grub_alias_t;

static grub_alias_t grub_alias_list = NULL;

static grub_err_t
my_cmd (grub_command_t cmd, int argc, char **argv)
{
  int my_argc, i;
  char **my_argv;
  grub_command_t func;
  if (grub_parser_split_cmdline (cmd->description, NULL, NULL, &my_argc, &my_argv)
      || my_argc < 0)
    return grub_errno;
  if (my_argc == 0)
    return GRUB_ERR_NONE;
  my_argv = grub_realloc (my_argv, sizeof (char *) * (argc + my_argc));
  if (!my_argv)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
    goto quit;
  }
  func = grub_command_find (my_argv[0]);
  if (! func)
  {
    grub_error (GRUB_ERR_BAD_OS, N_("Unknown command `%s'.\n"), my_argv[0]);
    goto quit;
  }
  if (my_argc + argc < 2)
  {
    (func->func) (func, 0, NULL);
    goto quit;
  }
  for (i = 0; i < argc; i++)
    my_argv[i + my_argc] = argv[i];
  (func->func) (func, my_argc + argc - 1, &my_argv[1]);
quit:
  if (my_argv[0])
    grub_free (my_argv[0]);
  if (my_argv)
    grub_free (my_argv);
  return grub_errno;
}

static grub_alias_t
grub_alias_find (const char *name)
{
  grub_alias_t f;
  for (f = grub_alias_list; f; f = f->next)
  {
    if (f->name && grub_strcmp (name, f->name) == 0)
      break;
  }
  return f;
}

static grub_alias_t
grub_alias_create (const char *name, const char *cmd, const char *help)
{
  grub_alias_t f = NULL;
  f = grub_zalloc (sizeof (struct grub_alias));
  if (! f)
    return NULL;
  f->name = grub_strdup (name);
  f->cmd = grub_strdup (cmd);
  if (help)
    f->help = grub_strdup (help);
  if (! f->name || ! f->cmd)
    goto out_of_mem;
  f->func = grub_register_command (f->name, my_cmd, f->help, f->cmd);
  return f;
out_of_mem:
  if (f->name)
    grub_free (f->name);
  if (f->cmd)
    grub_free (f->cmd);
  if (f->help)
    grub_free (f->help);
  grub_free (f);
  return NULL;
}

static grub_err_t
grub_alias_add (const char *name, const char *cmd, const char *help)
{
  grub_alias_t f;
  for (f = grub_alias_list; f; f = f->next)
  {
    if (f->name && grub_strcmp (name, f->name) == 0)
    {
      char *p = f->cmd;
      grub_dprintf ("alias", "overwrite \'%s\' -> \'%s\'", p, cmd);
      p = grub_strdup (cmd);
      if (! p)
        return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
      grub_free (f->cmd);
      f->cmd = p;
      if (f->help)
        grub_free (f->help);
      if (help)
        f->help = grub_strdup (help);
      grub_unregister_command (f->func);
      f->func = grub_register_command (f->name, my_cmd, f->help, f->cmd);
      return GRUB_ERR_NONE;
    }
    if (! f->next)
    {
      grub_dprintf ("alias", "append %s=\'%s\'", name, cmd);
      f->next = grub_alias_create (name, cmd, help);
      if (! f->next)
        return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
      return GRUB_ERR_NONE;
    }
  }
  grub_dprintf ("alias", "add alias %s=\'%s\'", name, cmd);
  f = grub_alias_create (name, cmd, help);
  if (! f)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  grub_alias_list = f;
  return GRUB_ERR_NONE;
}

static void
grub_alias_remove (const char *name)
{
  grub_alias_t *p, q;
  for (p = &grub_alias_list, q = *p; q; p = &(q->next), q = q->next)
  {
    if (q->name && grub_strcmp (name, q->name) == 0)
    {
      *p = q->next;
      grub_unregister_command (q->func);
      grub_free (q->name);
      grub_free (q->cmd);
      if (q->help)
        grub_free (q->help);
      grub_free (q);
      break;
    }
  }
}

static void
grub_alias_print (const grub_alias_t alias)
{
  if (! alias || ! alias->name || ! alias->cmd)
  {
    grub_printf ("alias not found.\n");
    return;
  }
  grub_printf ("%s = \'%s\'\n", alias->name, alias->cmd);
}

static grub_err_t
grub_cmd_alias (grub_command_t cmd __attribute__((__unused__)),
                int argc, char *argv[])
{
  grub_alias_t f;
  char *help = NULL;

  if (argc > 1)
  {
    help = argv[2];
    return grub_alias_add (argv[0], argv[1], help);
  }
  if (argc == 1)
  {
    f = grub_alias_find (argv[0]);
    grub_alias_print (f);
    return GRUB_ERR_NONE;
  }
  for (f = grub_alias_list; f; f = f->next)
    grub_alias_print (f);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_unalias (grub_command_t cmd __attribute__((__unused__)),
                int argc, char *argv[])
{
  int i;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("bad argument"));
  for (i = 0; i < argc; i++)
    grub_alias_remove (argv[i]);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_type (grub_command_t cmd __attribute__((__unused__)),
                   int argc, char *argv[])
{
  grub_command_t func;
  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("bad argument"));
  func = grub_command_find (argv[0]);
  if (! func)
    return GRUB_ERR_TEST_FAILURE;
  else
    return GRUB_ERR_NONE;
}

static grub_command_t cmd_alias, cmd_unalias, cmd_type;

GRUB_MOD_INIT(alias)
{
  cmd_alias = grub_register_command ("alias", grub_cmd_alias,
                                     N_("NAME COMMAND [SUMMARY]"),
                                     N_("Create aliases."));
  cmd_unalias = grub_register_command ("unalias", grub_cmd_unalias,
                                       N_("NAME"),
                                       N_("Delete aliases."));
  cmd_type = grub_register_command ("type", grub_cmd_type,
                                    N_("NAME"),
                                    N_("Check whether a command exists."));
}

GRUB_MOD_FINI(alias)
{
  grub_unregister_command (cmd_alias);
  grub_unregister_command (cmd_unalias);
  grub_unregister_command (cmd_type);
}

