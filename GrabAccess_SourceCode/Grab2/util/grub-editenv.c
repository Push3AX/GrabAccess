/* grub-editenv.c - tool to edit environment block.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010 Free Software Foundation, Inc.
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

#include <config.h>
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/lib/envblk.h>
#include <grub/i18n.h>
#include <grub/emu/hostfile.h>
#include <grub/util/install.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"


#include "progname.h"

#define DEFAULT_ENVBLK_PATH DEFAULT_DIRECTORY "/" GRUB_ENVBLK_DEFCFG

static struct argp_option options[] = {
  {0,        0, 0, OPTION_DOC, N_("Commands:"), 1},
  {"create", 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Create a blank environment block file."), 0},
  {"list",   0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("List the current variables."), 0},
  /* TRANSLATORS: "set" is a keyword. It's a summary of "set" subcommand.  */
  {N_("set [NAME=VALUE ...]"), 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Set variables."), 0},
  /* TRANSLATORS: "unset" is a keyword. It's a summary of "unset" subcommand.  */
  {N_("unset [NAME ...]"),    0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Delete variables."), 0},

  {0,         0, 0, OPTION_DOC, N_("Options:"), -1},
  {"verbose", 'v', 0, 0, N_("print verbose messages."), 0},

  { 0, 0, 0, 0, 0, 0 }
};

/* Print the version information.  */
static void
print_version (FILE *stream, struct argp_state *state)
{
  fprintf (stream, "%s (%s) %s\n", program_name, PACKAGE_NAME, PACKAGE_VERSION);
}
void (*argp_program_version_hook) (FILE *, struct argp_state *) = print_version;

/* Set the bug report address */
const char *argp_program_bug_address = "<"PACKAGE_BUGREPORT">";

static error_t argp_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
      case 'v':
        verbosity++;
        break;

      case ARGP_KEY_NO_ARGS:
        fprintf (stderr, "%s",
		 _("You need to specify at least one command.\n"));
        argp_usage (state);
        break;

      default:
        return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case ARGP_KEY_HELP_POST_DOC:
        return xasprintf (text, DEFAULT_ENVBLK_PATH, DEFAULT_ENVBLK_PATH);

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct argp argp = {
  options, argp_parser, N_("FILENAME COMMAND"),
  "\n"N_("\
Tool to edit environment block.")
"\v"N_("\
If FILENAME is `-', the default value %s is used.\n\n\
There is no `delete' command; if you want to delete the whole environment\n\
block, use `rm %s'."),
  NULL, help_filter, NULL
};

static grub_envblk_t
open_envblk_file (const char *name)
{
  FILE *fp;
  char *buf;
  long loc;
  size_t size;
  grub_envblk_t envblk;

  fp = grub_util_fopen (name, "rb");
  if (! fp)
    {
      /* Create the file implicitly.  */
      grub_util_create_envblk_file (name);
      fp = grub_util_fopen (name, "rb");
      if (! fp)
        grub_util_error (_("cannot open `%s': %s"), name,
			 strerror (errno));
    }

  if (fseek (fp, 0, SEEK_END) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  loc = ftell (fp);
  if (loc < 0)
    grub_util_error (_("cannot get file location `%s': %s"), name,
		     strerror (errno));

  size = (size_t) loc;

  if (fseek (fp, 0, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  buf = xmalloc (size);

  if (fread (buf, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), name,
		     strerror (errno));

  fclose (fp);

  envblk = grub_envblk_open (buf, size);
  if (! envblk)
    grub_util_error ("%s", _("invalid environment block"));

  return envblk;
}

static int
print_var (const char *varname, const char *value,
           void *hook_data __attribute__ ((unused)))
{
  printf ("%s=%s\n", varname, value);
  return 0;
}

static void
list_variables (const char *name)
{
  grub_envblk_t envblk;

  envblk = open_envblk_file (name);
  grub_envblk_iterate (envblk, NULL, print_var);
  grub_envblk_close (envblk);
}

static void
write_envblk (const char *name, grub_envblk_t envblk)
{
  FILE *fp;

  fp = grub_util_fopen (name, "wb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), name,
		     strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp)
      != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), name,
		     strerror (errno));

  if (grub_util_file_sync (fp) < 0)
    grub_util_error (_("cannot sync `%s': %s"), name, strerror (errno));
  fclose (fp);
}

static void
set_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;

  envblk = open_envblk_file (name);
  while (argc)
    {
      char *p;

      p = strchr (argv[0], '=');
      if (! p)
        grub_util_error (_("invalid parameter %s"), argv[0]);

      *(p++) = 0;

      if (! grub_envblk_set (envblk, argv[0], p))
        grub_util_error ("%s", _("environment block too small"));

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);
}

static void
unset_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;

  envblk = open_envblk_file (name);
  while (argc)
    {
      grub_envblk_delete (envblk, argv[0]);

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);
}

int
main (int argc, char *argv[])
{
  const char *filename;
  char *command;
  int curindex, arg_count;

  grub_util_host_init (&argc, &argv);

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, &curindex, 0) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  arg_count = argc - curindex;

  if (arg_count == 1)
    {
      filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }
  else
    {
      filename = argv[curindex++];
      if (strcmp (filename, "-") == 0)
        filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }

  if (strcmp (command, "create") == 0)
    grub_util_create_envblk_file (filename);
  else if (strcmp (command, "list") == 0)
    list_variables (filename);
  else if (strcmp (command, "set") == 0)
    set_variables (filename, argc - curindex, argv + curindex);
  else if (strcmp (command, "unset") == 0)
    unset_variables (filename, argc - curindex, argv + curindex);
  else
    {
      char *program = xstrdup(program_name);
      fprintf (stderr, _("Unknown command `%s'.\n"), command);
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free(program);
      exit(1);
    }

  return 0;
}
