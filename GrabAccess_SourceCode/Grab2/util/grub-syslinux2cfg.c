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
#include <grub/font.h>
#include <grub/emu/hostdisk.h>

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/syslinux_parse.h>

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

struct arguments
{
  char *input;
  char *root;
  char *target_root;
  char *cwd;
  char *target_cwd;
  char *output;
  int verbosity;
  grub_syslinux_flavour_t flav;
};

static struct argp_option options[] = {
  {"target-root",  't', N_("DIR"), 0,
   N_("root directory as it will be seen on runtime [default=/]."), 0},
  {"root",  'r', N_("DIR"), 0,
   N_("root directory of the syslinux disk [default=/]."), 0},
  {"target-cwd",  'T', N_("DIR"), 0,
   N_(
      "current directory of syslinux as it will be seen on runtime  [default is parent directory of input file]."
), 0},
  {"cwd",  'c', N_("DIR"), 0,
   N_("current directory of syslinux [default is parent directory of input file]."), 0},

  {"output",  'o', N_("FILE"), 0, N_("write output to FILE [default=stdout]."), 0},
  {"isolinux",     'i', 0,      0, N_("assume input is an isolinux configuration file."), 0},
  {"pxelinux",     'p', 0,      0, N_("assume input is a pxelinux configuration file."), 0},
  {"syslinux",     's', 0,      0, N_("assume input is a syslinux configuration file."), 0},
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
    case 't':
      free (arguments->target_root);
      arguments->target_root = xstrdup (arg);
      break;

    case 'T':
      free (arguments->target_cwd);
      arguments->target_cwd = xstrdup (arg);
      break;

    case 'c':
      free (arguments->cwd);
      arguments->cwd = xstrdup (arg);
      break;

    case 'o':
      free (arguments->output);
      arguments->output = xstrdup (arg);
      break;

    case ARGP_KEY_ARG:
      if (!arguments->input)
	{
	  arguments->input = xstrdup (arg);
	  return 0;
	}
      return ARGP_ERR_UNKNOWN;

    case 'r':
      free (arguments->root);
      arguments->root = xstrdup (arg);
      return 0;

    case 'i':
      arguments->flav = GRUB_SYSLINUX_ISOLINUX;
      break;

    case 's':
      arguments->flav = GRUB_SYSLINUX_SYSLINUX;
      break;
    case 'p':
      arguments->flav = GRUB_SYSLINUX_PXELINUX;
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
  options, argp_parser, N_("FILE"),
  N_("Transform syslinux config into GRUB one."),
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

  if (!arguments.input)
    {
      fprintf (stderr, "%s", _("Missing arguments\n"));
      exit(1);
    }

  grub_init_all ();
  grub_hostfs_init ();
  grub_host_init ();

  char *t, *inpfull, *rootfull, *res;
  t = grub_canonicalize_file_name (arguments.input);
  if (!t)
    {
      grub_util_error (_("cannot open `%s': %s"), arguments.input,
		       strerror (errno));
    }  

  inpfull = xasprintf ("(host)/%s", t);
  free (t);

  t = grub_canonicalize_file_name (arguments.root ? : "/");
  if (!t)
    {
      grub_util_error (_("cannot open `%s': %s"), arguments.root,
		       strerror (errno));
    }  

  rootfull = xasprintf ("(host)/%s", t);
  free (t);

  char *cwd = xstrdup (arguments.input);
  char *p = strrchr (cwd, '/');
  char *cwdfull;
  if (p)
    *p = '\0';
  else
    {
      free (cwd);
      cwd = xstrdup (".");
    }

  t = grub_canonicalize_file_name (arguments.cwd ? : cwd);
  if (!t)
    {
      grub_util_error (_("cannot open `%s': %s"), arguments.root,
		       strerror (errno));
    }  

  cwdfull = xasprintf ("(host)/%s", t);
  free (t);

  res = grub_syslinux_config_file (rootfull, arguments.target_root ? : "/",
				   cwdfull, arguments.target_cwd ? : cwd,
				   inpfull, arguments.flav);
  if (!res)
    grub_util_error ("%s", grub_errmsg);
  if (arguments.output)
    {
      FILE *f = grub_util_fopen (arguments.output, "wb");
      if (!f)
	grub_util_error (_("cannot open `%s': %s"), arguments.output,
			 strerror (errno));
      fwrite (res, 1, strlen (res), f); 
      fclose (f);
    }
  else
    printf ("%s\n", res);
  free (res);
  free (rootfull);
  free (inpfull);
  free (arguments.root);
  free (arguments.output);
  free (arguments.target_root);
  free (arguments.input);
  free (cwdfull);
  free (cwd);

  return 0;
}
