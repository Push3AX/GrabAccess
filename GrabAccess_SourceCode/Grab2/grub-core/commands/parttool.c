/* parttool.c - common dispatcher and parser for partition operations */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/normal.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/parttool.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv2+");

static struct grub_parttool *parts = 0;
static int curhandle = 0;
static grub_dl_t mymod;
static char helpmsg[] =
  N_("Perform COMMANDS on partition.\n"
     "Use `parttool PARTITION help' for the list "
     "of available commands.");

int
grub_parttool_register(const char *part_name,
		       const grub_parttool_function_t func,
		       const struct grub_parttool_argdesc *args)
{
  struct grub_parttool *cur;
  int nargs = 0;

  if (! parts)
    grub_dl_ref (mymod);

  cur = (struct grub_parttool *) grub_malloc (sizeof (struct grub_parttool));
  cur->next = parts;
  cur->name = grub_strdup (part_name);
  cur->handle = curhandle++;
  for (nargs = 0; args[nargs].name != 0; nargs++);
  cur->nargs = nargs;
  cur->args = (struct grub_parttool_argdesc *)
    grub_calloc (nargs + 1, sizeof (struct grub_parttool_argdesc));
  if (!cur->args)
    {
      grub_free (cur);
      curhandle--;
      return -1;
    }
  grub_memcpy (cur->args, args,
	       (nargs + 1) * sizeof (struct grub_parttool_argdesc));

  cur->func = func;
  parts = cur;
  return cur->handle;
}

void
grub_parttool_unregister (int handle)
{
  struct grub_parttool *prev = 0, *cur, *t;
  for (cur = parts; cur; )
    if (cur->handle == handle)
      {
	grub_free (cur->args);
	grub_free (cur->name);
	if (prev)
	  prev->next = cur->next;
	else
	  parts = cur->next;
	t = cur;
	cur = cur->next;
	grub_free (t);
      }
    else
      {
	prev = cur;
	cur = cur->next;
      }
  if (! parts)
    grub_dl_unref (mymod);
}

