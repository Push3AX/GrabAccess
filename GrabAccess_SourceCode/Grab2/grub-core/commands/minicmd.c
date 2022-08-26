/* minicmd.c - commands for the rescue mode */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2005,2006,2007,2009  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/term.h>
#include <grub/loader.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* cat FILE */
static grub_err_t
grub_mini_cmd_cat (struct grub_command *cmd __attribute__ ((unused)),
		   int argc, char *argv[])
{
  grub_file_t file;
  char buf[GRUB_DISK_SECTOR_SIZE];
  grub_ssize_t size;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_CAT);
  if (! file)
    return grub_errno;

  while ((size = grub_file_read (file, buf, sizeof (buf))) > 0)
    {
      int i;

      for (i = 0; i < size; i++)
	{
	  unsigned char c = buf[i];

	  if ((grub_isprint (c) || grub_isspace (c)) && c != '\r')
	    grub_printf ("%c", c);
	  else
	    {
	      grub_setcolorstate (GRUB_TERM_COLOR_HIGHLIGHT);
	      grub_printf ("<%x>", (int) c);
	      grub_setcolorstate (GRUB_TERM_COLOR_STANDARD);
	    }
	}
    }

  grub_xputs ("\n");
  grub_refresh ();
  grub_file_close (file);

  return 0;
}

/* help */
static grub_err_t
grub_mini_cmd_help (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char *argv[] __attribute__ ((unused)))
{
  grub_command_t p;

  for (p = grub_command_list; p; p = p->next)
    grub_printf ("%s (%d%c)\t%s\n", p->name,
		 p->prio & GRUB_COMMAND_PRIO_MASK,
		 (p->prio & GRUB_COMMAND_FLAG_ACTIVE) ? '+' : '-',
		 p->description);

  return 0;
}

/* dump ADDRESS [SIZE] */
static grub_err_t
grub_mini_cmd_dump (struct grub_command *cmd __attribute__ ((unused)),
		    int argc, char *argv[])
{
  grub_uint8_t *addr;
  grub_size_t size = 4;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no address specified");

#if GRUB_CPU_SIZEOF_VOID_P == GRUB_CPU_SIZEOF_LONG
#define grub_strtoaddr grub_strtoul
#else
#define grub_strtoaddr grub_strtoull
#endif

  addr = (grub_uint8_t *) grub_strtoaddr (argv[0], 0, 0);
  if (grub_errno)
    return grub_errno;

  if (argc > 1)
    size = (grub_size_t) grub_strtoaddr (argv[1], 0, 0);

  while (size--)
    {
      grub_printf ("%x%x ", *addr >> 4, *addr & 0xf);
      addr++;
    }

  return 0;
}

/* rmmod MODULE */
static grub_err_t
grub_mini_cmd_rmmod (struct grub_command *cmd __attribute__ ((unused)),
		     int argc, char *argv[])
{
  grub_dl_t mod;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no module specified");

  mod = grub_dl_get (argv[0]);
  if (! mod)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no such module");

  if (grub_dl_is_persistent (mod))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "cannot unload persistent module");

  if (grub_dl_ref_count (mod) > 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "cannot unload referenced module");

  grub_dl_unref (mod);
  grub_dl_unload (mod);

  return 0;
}

/* lsmod */
static grub_err_t
grub_mini_cmd_lsmod (struct grub_command *cmd __attribute__ ((unused)),
		     int argc __attribute__ ((unused)),
		     char *argv[] __attribute__ ((unused)))
{
  grub_dl_t mod;

  /* TRANSLATORS: this is module list header.  Name
     is module name, Ref Count is a reference counter
     (how many modules or open descriptors use it).
     Dependencies are the other modules it uses.
   */
  grub_printf_ (N_("Name\tRef Count\tDependencies\n"));
  FOR_DL_MODULES (mod)
  {
    grub_dl_dep_t dep;

    grub_printf ("%s\t%d\t\t", mod->name, mod->ref_count);
    for (dep = mod->dep; dep; dep = dep->next)
      {
	if (dep != mod->dep)
	  grub_xputs (",");

	grub_printf ("%s", dep->mod->name);
      }
    grub_xputs ("\n");
  }

  return 0;
}

/* exit */
static grub_err_t
grub_mini_cmd_exit (struct grub_command *cmd __attribute__ ((unused)),
		    int argc, char *argv[])
{
  int retval = -1;
  unsigned long n;

  if (argc < 0 || argc > 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  if (argc == 1)
    {
      n = grub_strtoul (argv[0], 0, 10);
      if (n != ~0UL)
	retval = n;
    }

  grub_exit (retval);
  /* Not reached.  */
}

static grub_command_t cmd_cat, cmd_help;
static grub_command_t cmd_dump, cmd_rmmod, cmd_lsmod, cmd_exit;

GRUB_MOD_INIT(minicmd)
{
  cmd_cat =
    grub_register_command ("cat", grub_mini_cmd_cat,
			   N_("FILE"), N_("Show the contents of a file."));
  cmd_help =
    grub_register_command ("help", grub_mini_cmd_help,
			   0, N_("Show this message."));
  cmd_dump =
    grub_register_command ("dump", grub_mini_cmd_dump,
			   N_("ADDR [SIZE]"), N_("Show memory contents."));
  cmd_rmmod =
    grub_register_command ("rmmod", grub_mini_cmd_rmmod,
			   N_("MODULE"), N_("Remove a module."));
  cmd_lsmod =
    grub_register_command ("lsmod", grub_mini_cmd_lsmod,
			   0, N_("Show loaded modules."));
  cmd_exit =
    grub_register_command ("exit", grub_mini_cmd_exit,
			   0, N_("Exit from GRUB."));
}

GRUB_MOD_FINI(minicmd)
{
  grub_unregister_command (cmd_cat);
  grub_unregister_command (cmd_help);
  grub_unregister_command (cmd_dump);
  grub_unregister_command (cmd_rmmod);
  grub_unregister_command (cmd_lsmod);
  grub_unregister_command (cmd_exit);
}
