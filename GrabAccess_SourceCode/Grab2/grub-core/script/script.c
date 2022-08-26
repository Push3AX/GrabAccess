/* script.c -- Functions to create an in memory description of the script. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2009,2010  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/script_sh.h>
#include <grub/parser.h>
#include <grub/mm.h>

/* It is not possible to deallocate the memory when a syntax error was
   found.  Because of that it is required to keep track of all memory
   allocations.  The memory is freed in case of an error, or assigned
   to the parsed script when parsing was successful.

   In case of the normal malloc, some additional bytes are allocated
   for this datastructure.  All reserved memory is stored in a linked
   list so it can be easily freed.  The original memory can be found
   from &mem.  */
struct grub_script_mem
{
  struct grub_script_mem *next;
  char mem;
};

/* Return malloc'ed memory and keep track of the allocation.  */
void *
grub_script_malloc (struct grub_parser_param *state, grub_size_t size)
{
  struct grub_script_mem *mem;
  mem = (struct grub_script_mem *) grub_malloc (size + sizeof (*mem)
						- sizeof (char));
  if (!mem)
    return 0;

  grub_dprintf ("scripting", "malloc %p\n", mem);
  mem->next = state->memused;
  state->memused = mem;
  return (void *) &mem->mem;
}

/* Free all memory described by MEM.  */
void
grub_script_mem_free (struct grub_script_mem *mem)
{
  struct grub_script_mem *memfree;

  while (mem)
    {
      memfree = mem->next;
      grub_dprintf ("scripting", "free %p\n", mem);
      grub_free (mem);
      mem = memfree;
    }
}

/* Start recording memory usage.  Returns the memory that should be
   restored when calling stop.  */
struct grub_script_mem *
grub_script_mem_record (struct grub_parser_param *state)
{
  struct grub_script_mem *mem = state->memused;
  state->memused = 0;

  return mem;
}

/* Stop recording memory usage.  Restore previous recordings using
   RESTORE.  Return the recorded memory.  */
struct grub_script_mem *
grub_script_mem_record_stop (struct grub_parser_param *state,
			     struct grub_script_mem *restore)
{
  struct grub_script_mem *mem = state->memused;
  state->memused = restore;
  return mem;
}

/* Free the memory reserved for CMD and all of it's children.  */
void
grub_script_free (struct grub_script *script)
{
  struct grub_script *s;
  struct grub_script *t;

  if (! script)
    return;

  if (script->mem)
    grub_script_mem_free (script->mem);

  s = script->children;
  while (s) {
    t = s->next_siblings;
    grub_script_unref (s);
    s = t;
  }
  grub_free (script);
}



/* Extend the argument arg with a variable or string of text.  If ARG
   is zero a new list is created.  */
struct grub_script_arg *
grub_script_arg_add (struct grub_parser_param *state,
		     struct grub_script_arg *arg, grub_script_arg_type_t type,
		     char *str)
{
  struct grub_script_arg *argpart;
  struct grub_script_arg *ll;
  int len;

  argpart =
    (struct grub_script_arg *) grub_script_malloc (state, sizeof (*arg));
  if (!argpart)
    return arg;

  argpart->type = type;
  argpart->script = 0;

  len = grub_strlen (str) + 1;
  argpart->str = grub_script_malloc (state, len);
  if (!argpart->str)
    return arg; /* argpart is freed later, during grub_script_free.  */

  grub_memcpy (argpart->str, str, len);
  argpart->next = 0;

  if (!arg)
    return argpart;

  for (ll = arg; ll->next; ll = ll->next);
  ll->next = argpart;

  return arg;
}

/* Add the argument ARG to the end of the argument list LIST.  If LIST
   is zero, a new list will be created.  */
struct grub_script_arglist *
grub_script_add_arglist (struct grub_parser_param *state,
			 struct grub_script_arglist *list,
			 struct grub_script_arg *arg)
{
  struct grub_script_arglist *link;
  struct grub_script_arglist *ll;

  grub_dprintf ("scripting", "arglist\n");

  link =
    (struct grub_script_arglist *) grub_script_malloc (state, sizeof (*link));
  if (!link)
    return list;

  link->next = 0;
  link->arg = arg;
  link->argcount = 0;

  if (!list)
    {
      link->argcount++;
      return link;
    }

  list->argcount++;

  /* Look up the last link in the chain.  */
  for (ll = list; ll->next; ll = ll->next);
  ll->next = link;

  return list;
}

/* Create a command that describes a single command line.  CMDLINE
   contains the name of the command that should be executed.  ARGLIST
   holds all arguments for this command.  */
struct grub_script_cmd *
grub_script_create_cmdline (struct grub_parser_param *state,
			    struct grub_script_arglist *arglist)
{
  struct grub_script_cmdline *cmd;

  grub_dprintf ("scripting", "cmdline\n");

  cmd = grub_script_malloc (state, sizeof (*cmd));
  if (!cmd)
    return 0;

  cmd->cmd.exec = grub_script_execute_cmdline;
  cmd->cmd.next = 0;
  cmd->arglist = arglist;

  return (struct grub_script_cmd *) cmd;
}