static grub_err_t
show_help (grub_device_t dev)
{
  int found = 0;
  struct grub_parttool *cur;

  for (cur = parts; cur; cur = cur->next)
    if (grub_strcmp (dev->disk->partition->partmap->name, cur->name) == 0)
      {
	struct grub_parttool_argdesc *curarg;
	found = 1;
	for (curarg = cur->args; curarg->name; curarg++)
	  {
	    int spacing = 20;

	    spacing -= grub_strlen (curarg->name);
	    grub_printf ("%s", curarg->name);

	    switch (curarg->type)
	      {
	      case GRUB_PARTTOOL_ARG_BOOL:
		grub_printf ("+/-");
		spacing -= 3;
		break;

	      case GRUB_PARTTOOL_ARG_VAL:
		grub_xputs (_("=VAL"));
		spacing -= 4;
		break;

	      case GRUB_PARTTOOL_ARG_END:
		break;
	      }
	    while (spacing-- > 0)
	      grub_printf (" ");
	    grub_puts_ (curarg->desc);
	  }
      }
  if (! found)
    grub_printf_ (N_("Sorry, no parttool is available for %s\n"),
		  dev->disk->partition->partmap->name);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_parttool (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  grub_device_t dev;
  struct grub_parttool *cur, *ptool;
  int *parsed;
  int i, j;
  grub_err_t err = GRUB_ERR_NONE;

  if (argc < 1)
    {
      grub_puts_ (helpmsg);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "too few arguments");
    }

  if (args[0][0] == '(' && args[0][grub_strlen (args[0]) - 1] == ')')
    {
      args[0][grub_strlen (args[0]) - 1] = 0;
      dev = grub_device_open (args[0] + 1);
      args[0][grub_strlen (args[0]) - 1] = ')';
    }
  else
    dev = grub_device_open (args[0]);

  if (! dev)
    return grub_errno;

  if (! dev->disk)
    {
      grub_device_close (dev);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "not a disk");
    }

  if (! dev->disk->partition)
    {
      grub_device_close (dev);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "not a partition");
    }

  /* Load modules. */
  if (! grub_no_modules)
  {
    const char *prefix;
    prefix = grub_env_get ("prefix");
    if (prefix)
      {
	char *filename;

	filename = grub_xasprintf ("%s/" GRUB_TARGET_CPU "-" GRUB_PLATFORM
				   "/parttool.lst", prefix);
	if (filename)
	  {
	    grub_file_t file;

	    file = grub_file_open (filename, GRUB_FILE_TYPE_GRUB_MODULE_LIST);
	    if (file)
	      {
		char *buf = 0;
		for (;; grub_free(buf))
		  {
		    char *p, *name;

		    buf = grub_file_getline (file);

		    if (! buf)
		      break;

		    name = buf;
		    while (grub_isspace (name[0]))
		      name++;

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

		    if (grub_strcmp (name, dev->disk->partition->partmap->name)
			!= 0)
		      continue;

		    grub_dl_load (p);
		  }

		grub_file_close (file);
	      }

	    grub_free (filename);
	  }
      }
    /* Ignore errors.  */
    grub_errno = GRUB_ERR_NONE;
  }

  if (argc == 1)
    {
      err = show_help (dev);
      grub_device_close (dev);
      return err;
    }

  for (i = 1; i < argc; i++)
    if (grub_strcmp (args[i], "help") == 0)
      {
	err = show_help (dev);
	grub_device_close (dev);
	return err;
      }

  parsed = (int *) grub_calloc (argc, sizeof (int));

  for (i = 1; i < argc; i++)
    if (! parsed[i])
      {
	struct grub_parttool_argdesc *curarg;
	struct grub_parttool_args *pargs;
	for (cur = parts; cur; cur = cur->next)
	  if (grub_strcmp (dev->disk->partition->partmap->name, cur->name) == 0)
	    {
	      for (curarg = cur->args; curarg->name; curarg++)
		if (grub_strncmp (curarg->name, args[i],
				  grub_strlen (curarg->name)) == 0
		    && ((curarg->type == GRUB_PARTTOOL_ARG_BOOL
			 && (args[i][grub_strlen (curarg->name)] == '+'
			     || args[i][grub_strlen (curarg->name)] == '-'
			     || args[i][grub_strlen (curarg->name)] == 0))
			|| (curarg->type == GRUB_PARTTOOL_ARG_VAL
			    && args[i][grub_strlen (curarg->name)] == '=')))

		  break;
	      if (curarg->name)
		break;
	    }
	if (! cur)
	  {
	    grub_free (parsed);
	    grub_device_close (dev);
	    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unknown argument `%s'"),
			     args[i]);
	  }
	ptool = cur;
	pargs = (struct grub_parttool_args *)
	  grub_calloc (ptool->nargs, sizeof (struct grub_parttool_args));
	for (j = i; j < argc; j++)
	  if (! parsed[j])
	    {
	      for (curarg = ptool->args; curarg->name; curarg++)
		if (grub_strncmp (curarg->name, args[j],
				   grub_strlen (curarg->name)) == 0
		    && ((curarg->type == GRUB_PARTTOOL_ARG_BOOL
			 && (args[j][grub_strlen (curarg->name)] == '+'
			     || args[j][grub_strlen (curarg->name)] == '-'
			     || args[j][grub_strlen (curarg->name)] == 0))
			|| (curarg->type == GRUB_PARTTOOL_ARG_VAL
			    && args[j][grub_strlen (curarg->name)] == '=')))
		  {
		    parsed[j] = 1;
		    pargs[curarg - ptool->args].set = 1;
		    switch (curarg->type)
		      {
		      case GRUB_PARTTOOL_ARG_BOOL:
			pargs[curarg - ptool->args].bool
			  = (args[j][grub_strlen (curarg->name)] != '-');
			break;

		      case GRUB_PARTTOOL_ARG_VAL:
			pargs[curarg - ptool->args].str
			  = (args[j] + grub_strlen (curarg->name) + 1);
			break;

		      case GRUB_PARTTOOL_ARG_END:
			break;
		      }
		  }
	    }

	err = ptool->func (dev, pargs);
	grub_free (pargs);
	if (err)
	  break;
      }

  grub_free (parsed);
  grub_device_close (dev);
  return err;
}

static grub_command_t cmd;

GRUB_MOD_INIT(parttool)
{
  mymod = mod;
  cmd = grub_register_command ("parttool", grub_cmd_parttool,
			       N_("PARTITION COMMANDS"),
			       helpmsg);
}

GRUB_MOD_FINI(parttool)
{
  grub_unregister_command (cmd);
}
