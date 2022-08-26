/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2012,2013 Free Software Foundation, Inc.
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

#include <grub/util/misc.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/macho.h>
#include <grub/util/install.h>

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

struct arguments
{
  char *input32;
  char *input64;
  char *output;
  int verbosity;
};

static struct argp_option options[] = {
  {"input32",  '3', N_("FILE"), 0,
   N_("set input filename for 32-bit part."), 0},
  {"input64",  '6', N_("FILE"), 0,
   N_("set input filename for 64-bit part."), 0},
  {"output",  'o', N_("FILE"), 0,
   N_("set output filename. Default is STDOUT"), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case '6':
      arguments->input64 = xstrdup (arg);
      break;
    case '3':
      arguments->input32 = xstrdup (arg);
      break;

    case 'o':
      arguments->output = xstrdup (arg);
      break;

    case 'v':
      arguments->verbosity++;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("[OPTIONS]"),
  N_("Glue 32-bit and 64-bit binary into Apple universal one."),
  NULL, NULL, NULL
};

int
main (int argc, char *argv[])
{
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  /* Check for options.  */
  memset (&arguments, 0, sizeof (struct arguments));
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (!arguments.input32 || !arguments.input64)
    {
      fprintf (stderr, "%s", _("Missing input file\n"));
      exit(1);
    }

  grub_util_glue_efi (arguments.input32,
		      arguments.input64,
		      arguments.output);

  return 0;
}