/* Create a command that functions as an if statement.  If BOOL is
   evaluated to true (the value is returned in envvar '?'), the
   interpreter will run the command TRUE, otherwise the interpreter
   runs the command FALSE.  */
struct grub_script_cmd *
grub_script_create_cmdif (struct grub_parser_param *state,
			  struct grub_script_cmd *exec_to_evaluate,
			  struct grub_script_cmd *exec_on_true,
			  struct grub_script_cmd *exec_on_false)
{
  struct grub_script_cmdif *cmd;

  grub_dprintf ("scripting", "cmdif\n");

  cmd = grub_script_malloc (state, sizeof (*cmd));
  if (!cmd)
    return 0;

  cmd->cmd.exec = grub_script_execute_cmdif;
  cmd->cmd.next = 0;
  cmd->exec_to_evaluate = exec_to_evaluate;
  cmd->exec_on_true = exec_on_true;
  cmd->exec_on_false = exec_on_false;

  return (struct grub_script_cmd *) cmd;
}

/* Create a command that functions as a for statement.  */
struct grub_script_cmd *
grub_script_create_cmdfor (struct grub_parser_param *state,
			   struct grub_script_arg *name,
			   struct grub_script_arglist *words,
			   struct grub_script_cmd *list)
{
  struct grub_script_cmdfor *cmd;

  grub_dprintf ("scripting", "cmdfor\n");

  cmd = grub_script_malloc (state, sizeof (*cmd));
  if (! cmd)
    return 0;

  cmd->cmd.exec = grub_script_execute_cmdfor;
  cmd->cmd.next = 0;
  cmd->name = name;
  cmd->words = words;
  cmd->list = list;

  return (struct grub_script_cmd *) cmd;
}

/* Create a "while" or "until" command.  */
struct grub_script_cmd *
grub_script_create_cmdwhile (struct grub_parser_param *state,
			     struct grub_script_cmd *cond,
			     struct grub_script_cmd *list,
			     int is_an_until_loop)
{
  struct grub_script_cmdwhile *cmd;

  cmd = grub_script_malloc (state, sizeof (*cmd));
  if (! cmd)
    return 0;

  cmd->cmd.exec = grub_script_execute_cmdwhile;
  cmd->cmd.next = 0;
  cmd->cond = cond;
  cmd->list = list;
  cmd->until = is_an_until_loop;

  return (struct grub_script_cmd *) cmd;
}

/* Create a chain of commands.  LAST contains the command that should
   be added at the end of LIST's list.  If LIST is zero, a new list
   will be created.  */
struct grub_script_cmd *
grub_script_append_cmd (struct grub_parser_param *state,
			struct grub_script_cmd *list,
			struct grub_script_cmd *last)
{
  struct grub_script_cmd *ptr;

  grub_dprintf ("scripting", "append command\n");

  if (! last)
    return list;

  if (! list)
    {
      list = grub_script_malloc (state, sizeof (*list));
      if (! list)
	return 0;

      list->exec = grub_script_execute_cmdlist;
      list->next = last;
    }
  else
    {
      ptr = list;
      while (ptr->next)
	ptr = ptr->next;

      ptr->next = last;
    }

  return list;
}



struct grub_script *
grub_script_create (struct grub_script_cmd *cmd, struct grub_script_mem *mem)
{
  struct grub_script *parsed;

  parsed = grub_malloc (sizeof (*parsed));
  if (! parsed)
    return 0;

  parsed->mem = mem;
  parsed->cmd = cmd;
  parsed->refcnt = 0;
  parsed->children = 0;
  parsed->next_siblings = 0;

  return parsed;
}

/* Parse the script passed in SCRIPT and return the parsed
   datastructure that is ready to be interpreted.  */
struct grub_script *
grub_script_parse (char *script,
		   grub_reader_getline_t getline, void *getline_data)
{
  struct grub_script *parsed;
  struct grub_script_mem *membackup;
  struct grub_lexer_param *lexstate;
  struct grub_parser_param *parsestate;

  parsed = grub_script_create (0, 0);
  if (!parsed)
    return 0;

  parsestate = grub_zalloc (sizeof (*parsestate));
  if (!parsestate)
    {
      grub_free (parsed);
      return 0;
    }

  /* Initialize the lexer.  */
  lexstate = grub_script_lexer_init (parsestate, script,
				     getline, getline_data);
  if (!lexstate)
    {
      grub_free (parsed);
      grub_free (parsestate);
      return 0;
    }

  parsestate->lexerstate = lexstate;

  membackup = grub_script_mem_record (parsestate);

  /* Parse the script.  */
  if (grub_script_yyparse (parsestate) || parsestate->err)
    {
      struct grub_script_mem *memfree;
      memfree = grub_script_mem_record_stop (parsestate, membackup);
      grub_script_mem_free (memfree);
      grub_script_lexer_fini (lexstate);
      grub_free (parsestate);
      grub_free (parsed);
      return 0;
    }

  parsed->mem = grub_script_mem_record_stop (parsestate, membackup);
  parsed->cmd = parsestate->parsed;
  parsed->children = parsestate->scripts;

  grub_script_lexer_fini (lexstate);
  grub_free (parsestate);

  return parsed;
}
