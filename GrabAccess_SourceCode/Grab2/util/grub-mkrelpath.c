/* grub-mkrelpath.c - make a system path relative to its root */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2010 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <string.h>
#include <grub/util/misc.h>
#include <grub/emu/misc.h>
#include <grub/i18n.h>

#define _GNU_SOURCE	1
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

struct arguments
{
  char *pathname;
};

static struct argp_option options[] = {
  {"relative",  'r', 0, 0, "use relative path on btrfs", 0},
  { 0, 0, 0, 0, 0, 0 }
};

extern int use_relative_path_on_btrfs;

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'r':
      use_relative_path_on_btrfs = 1;
      break;
    case ARGP_KEY_ARG:
      if (state->arg_num == 0)
	arguments->pathname = xstrdup (arg);
      else
	{
	  /* Too many arguments. */
	  fprintf (stderr, _("Unknown extra argument `%s'."), arg);
	  fprintf (stderr, "\n");
	  argp_usage (state);
	}
      break;
    case ARGP_KEY_NO_ARGS:
      fprintf (stderr, "%s", _("No path is specified.\n"));
      argp_usage (state);
      exit (1);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("PATH"),
  N_("Transform a system filename into GRUB one."),
  NULL, NULL, NULL
};

int
main (int argc, char *argv[])
{
  char *relpath;
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  memset (&arguments, 0, sizeof (struct arguments));

  /* Check for options.  */
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  relpath = grub_make_system_path_relative_to_its_root (arguments.pathname);
  printf ("%s\n", relpath);
  free (relpath);

  return 0;
}
